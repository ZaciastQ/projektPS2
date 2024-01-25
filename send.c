#include "client_func.h"
int main(int argc, char **argv) {
    ssize_t n;
    int sockfd;
    
    struct user_account currentUser;
    struct sockaddr_in servaddr;

    socklen_t len = sizeof(servaddr);
    time_t last_heartbeat = time(NULL);
    
    int roleChoice;
    char sendline[MAX_BUFF], recvline[MAX_BUFF];

    
    // Inicjalizacja struktury użytkownika
    memset(&currentUser, 0, sizeof(currentUser));
    
    // Tworzenie gniazda dla komunikacji z serwerem
    sockfd = socket(AF_INET, SOCK_DGRAM, 0);
    if (sockfd < 0) {
        perror("Błąd w tworzeniu gniazda");
        exit(EXIT_FAILURE);
    }

    // Konfiguracja adresu serwera
    memset(&servaddr, 0, sizeof(servaddr));
    servaddr.sin_family = AF_INET;
    servaddr.sin_port = htons(PORT);
    if (inet_pton(AF_INET, argv[1], &servaddr.sin_addr) <= 0) {
        perror("Błąd w inet_pton");
        exit(EXIT_FAILURE);
    }

    // Sprawdzenie poprawności argumentów
    if (argc != 2) {
        fprintf(stderr, "Użycie: %s <IPaddress>\n", argv[0]);
        exit(EXIT_FAILURE);
    }

  
    while(currentUser.is_logged_in==0){
        //rola
        printf("Wybierz rolę lub wyjdź:\n1. Prowadzący\n2. Uczestnik\n3. Wyjdź z aplikacji\nWybór: ");
        scanf("%d", &roleChoice);
        getchar();
        if (roleChoice == 1) {
            currentUser.is_instructor = 1; // Prowadzący/Instructor
        } else if (roleChoice == 2) {
            currentUser.is_instructor = 0;
        }else if (roleChoice == 3) {
            printf("Wyjście z aplikacji.\n");
            exit(EXIT_SUCCESS);
        }else {printf("coś nie tak");
        }

        // Wyslanie roli
        sprintf(sendline, "ROLE: %d", roleChoice);
        if (sendto(sockfd, sendline, strlen(sendline), 0, (const struct sockaddr *)&servaddr, sizeof(servaddr)) < 0) {
            perror("Błąd w wysyłaniu");
            exit(EXIT_FAILURE);
        }

        if ((n = recvfrom(sockfd, recvline, MAX_BUFF, 0, (struct sockaddr *)&servaddr, &len)) < 0) {
            perror("Błąd w odbieraniu odpowiedzi");
            exit(EXIT_FAILURE);
        }
        recvline[n] = '\0';
        printf("%s\n", recvline);
        
       
        // Wybor akcji
        int accountAction;
        printf("Wybierz akcję:\n1. Zarejestruj się\n2. Zaloguj się\n3. Wyjdź z aplikacji\nWybór: ");
        scanf("%d", &accountAction);
        getchar();

        memset(sendline, 0, sizeof(sendline));
        if (accountAction == 1) {
             // Rejestracja
            register_user(&currentUser);

            // Odpowiedz serwera
            if (strstr(recvline, "Potwierdzenie: zarejestrowano")) {
                printf("Rejestracja przebiegła pomyślnie.\n");
            } else if (strstr(recvline, "Error: Username")) {
                printf("Użytkownik o takiej nazwie już istnieje.\n");
                continue; //pozwolenie użytkownikowi na wybranie innej opcji
            }
        } else if (accountAction == 2) {
            // Login
            printf("Podaj swoja nazwe: ");
            scanf("%s", currentUser.username);
            getchar();
            printf("Wpisz prosze swoje haslo: ");
            scanf("%s", currentUser.password);
            getchar();
        } else if(accountAction == 3){
            printf("Wyjście z aplikacji.\n");
            exit(EXIT_SUCCESS);
        }
        else{
            printf("Niepoprawny wybór. Spróbuj ponownie.\n");
            continue;
        }   
        sprintf(sendline, "ACTION: %d | USERNAME: %s | PASSWORD: %s | SOCKFD: %d", accountAction, currentUser.username, currentUser.password, sockfd);
        
        // Wysylanie
        if (sendto(sockfd, sendline, strlen(sendline), 0, (const struct sockaddr *)&servaddr, sizeof(servaddr)) < 0) {
            perror("Błąd w wysyłaniu");
            exit(EXIT_FAILURE);
        }

        // poczekaj na odpowiedz
        if ((n = recvfrom(sockfd, recvline, MAX_BUFF, 0, (struct sockaddr *)&servaddr, &len)) < 0) {
            perror("Błąd w odbieraniu odpowiedzi");
            exit(EXIT_FAILURE);
        }
        recvline[n] = '\0';
        printf("%s\n", recvline);
        // sprawdz czy serwer sobie poradzil z logowaniem
        if (strstr(recvline, "Potwierdzenie: zalogowano")) {
            currentUser.is_logged_in = 1;
            printf("Zalogowano pomyślnie.\n");
            break;  //jesli tak zakoncz petle warunkową
        }
    }
    while( currentUser.is_logged_in == 1)  {        
        if (currentUser.is_instructor) {
            int session_active = 0;
            char send_message_command[MAX_BUFF];
            
            char buffer[1024];
            time_t last_heartbeat_time = 0;

            int flags = fcntl(sockfd, F_GETFL, 0);
            fcntl(sockfd, F_SETFL, flags | O_NONBLOCK);

            if (!session_active) {
                printf("Wybierz akcję: 1 - Utwórz sesję, 2 - Dołącz do sesji szkoleniowej, 3 - Wyjdź\nWybór: ");
                int action_choice;
                scanf("%d", &action_choice);
                getchar();

                switch (action_choice) {
                    case 1: // Utwórz sesję
                        get_training_id(&currentUser);
                        printf("Tworzenie nowej sesji szkoleniowej...\n");
                        sprintf(send_message_command, "CREATE_TRAINING %d", currentUser.training_id);
                        sendto(sockfd, send_message_command, strlen(send_message_command), 0, 
                            (const struct sockaddr *)&servaddr, sizeof(servaddr));
                            
                        if (receive_server_response_with_timeout(sockfd, &servaddr, recvline, MAX_BUFF, 5) == 0) {
                            printf("Server response: %s\n", recvline);

                            // Sprawdzanie, czy odpowiedź serwera wskazuje na sukces
                            if (strstr(recvline, "Szkolenie o numerze ID") && strstr(recvline, "jest utworzone")) {
                                    printf("Szkolenie zostało stworzone dzięki ID %d.\n", currentUser.training_id);   
                                } else {
                                    printf("Nie można przetworzyć: %s\n", recvline);
                                }
                            } else {
                                printf("Nie można odebrać wiadomości od serwera.\n");
                            }
                        break;
                    case 2: // Dołącz do sesji szkoleniowej
                        get_training_id(&currentUser);
                        sprintf(send_message_command, "JOIN_TRAINING %d", currentUser.training_id);
                        sendto(sockfd, send_message_command, strlen(send_message_command), 0, 
                            (const struct sockaddr *)&servaddr, sizeof(servaddr));
                            if (receive_server_response_with_timeout(sockfd, &servaddr, recvline, MAX_BUFF, 5) == 0) {
                            printf("Odpowiedź serwera: %s\n", recvline);

                                // sprawdz czy dolaczenie sie udalo
                                if (strstr(recvline, "Potwierdzenie: dołączyłeś do sesji")) {
                                    printf("Dołączono do sesji o ID %d...\n", currentUser.training_id);
                                    session_active = 1;
                                }else if (strstr(recvline, "CONFIRMATION: Joined session")) {
                                    int joined_session_id;
                                    sscanf(recvline, "CONFIRMATION: Joined session %d", &joined_session_id);
                                    printf("Dołączono do sesji o ID %d...\n", joined_session_id);
                                    session_active = 1; 
                                }else {
                                    printf("Nie można dołączyć do sesji: %s\n", recvline);
                                }
                            } else {
                                printf("Brak odpowiedzi serwera.\n");
                            }
                            
                            break;
                        case 3: // Wyjdź
                            printf("Exiting...\n");
                            return 0;
                    }
            } 
            if(session_active) {
                printf("Jesteś w sesji. Wybierz akcję: 1 - Wejdź do chatu sesji, 2 - Opuszczanie sesji\nWybór: ");
                int action_choice;
                scanf("%d", &action_choice);
                getchar();
                time_t current_time = time(NULL);
                if (difftime(current_time, last_heartbeat_time) >= 5) {  // Co 5 sekund
                    char heartbeat_msg[MAX_BUFF];
                    snprintf(heartbeat_msg, sizeof(heartbeat_msg), "HEARTBEAT %s", currentUser.username);
                    if (sendto(sockfd, heartbeat_msg, strlen(heartbeat_msg), 0, (const struct sockaddr *)&servaddr, sizeof(servaddr)) < 0) {
                        perror("Error sending HEARTBEAT message");
                    }
                    last_heartbeat_time = current_time;  // Zaktualizuj czas ostatniego HEARTBEAT
                }

                switch (action_choice) {
                    case 1: // Wejdź do chatu sesji
                        printf("Jesteś w chatcie sesji. Wpisz 'exit' aby wyjść.\n");
                        while (1) {
                            // Ustawienie deskryptorów dla select()
                            fd_set readfds;
                            FD_ZERO(&readfds);
                            FD_SET(sockfd, &readfds); // Dodaj socket do zestawu
                            FD_SET(STDIN_FILENO, &readfds); // Dodaj stdin do zestawu

                            struct timeval tv; // Ustawienie timeout
                            tv.tv_sec = 0;
                            tv.tv_usec = 500000; // 0.5 Sekundy

                            int activity = select(sockfd + 1, &readfds, NULL, NULL, &tv);

                            if ((activity < 0) && (errno != EINTR)) {
                                printf("Błąd w select\n");
                            }
                                
                            if (FD_ISSET(sockfd, &readfds)) { // Sprawdzenie aktywności na sockecie
                                ssize_t bytes_received = recv(sockfd, buffer, sizeof(buffer) - 1, 0);
                                if (bytes_received > 0) {
                                    buffer[bytes_received] = '\0'; 

                                    // Sprawdzenie, czy to powiadomienie od serwera
                                    if (strstr(buffer, "Notification:") && currentUser.is_instructor) {
                                        printf("%s\n", buffer);
                                    } else {
                                        // Wyświetl wiadomość dla wszystkich użytkowników
                                        printf("%s\n", buffer);
                                    }
                                } else if (bytes_received < 0 && errno != EWOULDBLOCK) {
                                    perror("Error receiving data");
                                }
                            }

                            if (FD_ISSET(STDIN_FILENO, &readfds)) { // Sprawdzenie aktywności na stdin
                                fgets(send_message_command, sizeof(send_message_command), stdin);
                                send_message_command[strcspn(send_message_command, "\n")] = 0;
                                if (strcmp(send_message_command, "exit") == 0) {
                                    break;
                                }

                                // Implementacja logiki wysyłania wiadomości
                                char message_to_send[MAX_BUFF + MAX_USERNAME + MAX_COMMAND_LENGTH + 20];
                                snprintf(message_to_send, sizeof(message_to_send), "SEND_MESSAGE %s %d %s", currentUser.username, currentUser.training_id, send_message_command);

                                // Wysyłanie komendy z wiadomością do serwera
                                if (sendto(sockfd, message_to_send, strlen(message_to_send), 0, (struct sockaddr *)&servaddr, sizeof(servaddr)) < 0) {
                                    perror("Error in sending message");
                                    continue; // Kontynuuj, mimo błędu
                                }
                            }
                            // Dodaj wysyłanie HEARTBEAT co 5 sekund
                            time_t current_time = time(NULL);
                            if (difftime(current_time, last_heartbeat_time) >= 5) {
                                char heartbeat_msg[MAX_BUFF];
                                snprintf(heartbeat_msg, sizeof(heartbeat_msg), "HEARTBEAT %s", currentUser.username);
                                if (sendto(sockfd, heartbeat_msg, strlen(heartbeat_msg), 0, (const struct sockaddr *)&servaddr, sizeof(servaddr)) < 0) {
                                    perror("Error sending HEARTBEAT message");
                                }
                                last_heartbeat_time = current_time;
                            }
                            
                        }
                        break;
                        case 2:  
                            sprintf(sendline, "LEAVE_SESSION %s %d", currentUser.username, currentUser.training_id);
                            if (sendto(sockfd, sendline, strlen(sendline), 0, (const struct sockaddr *)&servaddr, sizeof(servaddr)) < 0) {
                                perror("Błąd w wysyłaniu komunikatu o opuszczeniu sesji");
                                exit(EXIT_FAILURE);
                            }

                            printf("Opuszczono sesję o ID %d.\n", currentUser.training_id);
                            session_active = 0;
                            currentUser.is_logged_in = 0;
                            break;
                }
            }
             
        }
            
            
    if (!currentUser.is_instructor) {
        int session_active = 0;
        int session_id_to_join = -1;
        char send_message_command[MAX_BUFF];
        char buffer[1024];
        time_t last_heartbeat_time = time(NULL);

        fd_set readfds;
        struct timeval tv;

        int flags = fcntl(sockfd, F_GETFL, 0);
        fcntl(sockfd, F_SETFL, flags | O_NONBLOCK);

        int joined_session = 0;
        while(1) {
            if (!session_active) {
                printf("Wybierz akcję: 1 - Dołącz do sesji szkoleniowej, 2 - Wyjdź\nWybór: ");
                char action_choice_str[10];
                fgets(action_choice_str, sizeof(action_choice_str), stdin);
                int action_choice = atoi(action_choice_str);

                if (action_choice == 1 && !joined_session) {
                    get_training_id(&currentUser);
                    session_id_to_join = currentUser.training_id;
                    sprintf(send_message_command, "JOIN_TRAINING %d", session_id_to_join);
                    if (sendto(sockfd, send_message_command, strlen(send_message_command), 0, (const struct sockaddr *)&servaddr, sizeof(servaddr)) < 0) {
                        perror("Error in sending join session request");
                    }

                    if (receive_server_response_with_timeout(sockfd, &servaddr, buffer, MAX_BUFF, 5) == 0) {
                        printf("Odpowiedź serwera: %s\n", buffer);
                        if (strstr(buffer, "CONFIRMATION: Joined session")) {
                            printf("Dołączono do sesji ID %d...\n", session_id_to_join);
                            session_active = 1;
                            joined_session = 1; // Ustawienie flagi, że użytkownik dołączył do sesji
                            sprintf(send_message_command, "NOTIFY_INSTRUCTOR %d %s polaczyl", session_id_to_join, currentUser.username);
                            sendto(sockfd, send_message_command, strlen(send_message_command), 0, (const struct sockaddr *)&servaddr, sizeof(servaddr));
                        } else {
                            printf("Nie udało się połączyć do sesji: %s\n", buffer);
                        }
                    } else {
                        printf("Brak odpowiedzi serwera.\n");
                    }
                } else if (action_choice == 2) {
                    printf("Wyjście z sesji o ID %d...\n", session_id_to_join);
                    printf("Opuszczono sesję o ID %d.\n", currentUser.training_id);
                    session_active = 0;
                    currentUser.is_logged_in = 0;
                    joined_session = 0; // Reset flagi dołączenia do sesji
                    break;
                }
            } else {
                // Przekazanie flagi joined_session do funkcji
                handle_session_active_state(sockfd, &servaddr, &readfds, &tv, &currentUser, &session_active, &session_id_to_join, &last_heartbeat_time, &joined_session);
            }
        }
    }
}
return 0;
}
