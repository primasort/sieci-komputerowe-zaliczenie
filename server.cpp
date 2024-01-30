#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/ip.h>
#include <arpa/inet.h>
#include <pthread.h>
#include <signal.h>
#include <mutex>
#include <cstdlib>
#include <error.h>
#include <netdb.h>
#include <unordered_set>
#include <sys/epoll.h>
#include <iostream>
#include <chrono>
#include <ctime>
#include <fstream>
#include <algorithm>
#include <cctype>
#include <map>
#include <locale>
#include <codecvt>
#include <unordered_map>
#include <sys/epoll.h>
#include <thread>

using namespace std;

// Wiadomości specjalne: wszystkie wiadomości zaczynające się od "**" lub kończące się na "**" są wiadomościami specjalnymi


// tablice przechowujące słowa
string cities[832];
string countries[76];
string names[91];
string animals[93];
string krainy[390];
string dyscypliny[185];


// struktura klienta
struct Client{
    int socket;
    string name = "";
    int points = 0;
    string given[6];
    int next_given_word_index = 0;
};

// deklaracje zmiennych globalnych
int server_socket; // gniazdo serwera
unordered_set<int> clientFds; // zbiór deskryptorów klientów
mutex clientFdsLock; // mutex do synchronizacji dostępu do zbioru deskryptorów klientów
int number_of_clients = 0; // liczba klientów
int logged_clients_number = 0; // liczba zalogowanych klientów
Client clients_list[100]; // lista klientów
std::mutex gameStartMutex; // mutex do synchronizacji dostępu do zmiennej gameShouldStart
bool gameShouldStart = false; // Globalna flaga informująca czy gra powinna się rozpocząć
std::mutex client_mutex; // mutex do synchronizacji dostępu do danych klienta
int round_counter = 0; // licznik rund
int number_of_rounds = 2; // liczba rund
int check_result = 0; // zmienna sprawdzająca czy powinniśmy pominąć rundę
char letters[] = {'a', 'b', 'c', 'd', 'e', 'f', 'g', 'i', 'j',
                  'k', 'l', 'm', 'n', 'p', 'r', 's', 't', 'w'};
char chosen_letter; // wylosowana litera
bool accept_proposals = false; // flaga informująca czy można przyjmować propozycje haseł od klientów

// deklaracje funkcji
void load_data();
void forced_exit(int signal);
string to_lower_case(const string& input);
wstring to_lower_pl(const wstring& input);
wstring string_to_wstring(const string& str);
string wstring_to_string(const wstring& wstr);


// ****************************************************************************************************
// wczytywanie słów z plików
void load_data() {
    ifstream cities_file("data/miasta_832.txt");
    ifstream countries_file("data/panstwa_76.txt");
    ifstream names_file("data/imiona_91.txt");
    ifstream animals_file("data/zwierzeta_93.txt");
    ifstream krainy_file("data/krainy_390.txt");
    ifstream dyscypliny_file("data/dyscypliny_185.txt");

    string line;
    int i = 0;
    while (getline(cities_file, line)) {
        // przekształcenie nazw miast na małe litery
        line = to_lower_case(line);
        cities[i] = line;
        i++;
    }
    i = 0;
    while (getline(countries_file, line)) {
        line = to_lower_case(line);
        countries[i] = line;
        i++;
    }
    i = 0;
    while (getline(names_file, line)) {
        line = to_lower_case(line);
        names[i] = line;
        i++;
    }
    i = 0;
    while (getline(animals_file, line)) {
        line = to_lower_case(line);
        animals[i] = line;
        i++;
    }
    i = 0;
    while (getline(krainy_file, line)) {
        line = to_lower_case(line);
        krainy[i] = line;
        i++;
    }
    i = 0;
    while (getline(dyscypliny_file, line)) {
        line = to_lower_case(line);
        dyscypliny[i] = line;
        i++;
    }

    cities_file.close();
    countries_file.close();
    names_file.close();
    animals_file.close();
    krainy_file.close();
    dyscypliny_file.close();
}

// ****************************************************************************************************
// konwersja dużych liter na małe uwzględniając polskie znaki diakrytycznych

string to_lower_case(const string& input) {
    wstring wideInput = string_to_wstring(input);
    wstring wideResult = to_lower_pl(wideInput);
    return wstring_to_string(wideResult);
}

wstring to_lower_pl(const wstring& input) {
    wstring result;

    // Mapowanie polskich znaków diakrytycznych
    const unordered_map<wchar_t, wchar_t> specialChars = {
        {L'Ą', L'ą'}, {L'Ć', L'ć'}, {L'Ę', L'ę'}, {L'Ł', L'ł'},
        {L'Ń', L'ń'}, {L'Ó', L'ó'}, {L'Ś', L'ś'}, {L'Ź', L'ź'}, {L'Ż', L'ż'}
    };

    for (wchar_t c : input) {
        if (specialChars.find(c) != specialChars.end()) {
            result += specialChars.at(c);
        } else {
            result += towlower(c);
        }
    }

    return result;
}

std::string wstring_to_string(const std::wstring& wstr) {
    std::wstring_convert<std::codecvt_utf8_utf16<wchar_t>> converter;
    return converter.to_bytes(wstr);
}

std::wstring string_to_wstring(const std::string& str) {
    std::wstring_convert<std::codecvt_utf8_utf16<wchar_t>> converter;
    return converter.from_bytes(str);
}

// ****************************************************************************************************
// funkcja uruchamiana po naciśnięciu ctrl+c
// zamykamy gniazdo i kończymy program program
void forced_exit(int signal) {
    //zablokowanie mutexa na czas zamknięcia serwera
    unique_lock<mutex> lock(clientFdsLock);
    for (auto clientFd : clientFds) {
        shutdown(clientFd, SHUT_RDWR);
        close(clientFd);
    }
    cout << "Zamykanie serwera..." << endl;
    close(server_socket);
    exit(0);
}

// ****************************************************************************************************
// funkcja dodająca deskryptor do epoll
static void epoll_add(int epoll_fd, int fd, uint32_t events) {
    struct epoll_event event;
    event.events = events;
    event.data.fd = fd;
    if (epoll_ctl(epoll_fd, EPOLL_CTL_ADD, fd, &event) == -1) {
        perror("epoll_ctl");
        exit(EXIT_FAILURE);
    }
}

// ****************************************************************************************************
// dodawanie klienta
void add_client(int client_socket){
    clients_list[number_of_clients].socket = client_socket;
    number_of_clients++;
}

// ****************************************************************************************************
// usuwanie klienta
void remove_client(int client_socket){
    for(int i = 0; i < number_of_clients; i++){
        if(clients_list[i].socket == client_socket){
            // zerowanie w przypadku kiedy usuwamy ostatniego klienta z listy, którego nie można nadpisać kolejnym klientem
            clients_list[i].socket = 0;
            clients_list[i].name = "";
            clients_list[i].points = 0;
            for(int j = 0; j < 4; j++){
                clients_list[i].given[j] = "";
            }

            for(int j = i; j < number_of_clients - 1; j++){
                clients_list[j] = clients_list[j+1];
            }
            number_of_clients--;
            break;
        }
    }
}

// ****************************************************************************************************
// funkcja znajdująca klienta na liście klientów
int find_client(int client_socket){
    for(int i = 0; i < number_of_clients; i++){
        if(clients_list[i].socket == client_socket){
            return i;
        }
    }
    return -1;
}

// ****************************************************************************************************
// funkcja realizująca odliczanie
void counting_down_and_check(int seconds){
    if(logged_clients_number < 1){
        // zakończ rozgrywkę jeżeli liczba klientów jest mniejsza niż 1
        check_result = -1;
        cout << "Za mało graczy do kontynuowania gry" << endl;
        cout << "!!! PRZERYWANIE GRY !!!" << endl;
    }

    std::this_thread::sleep_for(std::chrono::seconds(seconds));
}

// ****************************************************************************************************

char letter_random(){
    srand(time(NULL));
    int random_index = rand() % 20;
    cout << "Wylosowana litera: " << letters[random_index] << endl;

    return letters[random_index];
}

void remove_newline(string& str) {
    str.erase(remove(str.begin(), str.end(), '\n'), str.end());
}


// ****************************************************************************************************
// dodawanie punktów po każdej rundzie
void count_points(){
    // zmiejszamy litery w propozycjach haseł do małych
    for(int i = 0; i < number_of_clients; i++){
        for(int j = 0; j < 6; j++){
            clients_list[i].given[j] = to_lower_case(clients_list[i].given[j]);
            remove_newline(clients_list[i].given[j]);
        }
    }

    // wyświetlamy propozycje haseł dla klientów
    for(int i = 0; i < number_of_clients; i++){
        cout << "Klient " << clients_list[i].name << " propozycje haseł: ";
        for(int j = 0; j < 6; j++){
            cout << clients_list[i].given[j] << " ";
        }
        cout << endl;
    }


    // sprawdzamy czy propozycje haseł pokrywają się z tymi pobranymi z plików
    // jeżeli tak to dodajemy punkty
    for(int i = 0; i < number_of_clients; i++){
        if(find(cities, cities + 832, clients_list[i].given[0]) != cities + 832){
            cout << "Klient " << clients_list[i].name << " dostał punkt za miasto" << endl;
            clients_list[i].points++;
        }
        if(find(countries, countries + 76, clients_list[i].given[1]) != countries + 76){
            cout << "Klient " << clients_list[i].name << " dostał punkt za państwo" << endl;
            clients_list[i].points++;
        }
        if(find(names, names + 91, clients_list[i].given[2]) != names + 91){
            cout << "Klient " << clients_list[i].name << " dostał punkt za imię" << endl;
            clients_list[i].points++;
        }
        if(find(animals, animals + 93, clients_list[i].given[3]) != animals + 93){
            cout << "Klient " << clients_list[i].name << " dostał punkt za zwierzę" << endl;
            clients_list[i].points++;
        }
        if(find(krainy, krainy + 390, clients_list[i].given[4]) != krainy + 390){
            cout << "Klient " << clients_list[i].name << " dostał punkt za krainę" << endl;
            clients_list[i].points++;
        }
        if(find(dyscypliny, dyscypliny + 1, clients_list[i].given[5]) != dyscypliny + 185){
            cout << "Klient " << clients_list[i].name << " dostał punkt za dyscyplinę" << endl;
            clients_list[i].points++;
        }
    }
}

// ****************************************************************************************************
// rozsyłanie wyników do aktywnych graczy i wyznaczanie zwyciężcy
void choose_winner(){
    char buffer[1024];
    
    // Wysyłanie wyników do wszystkich aktywnych klientów
    for(int i = 0; i < number_of_clients; i++){
        sprintf(buffer, "koniec rozgrywki!");
        send(clients_list[i].socket, buffer, strlen(buffer), 0);
    }
    
    // Wyszukiwanie zwycięzcy
    int maxPoints = 0;
    int winnerIndex = -1;
    for(int i = 0; i < number_of_clients; i++){
        if(clients_list[i].points > maxPoints){
            maxPoints = clients_list[i].points;
            winnerIndex = i;
        }
    }
    
    // Wysyłanie wiadomości o zwycięzcy
    if(winnerIndex != -1){
        sprintf(buffer, "Wygrałeś! Twój wynik: %d", clients_list[winnerIndex].points);
        send(clients_list[winnerIndex].socket, buffer, strlen(buffer), 0);
    }
}

// ****************************************************************************************************
void send_to_all(string message){
    for(int i = 0; i < number_of_clients; i++){
        cout << "Wysylanie do klienta: " << clients_list[i].socket << " wiadomosc: " << message << endl;
        send(clients_list[i].socket, message.c_str(), message.length(), 0);
    }
}

// ****************************************************************************************************
// funkcja realizujaca główną logikę gry
void game(){
    int a = 0;
    char buffer[1024];
    string message;

    while(true){
        if(gameShouldStart == true){
            if(logged_clients_number > 1){
                // wyślij do klientów pozwolenia na rozpoczęcie gry
                message = "**permission**";
                send_to_all(message);
                for(int x = 0; x < number_of_rounds; x++){
                    counting_down_and_check(5); // sprawdzamy czy liczba klientów jest większa niż 1
                    if(check_result != -1){ // jeżeli mamy wystarczająco dużo graczy, to kontynuujemy
                        cout << "Runda " << x + 1 << endl;
                        chosen_letter = letter_random(); // losujemy literę
                        // wysyłamy klientom informację o rozpoczęciu rundy i wylosowanej literze

                        //*******************************
                        message = chosen_letter;
                        send_to_all(message);
                        message = "**round_start**";
                        send_to_all(message);

                        //*******************************
                        // ustawiamy flagę nasłuchoiwania propozycji haseł na true
                        accept_proposals = true;

                        counting_down_and_check(30); // czekamy 10 sekund na propozycje haseł od klientów
                    }
                    // po okresie oczekiwania przerywamy nasłuchiwanie i podliczamy punkty
                    accept_proposals = false;
                    //clients_list[find_client(events[i].data.fd)].next_given_word_index = 0;

                    // wysyłamy klientom informację o zakończeniu rundy
                    message = "**round_end**";
                    send_to_all(message);
                    count_points();

                    for(int i = 0; i < number_of_clients; i++){
                        for(int j = 0; j < 6; j++){
                            clients_list[i].given[j] = "";
                        }
                    }
                }
                // zakończenie gry
                cout << "!!! KONIEC GRY !!!" << endl;
                gameStartMutex.lock();
                gameShouldStart = false;
                gameStartMutex.unlock();
                choose_winner();
            }
            else{
                cout << "Za mało graczy do rozpoczęcia gry" << endl;
                gameStartMutex.lock();
                gameShouldStart = false;
                gameStartMutex.unlock();
            }
        }   
    }
}

// ****************************************************************************************************
bool is_name_allowed(string name){
    // sprawdzamy czy imię jest unikalne, czy nie jest puste i czy nie zawiera wiadomości specjalnych
    if(name == "" || name.find("**") != string::npos){
        return false;
    }
    for(int i = 0; i < number_of_clients; i++){
        if(clients_list[i].name == name){
            return false;
        }
    }
    return true;
}


int main(int argc, char *argv[]){
    // wczytywanie słów z plików
    load_data();
    number_of_rounds = 2;

    // domyślne porty i adresy ip
    char *ip; // adres ip na którym serwer będzie nasłuchiwał 
    int port = 8080; // Numer portu na którym serwer będzie nasłuchiwał

    int server_socket, client_socket; // deskryptory dla gniazd serwera i klienta 
    struct sockaddr_in server_addr, client_addr; // struktury do przechowywania informacji o adresach serwera i klienta
    socklen_t client_addr_len; // zmienna przechowująca rozmiar struktury client_addr

    if (argc > 1) {
        ip = argv[1];
        if (argc > 2) {
            port = atoi(argv[2]);
        }
    } else {
        ip = strdup("127.0.0.1"); // Używamy strdup, aby skopiować stringa do nowej zmiennej, ponieważ argv[0] jest zmienną typu const char*
    }
    // utworzenie gniazda
    server_socket = socket(AF_INET, SOCK_STREAM, 0);

    // obsługa nieoczekiwanego zamknięcia - wywołanie funkcji po naciśnięciu ctrl+c
    signal(SIGINT, forced_exit);
    signal(SIGPIPE, SIG_IGN);

    // ustawienie opcji gniazda, tak aby było omżna ponownie użyć portu po zamknięciu serwera
    int opt = 1;
    setsockopt(server_socket, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

   // Sprawdzenie czy gniazdo zostało poprawnie utworzone
   if(server_socket < 0){
       perror("Nie można utworzyć gniazda serwera");
       exit(1);
   }
   else{
       printf("Gniazdo serwera utworzone\n");
   }

    // ustawienie adresu serwera
    memset(&server_addr, 0, sizeof(server_addr)); // wyzerowanie struktury
    server_addr.sin_family = AF_INET; // ustawienie rodziny adresów
    server_addr.sin_addr.s_addr = inet_addr(ip); // ustawienie adresu ip
    // htons - funkcja konwertująca liczbę zapisaną w formacie hosta na liczbę zapisaną w formacie sieciowym  
    server_addr.sin_port = htons(port); // ustawienie numeru portu

    // przypisanie adresu do gniazda
    int bind_result = bind(server_socket, (struct sockaddr*)&server_addr, sizeof(server_addr));

    if(bind_result < 0){
        perror("Nie można przypisać adresu do gniazda");
        exit(1);
    }
    else{
        printf("Przypisano do gniazda serwera adres %s i numer portu %d\n", ip, port);
    }

    // ustawienie gniazda w tryb nasłuchiwania
    listen(server_socket, 5);

    if(listen(server_socket, 5) < 0){
        perror("Nie można ustawić gniazda w tryb nasłuchiwania");
        exit(1);
    }
    else{
        printf("Gniazdo serwera w trybie nasłuchiwania\n");
    }

    // tworzymy deskryptor dla epoll, będziemy go używać do obsługi zdarzeń na gnieździe serwera
    int epoll_fd = epoll_create1(0); // epoll_create1(0) - tworzy nową instancję epoll, 
    if (epoll_fd == -1) {
        perror("epoll_create1");
        exit(EXIT_FAILURE);
    }

    // dodajemy deskryptor gniazda serwera do epoll
    struct epoll_event events[32]; // tablica do przechowywania zdarzeń epoll (maksymalnie 32)
    epoll_add(epoll_fd, server_socket, EPOLLIN | EPOLLET| EPOLLOUT);

    /*
        EPOLLIN - zdarzenie odczytu
        EPOLLET - tryb krawędziowy - zdarzenia są zgłaszane tylko przy zmianie stanu deskryptora
        EPOLLOUT - zdarzenia są zgłaszane tylko przy zmianie stanu deskryptora
    */

    // tworzymy nowy wątek, który uruchaamia funkcję game
    // działa on w tle i nie blokuje działania serwera
    std::thread first (game);

    // główna pętla programu
    while(true){
        int n = epoll_wait(epoll_fd, events, 32, -1); // epoll_wait - czeka na zdarzenia epoll
        for (int i=0; i<n; i++) {
            if (events[i].data.fd == server_socket) { // jeśli zdarzenie dotyczy gniazda serwera
                // i biorac poprzednie ustawienia, oznacza to nadchodzące nowe połączenie.
                
                client_addr_len = sizeof(client_addr);
                client_socket = accept(server_socket, (struct sockaddr*)&client_addr, &client_addr_len);
                if (client_socket == -1) {
                    perror("accept");
                    exit(EXIT_FAILURE);
                }

                // dodajemy nowy deskryptor klienta do epoll
                epoll_add(epoll_fd, client_socket, EPOLLIN | EPOLLET | EPOLLOUT);
                // dodajemy deskryptor klienta do zbioru deskryptorów klientów
                clientFdsLock.lock();
                add_client(client_socket);
                clientFdsLock.unlock();

                // wysyłamy wiadomość powitalną do klienta
                //string welcomeMessage = "Witaj w grze Państwa Miasta!\n";
                //send(client_socket, welcomeMessage.c_str(), welcomeMessage.length(), 0);
                cout << "Nowe połączenie: " << client_socket << endl;
             } 
            
            else if(events[i].events & (EPOLLHUP | EPOLLRDHUP)){ // jeżeli zdarzenie dotyczy rozłączenia klienta
                cout << "Klient rozłączony: " << events[i].data.fd << endl;
                // usuwamy deskryptor klienta z epoll
                epoll_ctl(epoll_fd, EPOLL_CTL_DEL, events[i].data.fd, NULL);
                // usuwamy deskryptor klienta ze zbioru deskryptorów klientów
                close(events[i].data.fd);
                clientFdsLock.lock();
                remove_client(events[i].data.fd);
                clientFdsLock.unlock();
                logged_clients_number--;
                continue; // continue ponieważ nie chcemy sprawdzać zdarzeń na deskryptorze klienta, który został usunięty
            }
            else if(events[i].events & EPOLLIN){
                // odczytujemy wiadomość od klienta
                char buffer[1024];
                memset(buffer, 0, sizeof(buffer));
                int read_result = read(events[i].data.fd, buffer, 1024);

                cout << "Odebrano wiadomość od klienta " << events[i].data.fd << ": " << buffer << endl;

                if(clients_list[find_client(events[i].data.fd)].name == ""){
                    // jeśli klient nie ma jeszcze ustawionego imienia to je sobie ustawia
                    string name = buffer;

                    // sprawdzamy czy imię jest unikalne, czy nie jest puste i czy nie zawiera wiadomości specjalnych
                    if(is_name_allowed(name) == false){
                        string message = "**name_not_allowed**";
                        cout << "Nowy klient próbował ustawić imię, ale jest ono nieprawidłowe" << endl;
                        send(events[i].data.fd, message.c_str(), message.length(), 0);
                        cout << "Wysłano wiadomość do klienta " << events[i].data.fd << ": " << message << endl;
                        continue; // Kontynuuj pętlę, oczekując na nową odpowiedź od klienta
                    }
                    else{
                        cout << "Nowy klient ustawił imię: " << name << "i zostało to zaakceptowane" << endl;
                        clients_list[find_client(events[i].data.fd)].name = name;
                        logged_clients_number++;
                        // wysyła wiadomość do klienta **ok** jeżeli imię jest poprawne
                        string message = "**ok**";
                        send(events[i].data.fd, message.c_str(), message.length(), 0);
                        cout << "Klient " << name << " zalogowany" << endl;
                    }
                }

                if(clients_list[find_client(events[i].data.fd)].name != "" && gameShouldStart == false){
                    // jeśli klient ma już ustawione imię i gra się jeszcze nie rozpoczęła to ustawiamy propozycje haseł dla klienta
                    string message = buffer;
                    cout << "Klient " << clients_list[find_client(events[i].data.fd)].name << " wysłał wiadomość: " << message << endl;

                    if(message == "**start**" || message == "**START**" || message == "**start**\n"){
                        // jeżeli klient wysłał wiadomość "**start**" to ustawiamy flagę gameShouldStart na true
                        gameStartMutex.lock();
                        gameShouldStart = true;
                        message = "";
                        gameStartMutex.unlock();
                    }
                }

                if(accept_proposals == true && clients_list[find_client(events[i].data.fd)].name != ""){
                    // odbieramy wiadomości od klientów i zapisujemy je w tablicy clients_list[i].given
                    client_mutex.lock();
                    cout << "Klient " << clients_list[find_client(events[i].data.fd)].name << " wysłał wiadomość: " << buffer << endl;
                    cout << "Zapisuję tą wiadomość do tablicy clients_list[" << find_client(events[i].data.fd) << "].given[" << clients_list[find_client(events[i].data.fd)].next_given_word_index << "]" << endl;
                    clients_list[find_client(events[i].data.fd)].given[clients_list[find_client(events[i].data.fd)].next_given_word_index] = buffer;
                    if(clients_list[find_client(events[i].data.fd)].next_given_word_index == 5){
                        clients_list[find_client(events[i].data.fd)].next_given_word_index = 0;
                    }
                    else{
                        clients_list[find_client(events[i].data.fd)].next_given_word_index++;
                    }
                    client_mutex.unlock();
                }
            }
        }
    }

    return 0;
}