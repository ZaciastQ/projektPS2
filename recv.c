#include "func.h"

int main(int argc,char **argv) {
    int sockfd,activity;
    char  buffer[MAX_BUFF];
    struct sockaddr_in servaddr, cliaddr;
    int i;
    
    fd_set rdset;
    ssize_t n;
    socklen_t len;
    const int on =1 ;
    initialize_active_users();
    initialize_session_clients();
    
    if ((sockfd = socket(AF_INET, SOCK_DGRAM, 0)) < 0) {
        syslog(LOG_ERR, "socket failed");
        perror("socket failed");
        exit(EXIT_FAILURE);
    }
    printf("Utworzono gniazdo UDP pomyślnie.\n");
    int reuse = 1;
    if (setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse)) < 0) {
        perror("setsockopt(SO_REUSEADDR) failed");
        exit(EXIT_FAILURE);
    }
    memset(&servaddr, 0, sizeof(servaddr));
    memset(&cliaddr, 0, sizeof(cliaddr));
    servaddr.sin_family = AF_INET;
    servaddr.sin_addr.s_addr = htonl(INADDR_ANY);
    servaddr.sin_port = htons(PORT);

    // Wiązanie gniazda
    if (bind(sockfd, (const struct sockaddr *)&servaddr, sizeof(servaddr)) < 0) {
        perror("Błąd w bind");
        exit(EXIT_FAILURE);
    }
    syslog(LOG_INFO, "Serwer UDP socket bind na porcie %d utworzony .\n", PORT);
    printf("Serwer UDP socket bind na porcie %d utworzony .\n", PORT);
    //zabijanie socketów zombie
    signal(SIGCHLD,sig_chld);
    
    int max_fd;
    
    // Rejestrowanie obsługi sygnału
    signal(SIGINT, shutdown_handler);  // Obsługa Ctrl+C
    signal(SIGTERM, shutdown_handler); // Obsługa sygnału zakończenia od systemu

    for (;;) {
        check_heartbeats(sockfd);
        FD_ZERO(&rdset);
        FD_SET(sockfd, &rdset);

        // monitorowanie wejscia 
        int activity = select(sockfd + 1, &rdset, NULL, NULL, NULL);
        if ((activity < 0) && (errno != EINTR)) {
            printf("Błąd w select\n");
            continue; // Continue 
        }
        
        struct user_account currentUser;
        char response[MAX_BUFF];

        // sprawdzanie czy są dane od klienta
        if (FD_ISSET(sockfd, &rdset)) {
            len = sizeof(cliaddr);
            n = recvfrom(sockfd, (char *)buffer, MAX_BUFF, 
                        MSG_WAITALL, (struct sockaddr *) &cliaddr,
                        &len);
            buffer[n] = '\0';
            printf("Odebrano: %s\n", buffer);
            int role, action;
            char username[MAX_USERNAME], password[MAX_PASSWORD];

            // Role
            if (sscanf(buffer, "ROLE: %d", &role) == 1) {
                currentUser.is_instructor = (role == 1) ? 1 : 0; 
                sprintf(response, "Potwierdzenie: Wybrano rolę %d na serwerze", role);
                send_response(sockfd, &cliaddr, len, response);
            }
            else if (sscanf(buffer, "ACTION: %d | USERNAME: %s | PASSWORD: %s | SOCKFD: %d", &action, username, password, &sockfd) == 4) {
                strcpy(currentUser.username, username);
                strcpy(currentUser.password, password);

                if (action == 1) {
                    int register_status=register_user_on_server(&currentUser);
                    if (register_status == 1) {
                        // uzytkownik istnieje
                        sprintf(response, "Error: uzytkownik %s juz istnieje.", currentUser.username);
                    } else if (register_status == 2) {
                        // File error
                        sprintf(response, "Error: Nie mozna otworzyc pliku.");
                    } else {
                        // Rejestracja pomyslna
                        sprintf(response, "Potwierdzenie: zarejestrowano %s na serwerze", currentUser.username);
                    }
                    send_response(sockfd, &cliaddr, len, response);
                }
                else if (action == 2) {
                    if (login_user_on_server(&currentUser,sockfd)) {
                        // znajdz wolny slot
                        int user_found = 0;
                        for (int i = 0; i < MAX_CLIENTS; i++) {
                            // Uaktualizacja klienta
                            if (strcmp(active_users[i].username, currentUser.username) == 0) {
                                active_users[i].is_logged_in = 1;
                                active_users[i].is_instructor = is_instructor(currentUser.username);
                    
                                memcpy(&active_users[i].address, &cliaddr, sizeof(cliaddr));
                                user_found = 1;
                                break;
                            }
                        }
                        //jesli nie ma klienta znajdz slot i dodaj
                        if (!user_found) {
                            for (int i = 0; i < MAX_CLIENTS; i++) {
                                if (active_users[i].is_logged_in == 0) {
                                    strcpy(active_users[i].username, currentUser.username);
                                    strcpy(active_users[i].password, currentUser.password);
                                    active_users[i].is_logged_in = 1;
                                    active_users[i].is_instructor = is_instructor(currentUser.username);
                                    memcpy(&active_users[i].address, &cliaddr, sizeof(cliaddr));
                                    user_found = 1;
                                    break;
                                }
                            }
                        }

                        if (user_found) {
                            printf("Użytkownik %s zalogowany pomyślnie.\n", currentUser.username);
                            if(currentUser.is_instructor) {
                                sprintf(response, "Potwierdzenie: zalogowano prowadzącego %s na serwerze", currentUser.username);
                            } else {
                                sprintf(response, "Potwierdzenie: zalogowano uczestnika %s na serwerze", currentUser.username);
                            }
                            send_response(sockfd, &cliaddr, len, response);
                        } else {
                            printf("Nie można zalogować i zaaktualizować tablicy dla  %s. Brak miejsca dla niego w tablicy.\n", currentUser.username);
                            sprintf(response, "Error: Nie można zalogować użytkownika %s, brak miejsca", currentUser.username);
                            send_response(sockfd, &cliaddr, len, response);
                        }
                    } else {
                        sprintf(response, "Error: Niepoprawne dane logowania dla %s", currentUser.username);
                        send_response(sockfd, &cliaddr, len, response);
                    }
                }
                else {
                    printf("Nieznana akcja\n");
                }
            }else if (sscanf(buffer, "CREATE_TRAINING %d", &currentUser.training_id) == 1) {
                int session_id = handle_create_session(&currentUser); // currentUser to zalogowany użytkownik
                if (session_id >= 0) {
                    sprintf(response, "Szkolenie o numerze ID %d jest utworzone", session_id);
                } else {
                    sprintf(response, "Błąd: Nie można utworzyć szkolenia o ID %d", session_id);
                }
                send_response(sockfd, &cliaddr, len, response);
            
            }else if (sscanf(buffer, "JOIN_TRAINING %d", &currentUser.training_id) == 1) {
                syslog(LOG_INFO,"Odebrano JOIN_TRAINING dla sesji o ID %d\n", currentUser.training_id);
                int found = 0;
                for (int i = 0; i < MAX_SESSIONS; i++) {
                    syslog(LOG_INFO,"Sprawdzanie %d (active: %d)\n", sessions[i].training_id, sessions[i].active);
                    if (sessions[i].training_id == currentUser.training_id && sessions[i].active) {
                        syslog(LOG_INFO,"Znaleziono aktywna sesje %d\n", currentUser.training_id);
                        found = 1;

                        // Tworzenie obiektu dla klienta
                        struct session_client new_client;
                        new_client.user = currentUser;
                        new_client.session_id = currentUser.training_id;

                        // Dołaczanie klienta do sesji
                        int add_status = add_client_to_session(&new_client, sessions, MAX_SESSIONS);
                        syslog(LOG_INFO,"Proba dodania klienta do sesji, add_status: %d\n", add_status);

                        if (add_status == 0) {
                            // Update active_users array to reflect the current user's session join
                            int user_updated = 0;

                            for (int j = 0; j < MAX_CLIENTS; j++) {
                                if (strcmp(active_users[j].username, currentUser.username) == 0) {
                                    // Update the user's training_id and login status
                                    active_users[j].training_id = currentUser.training_id;
                                    active_users[j].is_logged_in = 1;
                                    active_users[j].check_heartbeat = 1;
                                    active_users[j].last_seen = time(NULL);
                                    // 
                                    active_users[j].is_instructor = is_instructor(currentUser.username);
                                    syslog(LOG_INFO," Zaaktualizowano uzytwkonika %s w sesji o ID %d , instructor status %d\n",
                                        currentUser.username, currentUser.training_id, active_users[j].is_instructor);
                                    user_updated = 1;
                                    break;
                                }
                            }
                            if (!user_updated) {
                                syslog(LOG_INFO,"Nie udalo sie zaaktualizowac uzytkownika %s w active_users array\n", currentUser.username);
                            }

                            // Send confirmation response to the participant
                            char participant_response[MAX_BUFF];
                            sprintf(participant_response, "CONFIRMATION: Joined session %d", currentUser.training_id);
                            send_response(sockfd, &cliaddr, len, participant_response);
                            syslog(LOG_INFO," Wyslano potwierdzenie %s o dolaczaniu do sesji %d\n", currentUser.username, currentUser.training_id);
                        } else if (add_status == -1) {
                            send_response(sockfd, &cliaddr, len, "Sesja jest pełna.");
                        } else if (add_status == -2) {
                            send_response(sockfd, &cliaddr, len, "Sesja szkoleniowa nie znaleziona.");
                        } else if (add_status == -3) {
                            send_response(sockfd, &cliaddr, len, "Użytkownik nie jest zalogowany.");
                        } else if (add_status == -4) {
                            send_response(sockfd, &cliaddr, len, "Użytkownik jest już w sesji.");
                        } else {
                            send_response(sockfd, &cliaddr, len, "Nie można dodać użytkownika do sesji.");
                        }

                        break; 
                    }
                }

                if (!found) {
                    send_response(sockfd, &cliaddr, len, "Sesja szkoleniowa nie znaleziona.");
                }
            }else if (strncmp(buffer, "SEND_MESSAGE", 12) == 0) {
                char username[MAX_USERNAME];
                int session_id;
                char message_content[MAX_BUFF];
                
                // Parse the incoming buffer for username, session ID, and message content
                if (sscanf(buffer, "SEND_MESSAGE %s %d %[^\t\n]", username, &session_id, message_content) == 3) {
                    struct training_session* session = find_session_by_id(session_id);
                    
                   
                    printf("Sesja %d zawiera uzytkownikow:\n", session_id);
                    for (int i = 0; i < session->num_participants; i++) {
                        printf("Participant %d: %s\n", i+1, session->participants[i]);
                    }
                    
                    if (session && is_client_in_session(username, session) && is_session_active(session)) {
                        char full_message[MAX_BUFF + MAX_USERNAME + 3];
                        snprintf(full_message, sizeof(full_message), "%s: %s", username, message_content);
                        broadcast_message_to_session(session, full_message, username);
                    } else {
                        printf("%s nie jest w sesji %d lub sesja jest nieaktywna.\n", username, session_id);
                    }
                } else {
                    printf("Zly format wiadomosci odebranej: %s\n", buffer);
                }
            }else if (strncmp(buffer, "NOTIFY_INSTRUCTOR", 17) == 0) {
                int session_id;
                char participantName[MAX_USERNAME];
                char eventType[20];  

                if (sscanf(buffer, "NOTIFY_INSTRUCTOR %d %s %s", &session_id, participantName, eventType) == 3) {
                    participantName[MAX_USERNAME - 1] = '\0';
                    eventType[19] = '\0';


                    if (sessions[session_id].active && !currentUser.is_instructor) {
                        //dodaj czas do powiadomienia
                        char time_buffer[30];
                        time_t now = time(NULL);
                        struct tm *tm_struct = localtime(&now);
                        strftime(time_buffer, sizeof(time_buffer), "%Y-%m-%d %H:%M:%S", tm_struct);

                        // wyślij
                        char notification[MAX_BUFF * 2];
                        snprintf(notification, sizeof(notification), "Notification: %s %s sie z sesja o %s", participantName, eventType, time_buffer);

                        notify_instructor(session_id, notification, sockfd);
                    } else {
                        //debug
                        printf(" User '%s', Instructor status: %d, Sesja %d active status: %d\n", participantName, currentUser.is_instructor, session_id, sessions[session_id].active);
                    }
                }
            }else if (strncmp(buffer, "HEARTBEAT", 9) == 0) {
                char username[MAX_USERNAME];
                if (sscanf(buffer, "HEARTBEAT %s", username) == 1) {
                    update_user_last_seen(username);
                }
            }else if (strncmp(buffer, "LEAVE_SESSION", 13) == 0) {
                char username[MAX_USERNAME];
                int session_id;
                if (sscanf(buffer, "LEAVE_SESSION %s %d", username, &session_id) == 2) {
                    int result = remove_client_from_session(username, session_id,sockfd);
                    if (result == 0) {
                        active_users[i].check_heartbeat = 0;
                    }
                }
            }else {
                printf("Odebrano: %s\n", buffer);
                printf("Niepoprawny format danych\n");
            }
        }
    }
    return 0;
}
    
        
