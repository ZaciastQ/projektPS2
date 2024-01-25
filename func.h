
#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include <stdio.h>
#include <stdlib.h>

#include <netinet/in.h>
#include <arpa/inet.h>
#include <string.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>
#include <time.h>
#include <syslog.h>
#include <sys/select.h>
#include <errno.h>
#include <signal.h>
#include <sys/wait.h>
#include <fcntl.h>
#include <ctype.h> //isdigit()
#define MAX_CLIENTS 100
#define MAX_SESSION_CLIENTS 100
#define MAX_SESSIONS 101
#define MAX_PARTICIPANTS 100
#define PORT 8080
#define MAX_BUFF 1024
#define MAX(a,b) ((a) > (b) ? (a) : (b))
#define MAX_USERNAME 50
#define MAX_PASSWORD 50
int empty_slots[MAX_SESSION_CLIENTS];
int active_sockets[MAX_CLIENTS];

struct user_account {
    struct sockaddr_in address; 
    char username[MAX_USERNAME];
    char password[MAX_PASSWORD];
    int is_instructor; // 1 dla prowadzącego, 0 dla uczestnika
    int is_logged_in;
    int training_id; // Identifikator szkolenia
    time_t last_seen;
    int check_heartbeat;
};
struct training_session {
    int training_id;
    int active; // 1 oznacza aktywną sesję, 0 oznacza nieaktywną
    char participants[MAX_PARTICIPANTS][MAX_USERNAME];
    int num_participants;
};

// Struktura reprezentująca klienta sesji
struct session_client {
    struct user_account user;
    int session_id;
};
struct user_account active_users[MAX_CLIENTS];
struct session_client session_clients[MAX_SESSION_CLIENTS];
struct training_session sessions[MAX_SESSIONS];
char username[MAX_USERNAME], password[MAX_PASSWORD];
int is_instructor(char *username);

void initialize_active_users() {
    for (int i = 0; i < MAX_CLIENTS; i++) {
        active_users[i].is_logged_in = 0;  
        active_users[i].training_id = -1;  
        strcpy(active_users[i].username, "");  
        strcpy(active_users[i].password, "");  
        active_users[i].is_instructor = 0; 
        active_sockets[i] = -1;
        active_users[i].check_heartbeat = 0;
    }
}
// Tablica przechowująca klientów sesji
// Inicjalizacja tablicy klientów sesji
void initialize_session_clients() {
    for (int i = 0; i < MAX_SESSION_CLIENTS; i++) {
        session_clients[i].session_id = -1;
        empty_slots[i] = i;  // Initially, all slots are empty
    }
}


int add_client_to_session(struct session_client *new_client, struct training_session *sessions, int max_sessions) {
    for (int i = 0; i < max_sessions; i++) {
        if (sessions[i].training_id == new_client->session_id && sessions[i].active) {
            // Sprawdź, czy użytkownik już uczestniczy w sesji
            for (int j = 0; j < sessions[i].num_participants; j++) {
                if (strcmp(sessions[i].participants[j], new_client->user.username) == 0) {
                    return -4; // Użytkownik jest już w sesji
                }
            }
            // Sprawdź, czy jest miejsce w sesji
            if (sessions[i].num_participants < MAX_PARTICIPANTS) {
                strcpy(sessions[i].participants[sessions[i].num_participants], new_client->user.username);
                sessions[i].num_participants++;

                // Znajdź i zaktualizuj odpowiedniego użytkownika w active_users
                for (int k = 0; k < MAX_CLIENTS; k++) {
                    if (strcmp(active_users[k].username, new_client->user.username) == 0) {
                        active_users[k].check_heartbeat = 1;
                        break; // Przerwij po znalezieniu i aktualizacji użytkownika
                    }
                }

                return 0; // Dodano pomyślnie
            } else {
                return -1; // Sesja jest pełna
            }
        }
    }
    return -2; // Sesja szkoleniowa nie znaleziona
}




// Funkcja do sprawdzania, czy klient należy do sesji
int is_client_in_session(const char* username, const struct training_session* session) {
    // Sprawdza wszystkich uczestników sesji
    for (int i = 0; i < session->num_participants; i++) {
        // Porównuje nazwę użytkownika z nazwami uczestników sesji
        if (strcmp(username, session->participants[i]) == 0) {
            return 1; // Użytkownik jest już w sesji
        }
    }
    return 0; // Użytkownik nie jest w sesji
}

int is_session_active(const struct training_session* session) {
    // Sprawdza, czy pole 'active' sesji jest ustawione na 1
    if (session->active == 1) {
        return 1; // Sesja jest aktywna
    } else {
        return 0; // Sesja nie jest aktywna
    }
}

struct training_session* find_session_by_id(int session_id) {
    for (int i = 0; i < MAX_SESSIONS; i++) {
        if (sessions[i].training_id == session_id) {
            return &sessions[i];
        }
    }
    return NULL; // Nie znaleziono sesji
}

//procesy zombie usuwane (brak fork())
void sig_chld(int signo) {
    pid_t pid;
    int stat;

    pid = wait(&stat);
    printf("child %d terminated\n", pid);
    return;
}


int register_user_on_server(struct user_account *user) {
    FILE *fp = fopen("users.txt", "a+");
    if (fp == NULL) {
        perror("Error opening file");
        return 2; // File error
    }

    struct user_account temp;
    int role;

    while (fscanf(fp, "%d %s %s", &role, temp.username, temp.password) != EOF) {
        if (strcmp(temp.username, user->username) == 0) {
            printf("Użytkownik istnieje !\n");
            fclose(fp);
            return 1; // name istnieje
        }
    }

    // dodaj do pliku
    fprintf(fp, "%d %s %s\n", user->is_instructor, user->username, user->password);

    fclose(fp);
    printf("Użytkownik zarejestrowany pomyślnie.\n");
    return 0; // sukces
}
int login_user_on_server(struct user_account *user, int sockfd){
    FILE *fp = fopen("users.txt", "r");
    if (fp == NULL) {
        perror("Error opening file");
        return 0;  //  w przypadku error
    }

    struct user_account temp;
    int id; 
    while (fscanf(fp, "%d %s %s", &id, temp.username, temp.password) != EOF) {
        if (strcmp(temp.username, user->username) == 0 && strcmp(temp.password, user->password) == 0) {
            user->is_logged_in = 1;

            // zachowaj deskryptor dla usera
            for (int i = 0; i < MAX_CLIENTS; i++) {
                if (active_sockets[i] == -1) {
                    active_sockets[i] = sockfd;
                    break;
                }
            }

            fclose(fp);
            return 1;  //  1 w przypadku zalogowania na serwer
        }
    }

    fclose(fp);
    return 0;  // 0 gdy się nie udało zalogować
}


void handle_exit_on_server(struct user_account *user) {
    user->is_logged_in = 0;
    printf("Wylogowano.\n");
}
void send_response(int sockfd, const struct sockaddr_in *cliaddr, socklen_t len, const char *message) {
    if (sendto(sockfd, message, strlen(message), 0, (const struct sockaddr *)cliaddr, len) < 0) {
        perror("Błąd w wysyłaniu odpowiedzi");
        exit(EXIT_FAILURE);
    }
}

int handle_create_session(struct user_account *user) {
    if (!user->is_instructor) {
        printf("Tylko instruktorzy mogą tworzyć sesje.\n");
        return -1;
    }

    int session_id = user->training_id; // Używamy ID sesji z currentUser

    // sprawdź id sesji
    if (session_id < 0 || session_id >= MAX_SESSIONS) {
        printf("Nieprawidłowy ID sesji.\n");
        return -3; 
    }

    // sprawdz czy istnieje
    if (sessions[session_id].active) {
        printf("Sesja szkoleniowa o ID %d już istnieje.\n", session_id);
        return -2;
    }

    // stwórz sesje
    struct training_session new_session = {0}; // pola na 0 - inicjalizacja
    new_session.training_id = session_id;
    new_session.active = 1;

    // dodaj sesje do tablicy sesji
    sessions[session_id] = new_session;

    printf("Utworzono sesję szkoleniową o ID: %d\n", session_id);
    return session_id;
}



void broadcast_message_to_session(struct training_session* session, const char* full_message, const char* sender_username) {
    if (!session) {
        printf("Error: No session provided to broadcast.\n");
        return;
    }
    printf("Rozglaszanie wiadomosci '%s' w sesji %d\n", full_message, session->training_id);
    
    for (int i = 0; i < session->num_participants; i++) {
        if (strcmp(session->participants[i], sender_username) != 0) {
            for (int j = 0; j < MAX_CLIENTS; j++) {
                if (strcmp(active_users[j].username, session->participants[i]) == 0 &&
                    active_users[j].is_logged_in &&
                    active_sockets[j] != -1) {
                    
                    
                    struct sockaddr_in dest_addr = active_users[j].address;
                    socklen_t addr_len = sizeof(dest_addr);
                    
                    // Uzycie sendto() dla UDP 
                    ssize_t bytes_sent = sendto(active_sockets[j], full_message, strlen(full_message), 0,
                                                (struct sockaddr *)&dest_addr, addr_len);
                    if (bytes_sent < 0) {
                        perror("Error sending message to participant");
                        printf("Nie udalo sie wyslac wiadomosci %s na socket %d\n", session->participants[i], active_sockets[j]);
                    } else {
                        printf("Message sent to %s at socket %d\n", session->participants[i], active_sockets[j]);
                    }
                    break;
                }
            }
        }
    }
}



void notify_instructor(int session_id, char *notification, int sockfd) {
    // inicjalizacja sesji i instruktora
    struct training_session *session = NULL;
    struct user_account instructor;
    int instructor_found = 0;  

    
    syslog(LOG_INFO," Powiadomienie %d z wiadomoscia: %s\n", session_id, notification);

    for (int i = 0; i < MAX_SESSIONS; i++) {
        if (sessions[i].training_id == session_id && sessions[i].active) {
            session = &sessions[i];
            syslog(LOG_INFO,"znaleziono aktywna sesje %d\n", session_id);
            break;
        }
    }

    if (session) {
        for (int i = 0; i < MAX_CLIENTS; i++) {
            if (active_users[i].training_id == session_id && active_users[i].is_instructor) {
                instructor = active_users[i];
                instructor_found = 1;                 
               syslog(LOG_INFO," instruktor znaleziony z nazwa: %s\n", instructor.username);
                break;
            }
        }

        // sprawdz czy instruktor znaleziony przed wysłaniem powiadomienia
        if (instructor_found) {
            syslog(LOG_INFO," Instructor's IP address: %s\n", inet_ntoa(instructor.address.sin_addr));
            
            // notification instructor
            struct sockaddr_in instr_addr = instructor.address;
            ssize_t bytes_sent = sendto(sockfd, notification, strlen(notification), 0, (const struct sockaddr *)&instr_addr, sizeof(instr_addr));
            
            if (bytes_sent < 0) {
                perror(" Error sending notification to instructor");
            } else {
                printf(" Notyfikacja wyslana do instruktora (%ld bytes)\n", bytes_sent);
            }
        } else {
             syslog(LOG_INFO,"Brak instruktora dla sesji o ID %d\n", session_id);
        }
    } else {
        syslog(LOG_INFO," Sesja ID %d nie znaleziona\n", session_id);
    }
}



int check_user_role(char *username) {
    FILE *file;
    char line[100];
    int role;
    char file_username[MAX_USERNAME];
    char file_password[MAX_PASSWORD];

    file = fopen("users.txt", "r"); // Otwórz plik z użytkownikami
    if (file == NULL) {
        perror("Error opening file");
        return -1; // Zwróć -1, jeśli plik nie może być otwarty
    }

    while (fgets(line, sizeof(line), file)) {
        // Rozdziel linie na role, nazwy użytkowników i hasła
        if (sscanf(line, "%d %s %s", &role, file_username, file_password) == 3) {
            if (strcmp(username, file_username) == 0) {
                fclose(file); // Zamknij plik przed zwróceniem roli
                return role; // Zwróć rolę, jeśli nazwy użytkowników się zgadzają
            }
        }
    }

    fclose(file); // Zamknij plik, jeśli użytkownik nie zostanie znaleziony
    return -1; // Zwróć -1, jeśli użytkownik nie zostanie znaleziony
}
int is_instructor(char *username) {
    return check_user_role(username) == 1; 
}
void update_user_last_seen(const char* username) {
    for (int i = 0; i < MAX_CLIENTS; i++) {
        if (strcmp(active_users[i].username, username) == 0 && active_users[i].is_logged_in) {
            time(&active_users[i].last_seen);  // Update 
            printf("Zaaktualizowano czas dla %s\n", username);
            return;
        }
    }
    printf("User %s not found or not logged in\n", username);
}
int remove_client_from_session(const char* username, int session_id, int sockfd) {
    struct training_session* session = NULL;
    for (int i = 0; i < MAX_SESSIONS; i++) {
        if (sessions[i].training_id == session_id) {
            session = &sessions[i];
            break;
        }
    }

    if (session == NULL || !session->active) return -1;

    int removed = 0;
    for (int i = 0; i < session->num_participants; i++) {
        if (strcmp(session->participants[i], username) == 0) {
            // Usuń użytkownika z listy uczestników
            for (int j = i; j < session->num_participants - 1; j++) {
                strcpy(session->participants[j], session->participants[j + 1]);
            }
            session->num_participants--;
            removed = 1;

            // Powiadom instruktora o opuszczeniu sesji przez użytkownika z dodaniem czasu
            if (session->active) {
                char notification[MAX_BUFF * 2];
                char time_buffer[30];
                time_t now = time(NULL);
                struct tm *tm_struct = localtime(&now);
                strftime(time_buffer, sizeof(time_buffer), "%Y-%m-%d %H:%M:%S", tm_struct);
                
                snprintf(notification, sizeof(notification), "Notification: %s opuscil sesje o %s", username, time_buffer);
                notify_instructor(session_id, notification, sockfd);
            }
            break;
        }
    }

    // Reset 
    for (int i = 0; i < MAX_CLIENTS; i++) {
        if (strcmp(active_users[i].username, username) == 0) {
            active_users[i].training_id = -1;
            active_users[i].is_logged_in = 1; 
            active_users[i].check_heartbeat = 1; 
            break;
        }
    }

    return removed ? 0 : -2; // 0 jeśli usunięto, -2 jeśli nie znaleziono użytkownika
}

void check_heartbeats(int sockfd) {
    time_t current_time;
    time(&current_time);

    for (int i = 0; i < MAX_CLIENTS; i++) {
        if (active_users[i].is_logged_in && active_users[i].check_heartbeat && difftime(current_time, active_users[i].last_seen) > 250) {
            // Użytkownik przekroczył czas bez aktywności, usuwamy go
            printf("Użytkownik %s został usunięty z sesji ze względu na brak aktywności.\n", active_users[i].username);

            // Tworzenie powiadomienia
            char notification[MAX_BUFF];
            sprintf(notification, "Użytkownik %s został usunięty z sesji ze względu na brak aktywności.", active_users[i].username);
            
            // Wywołanie funkcji notify_instructor
            notify_instructor(active_users[i].training_id, notification, sockfd);

            // Usuwanie użytkownika z sesji itd.
            remove_client_from_session(active_users[i].username, active_users[i].training_id, sockfd);
            active_users[i].is_logged_in = 0;  // Oznacz użytkownika jako wylogowanego
            active_users[i].check_heartbeat = 0;  // Przestań sprawdzać HEARTBEAT
            active_sockets[i] = -1;  // Resetuj socket
        }
    }
}
void shutdown_handler(int signo) {
    // Wysyłanie powiadomienia do wszystkich aktywnych uczestników sesji
    for (int i = 0; i < MAX_SESSIONS; i++) {
        if (sessions[i].active) {
            char notification[MAX_BUFF] = "Serwer zostaje zamknięty. Prosimy o wylogowanie.";
            broadcast_message_to_session(&sessions[i], notification, "SERVER");
            syslog(LOG_INFO,"UWAGA Zamknieto działanie serwera !");
        }
    }
    syslog(LOG_INFO, "Serwer zamyka się.");
    exit(0);
}