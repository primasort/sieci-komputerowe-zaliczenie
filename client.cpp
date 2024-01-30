#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/ip.h>
#include <arpa/inet.h>
#include <thread>
#include <chrono>
#include <iostream>
#include <string> 
#include <vector>
#include <sys/epoll.h>

using namespace std;

bool game_started = false;
bool stop_game = false;
bool have_name = false;
bool is_it_end = false;
bool permission = false;
int round_counter = 0;
struct epoll_event events[32];
char chosen_letter;
int games_counter = 0;


void countdown(int time){
    for(int i = time; i > 0; i--){
        std::this_thread::sleep_for(std::chrono::seconds(1));
    }
}

string convertToString(char* a, int size){
    int i;
    std::string s = "";
    for (i = 0; i < size; i++) {
        s = s + a[i];
    }
    return s;
}

void game(int client_socket){
    char message[1024];  // Buffer for sending messages
    bool prompt_shown = false;  // Flaga do śledzenia, czy instrukcje zostały wyświetlone

    while(1){
        if (!game_started && !prompt_shown) {
            cout << "\nWpisz **start** aby rozpocząć grę, lub **end** aby wyjść. (tylko po nadaniu sobie nazwy)" << endl;
            prompt_shown = true; // Ustawienie flagi po wyświetleniu instrukcji
        }


        if (game_started == 1 && have_name == 1 && permission == 1)
        {
            cout << "!! Rozpoczęcie rundy !!" << endl;
            cout << "Wylosowana litera: " << chosen_letter << endl;

            for(int i = 0; i < 6; i++) {
                switch(i) {
                    case 0: cout << "Podaj propozycje hasła -> miasta: "; break;
                    case 1: cout << "Podaj propozycje hasła -> państwa: "; break;
                    case 2: cout << "Podaj propozycje hasła -> imiona: "; break;
                    case 3: cout << "Podaj propozycje hasła -> zwierzęta: "; break;
                    case 4: cout << "Podaj propozycje hasła -> kraina geograficzna: "; break;
                    case 5: cout << "Podaj propozycje hasła -> dyscyplina sportowa: "; break;
                }

                if(stop_game == true){
                    cout << "Skończył się czas" << endl;
                    if(games_counter == 2){
                        permission = false;
                        games_counter = 0;
                    }
                    games_counter++;
                    break;
                }   

                fgets(message, sizeof(message), stdin);
                if(strcmp(message, "**end**\n") == 0){
                    close(client_socket);
                    stop_game = true;
                    // funkcja zamyka całkowicie program
                    exit(0);
                }
                send(client_socket, message, strlen(message), 0);
            }
            cout << "\n runda zakończona" << endl;

            stop_game = false;  // Reset the flag for the next round
            game_started = false;  // Reset the flag for the next round
        }

        if (!game_started && have_name == true) {
            fgets(message, sizeof(message), stdin);
            if (strcmp(message, "**start**\n") == 0 && have_name == true) {
                send(client_socket, message, strlen(message), 0); 
                //game_started = true; // flaga występuje tutaj w celu debugowania bez połączenia z właściwym serwerem
                continue;
            } else if(strcmp(message, "**end**\n") == 0){
                close(client_socket);
                stop_game = true;
                exit(0);  // Exiting the function
            }
        }

    }
}


int main(int argc, char *argv[]){
    // gwiazdka oznacza, że wskaźnik wskazuje na adres pamięci, w którym znajduje się wartość zmiennej
    const char *default_ip = "127.0.0.1";
    char *ip = const_cast<char*>(default_ip);    // używamy gwiazdki, ponieważ funkcja socket() zwraca deskryptor gniazda, a nie jego adres
    int port = 8080; // Numer portu na którym serwer będzie nasłuchiwał

     // Sprawdzanie, czy podano IP i port
    if (argc > 1) {
        ip = argv[1];
        if (argc > 2) {
            port = atoi(argv[2]);
        }
    }

    int client_socket; // deskryptor dla gniazda klienta
    struct sockaddr_in server_addr; // struktura do przechowywania informacji o adresie serwera
    socklen_t server_addr_len; // zmienna przechowująca rozmiar struktury server_addr
    char buffer[1024]; // bufor do przechowywania danych wysyłanych i odbieranych przez klienta
    char message[1024];
    int n; // zmienna przechowująca liczbę bajtów wysłanych lub odebranych

    // Tworzenie gniazda klienta
    client_socket = socket(AF_INET, SOCK_STREAM, 0);
    if (client_socket < 0){
        perror("Nie można utworzyć gniazda klienta");
        exit(1);
    }
    else{
        printf("Gniazdo klienta utworzone\n");
    }

    // Inicjalizacja struktury klienta
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = inet_addr(ip);
    server_addr.sin_port = htons(port);

   // Nawiązywanie połączenia z serwerem
    if (connect(client_socket, (struct sockaddr *) &server_addr, sizeof(server_addr)) < 0) {
        perror("Błąd połączenia");
        exit(1);
    }

    printf("Połączono z serwerem");

    //-----------------------------------------------------------
    int epoll_fd = epoll_create1(0);
    if (epoll_fd == -1) {
        perror("epoll_create1");
        exit(EXIT_FAILURE);
    }

    struct epoll_event ev, events[10];
    ev.events = EPOLLIN;
    ev.data.fd = client_socket;

    epoll_ctl(epoll_fd, EPOLL_CTL_ADD, client_socket, &ev);
    //-----------------------------------------------------------

    std::thread first(game, client_socket);

    printf("Podaj swój nick: ");
    fgets(message, sizeof(message), stdin);
    message[strcspn(message, "\n")] = 0;  // Usuń znak nowej linii
    send(client_socket, message, strlen(message), 0);
    cout << "Oczekiwanie na rozpoczęcie gry..." << endl;

    while(1){
        // kiedy dostanie od serwera wiadomość o rozpoczęciu gry, to ustawia flagę game_started na true -> wiadomość **round_start**
        // kiedy dostanie wiadomość od serwera  że nazwa użytkownika jest zła to ustawia flagę have_name na false -> wiadomość **name_not_allowed**
        // kiedy wiadomośc będzie skłądała się z jednej litery to przypisuje treść wiadomości do zmiennej letter
        // kiedy nie rozpoznaje wiadomości to wypisuje ją na ekran

        int nfds = epoll_wait(epoll_fd, events, 10, 1000);
        if (nfds == -1) {
            perror("epoll_wait");
            exit(EXIT_FAILURE);
        }

        for (int i = 0; i < nfds; i++) {
            if (events[i].data.fd == client_socket) {
                memset(buffer, 0, sizeof(buffer));
                n = recv(client_socket, buffer, sizeof(buffer), 0);
                if (n <= 0) {
                    perror("recv");
                    exit(EXIT_FAILURE);
                }

                if (strncmp(buffer, "**round_start**", strlen("**round_start**")) == 0) {
                    game_started = true;
                    round_counter++;
                    cout << "\nRunda  się rozpoczeła" <<  endl;
                    //cout << "game_started = " << game_started << endl;
                    //cout << "have_name = " << have_name << endl;
                    cout << "Wciśnij enter aby kontynuować..." << endl;
                    continue;
                } 
                else if (strncmp(buffer + 1, "**round_start**", strlen("**round_start**")) == 0) {
                    chosen_letter = buffer[0];
                    game_started = true;
                    round_counter++;
                    if(round_counter > 2){
                        round_counter = 1;
                    }
                    cout << "\nRunda " << round_counter << endl;
                    continue;
                }

                if (strcmp(buffer, "**name_not_allowed**") == 0) {
                    cout << "Nazwa użytkownika jest niedostępna. Podaj inną nazwę: ";
                    fgets(message, sizeof(message), stdin);
                    message[strcspn(message, "\n")] = 0;  // Usuń znak nowej linii
                    send(client_socket, message, strlen(message), 0);
                    continue;
                }

                if (strcmp(buffer, "**ok**") == 0) {
                    cout << "Nazwa użytkownika została zaakceptowana." << endl;
                    have_name = true;
                    continue;
                }

                if (strcmp(buffer, "**round_end**") == 0) {
                    cout << "Serwer zakończył rundę." << endl;
                    is_it_end = true;
                    continue;
                }

                // jeżeli doszła wiadomośc **permission** ustaw flagę permission na true
                if (strcmp(buffer, "**permission**") == 0) {
                    permission = true;
                    continue;
                }

                if (strlen(buffer) == 1) {
                    chosen_letter = buffer[0];
                    continue;
                }

                cout << buffer << endl;
            }
        }
    }

    return 0;
}