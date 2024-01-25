#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <ctype.h>
#include <sys/types.h>
#include <netinet/ip.h> 
#include <fcntl.h>
#include <errno.h>
#include <signal.h>
#include <time.h>
#define PORT 8080
#define MAX_BUFF 2048
#define MAX_COMMAND_LENGTH 256
#define MAX_USERNAME 50
#define MAX_PASSWORD 50
#define HEARTBEAT_INTERVAL 5
#define MAX_MESSAGE_LENGTH 1900  

struct training_session* find_session_by_id(int session_id);
int is_client_in_session(const char* username, const struct training_session* session);
int is_session_active(const struct training_session* session);
void broadcast_message_to_session(struct training_session* session,const char* message, int sender_sockfd);

#define MAX(a,b) ((a) > (b) ? (a) : (b))
struct user_account {
    int id;
    char username[MAX_USERNAME];
    char password[MAX_PASSWORD];
    int is_instructor; // 1 dla prowadzącego, 0 dla uczestnika
    int is_logged_in;
    int training_id; // Identifikator szkolenia
};

void get_training_id(struct user_account *user) {
    char buffer[100];
    int trainingID;
    int isValid;
    char *endptr;

    do {
        isValid = 1;

        printf("Wprowadź identyfikator szkolenia (1-100): ");
        if (fgets(buffer, sizeof(buffer), stdin) == NULL) {
            // Obsługa błędów odczytu
            continue;
        }

        // Trimowanie białych znaków
        buffer[strcspn(buffer, "\n")] = 0; // Usuń znak nowej linii na końcu

        // Konwersja na liczbę
        trainingID = (int)strtol(buffer, &endptr, 10);

        // Sprawdź, czy konwersja się powiodła i czy pozostałe znaki to tylko białe znaki
        if (*endptr != '\0' || trainingID <= 0) {
            isValid = 0;
            printf("Nieprawidłowy identyfikator. Użyj tylko liczb większych od 0.\n");
        }
    } while (!isValid);

    // Aktualizacja identyfikatora szkolenia w strukturze użytkownika
    user->training_id = trainingID;
}
void register_user(struct user_account *user) {
    printf("Wpisz nazwe uzytkownika: ");
    scanf("%s", user->username); // Assuming username is a char array in struct
    getchar();
    printf("Podaj haslo: ");
    scanf("%s", user->password); // Assuming password is a char array in struct
    getchar();
}



void logout_user(struct user_account *user) {
    user->is_logged_in = 0;
    printf("Wylogowano pomyslnie.\n");
}


int receive_server_response_with_timeout(int sockfd, struct sockaddr_in *servaddr, char *buffer, int len, int timeout_seconds) {
    fd_set readfds;
    struct timeval tv;
    int retval;
    socklen_t servlen = sizeof(*servaddr);

    FD_ZERO(&readfds);
    FD_SET(sockfd, &readfds);

    tv.tv_sec = timeout_seconds;  // Timeout
    tv.tv_usec = 0;

    retval = select(sockfd + 1, &readfds, NULL, NULL, &tv);

    if (retval == -1) {
        perror("select() error");
        return -1;
    } else if (retval) {
        ssize_t n = recvfrom(sockfd, buffer, len, 0, (struct sockaddr *)servaddr, &servlen);
        if (n < 0) {
            perror("recvfrom() error");
            return -1;
        }
        buffer[n] = '\0'; // Null-terminate the string
        
        return 0;
    } else {
        printf("No data within %d seconds.\n", timeout_seconds);
        return -1;
    }
}


void send_heartbeat(int sockfd, struct sockaddr_in *servaddr, struct user_account *currentUser, time_t *last_heartbeat_time) {
    if (difftime(time(NULL), *last_heartbeat_time) >= 5) {
        char heartbeat_msg[MAX_BUFF];
        snprintf(heartbeat_msg, sizeof(heartbeat_msg), "HEARTBEAT %s", currentUser->username);
        if (sendto(sockfd, heartbeat_msg, strlen(heartbeat_msg), 0, (const struct sockaddr *)servaddr, sizeof(*servaddr)) < 0) {
            perror("Error sending HEARTBEAT message");
        }
        *last_heartbeat_time = time(NULL);
    }
}
void enter_session_chat(int sockfd, fd_set *readfds, struct timeval *tv, struct user_account *currentUser, int *session_id_to_join, struct sockaddr_in *servaddr, time_t *last_heartbeat_time) {
    char recv_buffer[MAX_BUFF];
    char send_buffer[MAX_BUFF];
    char send_message_command[MAX_BUFF + MAX_COMMAND_LENGTH];
    
    printf("Dołączanie do chatu sesji. Napisz 'exit' aby wyjść.\n");

    while (1) {
        FD_ZERO(readfds);
        FD_SET(sockfd, readfds); 
        FD_SET(STDIN_FILENO, readfds); 

        tv->tv_sec = 0;
        tv->tv_usec = 500000;  // 0.5 sekundy

        int retval = select(MAX(sockfd, STDIN_FILENO) + 1, readfds, NULL, NULL, tv);
        if (retval == -1) {
            perror("Error in select()");
            break;
        } 

        if (FD_ISSET(sockfd, readfds)) {
            int n = recv(sockfd, recv_buffer, sizeof(recv_buffer) - 1, 0);
            if (n > 0) {
                recv_buffer[n] = '\0';
                printf("%s\n", recv_buffer);
            } else if (n < 0) {
                perror("Error receiving data");
                continue; // Continue the loop in case of error
            }
        }

        if (FD_ISSET(STDIN_FILENO, readfds)) {
            fgets(send_buffer, sizeof(send_buffer), stdin);
            send_buffer[strcspn(send_buffer, "\n")] = 0;

            if (strcmp(send_buffer, "exit") == 0) {
                printf("Exiting chat...\n");

                

                break;
            }

            snprintf(send_message_command, sizeof(send_message_command), "SEND_MESSAGE %s %d %s",
                     currentUser->username, *session_id_to_join, send_buffer);

            if (sendto(sockfd, send_message_command, strlen(send_message_command), 0, (struct sockaddr *)servaddr, sizeof(*servaddr)) < 0) {
                perror("Error sending message");
            }
        }

        send_heartbeat(sockfd, servaddr, currentUser, last_heartbeat_time);
    }
}


void handle_session_active_state(int sockfd, struct sockaddr_in *servaddr, fd_set *readfds, struct timeval *tv, struct user_account *currentUser, int *session_active, int *session_id_to_join, time_t *last_heartbeat_time, int *joined_session) {
    printf("Jesteś w sesji: 1 -Wyjdź z sesji, 2 - Wejdź w chat\nWybór: ");
    char action_choice[10];
    fgets(action_choice, sizeof(action_choice), stdin);
    int choice = atoi(action_choice);
    char sendline[MAX_BUFF];

    switch (choice) {
        case 1:
            // Powiadom serwer, że uczestnik opuszcza sesję
            sprintf(sendline, "LEAVE_SESSION %s %d", currentUser->username, *session_id_to_join);
            if (sendto(sockfd, sendline, strlen(sendline), 0, (struct sockaddr *)servaddr, sizeof(*servaddr)) < 0) {
                perror("Error sending LEAVE_SESSION message");
            }

            printf("Opuszczono sesję o ID %d.\n", *session_id_to_join);
            *session_active = 0;
            *joined_session = 0;  // Użytkownik opuścił sesję
            break;        
        case 2:
            if (!*joined_session) {
                // Powiadom serwer o dołączeniu do chatu (JOIN_TRAINING)
                sprintf(sendline, "JOIN_TRAINING %d", *session_id_to_join);
                if (sendto(sockfd, sendline, strlen(sendline), 0, (struct sockaddr *)servaddr, sizeof(*servaddr)) < 0) {
                    perror("Error sending JOIN_TRAINING message");
                }
                *joined_session = 1;  // Użytkownik dołączył do sesji
            }

            // Wejście w chat sesji
            enter_session_chat(sockfd, readfds, tv, currentUser, session_id_to_join, servaddr, last_heartbeat_time);
            break;
    }
}

