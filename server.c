#include <sys/types.h>
#include <sys/wait.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <netdb.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <pthread.h>

#define QUEUE_SIZE 5

#define NUMBER_OF_USERS 32
#define NUMBER_OF_CHATROOMS 8
#define NUMBER_OF_MESSAGES 64

#define NAME_LENGTH 32
#define PASSWORD_LENGTH 32
#define MESSAGE_LENGTH 256

#define RESPONSE_LENGTH 262144

pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t mutex_users = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t mutex_chatrooms = PTHREAD_MUTEX_INITIALIZER;

// user ID to miejsce użytkownika na liście
struct user_t {
    int socket;
    char name[NAME_LENGTH];
	int roomId;
};

// chatroom ID to miejsce chatroomu na liście
struct chatroom_t {
    char name[NAME_LENGTH];
    char password[PASSWORD_LENGTH];
    int users[NUMBER_OF_USERS];
    char messages[NUMBER_OF_MESSAGES][MESSAGE_LENGTH];
};

struct thread_data_t {
    int socket;
    int user_counter; // równe ID użytkownika
    char incoming_message[1024];
    int bytes_read;
};

struct user_t users[NUMBER_OF_USERS];
struct chatroom_t chatrooms[NUMBER_OF_CHATROOMS];

char server_response[262144];

void show(){
	for(int i = 0; i < NUMBER_OF_CHATROOMS; i++){
	}
}

// tworzy w tablicy server_response[] string wysyłany do klienta
int update_server_response(int roomId) {
    memset(server_response, 0, sizeof(server_response));
    server_response[0] = '#';
    int cc = 1;
    if(roomId != -1) {
		int i = roomId;
        if (chatrooms[i].name[0] != 0) {

            // @nazwa chatroomu pod indeksem [i]
            server_response[cc] = '@';
            cc++;
            for (int j=0; j<NAME_LENGTH; j++) {
                if (chatrooms[i].name[j] == 0) break;
                else {
                    server_response[cc] = chatrooms[i].name[j];
                    cc++;
                }
            }

            // ^hasło do chatroomu
            server_response[cc] = '^';
            cc++;
            for (int j=0; j<PASSWORD_LENGTH; j++) {
                if (chatrooms[i].password[j] == 0) break;
                else {
                    server_response[cc] = chatrooms[i].password[j];
                    cc++;
                }
            }

            // %użytkownicy w chatroomie (nazwy, nie id), oddzieleni średnikiem
            server_response[cc] = '%';
            cc++;
            for (int j=0; j<NUMBER_OF_USERS; j++) {
                int user_id = chatrooms[i].users[j];
                if (user_id > -1) {
                    for (int k=0; k<NAME_LENGTH; k++) {
                        if (users[user_id].name[k] == 0) break;
                        else {
                            server_response[cc] = users[user_id].name[k];
                            cc++;
                        }
                    }
                    server_response[cc] = ';';
                    cc++;
                }
            }

            // wiadomości:
            server_response[cc-1] = '%';

            for (int j=0; j<NUMBER_OF_MESSAGES; j++) {
                if (chatrooms[i].messages[j][0] != 0) {
                    for (int k=0; k<MESSAGE_LENGTH; k++) {
                        if (chatrooms[i].messages[j][k] == 0) break;
                        else {
                            server_response[cc] = chatrooms[i].messages[j][k];
                            cc++;
                        }
                    }
                    server_response[cc] = ';';
                    cc++;
                }
            }
            cc--;
        }
    }

    server_response[cc] = '$';

    return cc + 1;

}
// #@nazwa pokoju^hasło%uzytkownik1;uzytkownik2;...%wiadomosc1;wiadomosc2;...$


// "usuwa" użytkownika zajmującego podany slot zerując wszystkie jego tablice
void deleteUser(int user_id) {
    if (user_id >= 0) {
        users[user_id].socket = -1;
        memset(users[user_id].name, 0, sizeof(users[user_id].name));
		users[user_id].roomId = -1;
    }
}

// działa analogicznie do deleteUser()
void deleteChatroom(int chatroom_id){
    int i = chatroom_id;
    if (i > -1) {
        memset(chatrooms[i].name, 0, sizeof(chatrooms[i].name));
        memset(chatrooms[i].password, 0, sizeof(chatrooms[i].password));
        for (int j=0; j<NUMBER_OF_USERS; j++) chatrooms[i].users[j] = -1;
        memset(chatrooms[i].messages, 0, sizeof(char) * NUMBER_OF_MESSAGES * MESSAGE_LENGTH);
    }
}

// zwraca 1 dla pustego pokoju
int isChatroomEmpty(int chatroom_id) {
    for (int i=0; i<NUMBER_OF_USERS; i++) if (chatrooms[chatroom_id].users[i] > -1) return 0;
    return 1;
}

// zwraca ID chatroomu, przyjmuje jego nazwę jako argument
int getChatroomIDbyName(char *name){
    int exists;
    for (int i=0; i<NUMBER_OF_CHATROOMS; i++){
        exists = i;
        for (int j=0; j<NAME_LENGTH; j++) {
			if (*(name + j) != chatrooms[i].name[j]) exists = -1; 
		}
        if (exists > -1) return exists;
    }
    return -1;
}

// zwraca najmniejszy indeks, pod którym do pokoju można dodać użytkownika
int getFristFreeSlotInChatroom(int chatroom_id) {
    for (int i=0; i<NUMBER_OF_USERS; i++) if (chatrooms[chatroom_id].users[i] == -1) return i;
    return -1;
}

// zwraca najmniejszy indeks, pod którym na serwerze można zapisać użytkownika
int getFirstFreeUserSlot() {
    for (int i=0; i<NUMBER_OF_USERS; i++) if (users[i].socket == -1) return i;
    return -1;
}

// zwraca indeks użytkownika w pokoju
int findUserInChatroom(int user_id, int chatroom_id) {
    for (int i=0; i<NUMBER_OF_USERS; i++) if (chatrooms[chatroom_id].users[i] == user_id) return i;
    return -1;
}

// użytkownik o ID = user_id dołącza do chatroomu o ID = chatroom_id
void joinChatroom(int user_id, int chatroom_id) {
    if (findUserInChatroom(user_id, chatroom_id) == -1) {
        int slot = getFristFreeSlotInChatroom(chatroom_id);
        if (slot != -1) {
            chatrooms[chatroom_id].users[slot] = user_id;
        }
    }
}

// zwraca minimalny indeks w tablicy pokoi, pod którym można umieścić nowy pokój
int getFirstFreeChatroomID() {
    for (int i=0; i<NUMBER_OF_CHATROOMS; i++) if (chatrooms[i].name[0] == 0) return i;
    return -1;
}

void sendToChatroom(int chatroom_id, char message[MESSAGE_LENGTH], int msg_length){

    // sprawdź, czy jest miejsce na nową wiadomość
    int i, j, spot = -1;
    for (i=0; i<NUMBER_OF_MESSAGES; i++) if (chatrooms[chatroom_id].messages[i][0] == 0) { spot = i; break; }

    if (spot > -1) for (i=0; i<msg_length; i++) chatrooms[chatroom_id].messages[spot][i] = message[i];

    // jeśli nie ma miejsca, usuń najstarszą wiadomość (pod indeksem [0]) i przesuń w tablicy wszystkie pozostałe, aby w ostatniej komórce było miejsce
    else {
        for (i=0; i < NUMBER_OF_MESSAGES - 1; i++)
            for (j=0; j<MESSAGE_LENGTH; j++)
                chatrooms[chatroom_id].messages[i][j] = chatrooms[chatroom_id].messages[i+1][j];
        memset(chatrooms[chatroom_id].messages[NUMBER_OF_MESSAGES-1], 0, sizeof(chatrooms[chatroom_id].messages[NUMBER_OF_MESSAGES-1]));
        for (j=0; j<msg_length; j++) chatrooms[chatroom_id].messages[NUMBER_OF_MESSAGES-1][j] = message[j];
    }
}

// prześlij odpowiedź serwera do wszystkich użytkowników, aby mogli zaktualizować stan aplikacji
void broadcast_server_response(int bytes, int userId, int roomId) {
    for (int i=0; i<NUMBER_OF_USERS; i++) {
        if (users[i].socket != -1 && ((users[i].roomId == roomId && roomId != -1) || i == userId)) {
            write(users[i].socket, server_response, bytes);
        }
    }
	//printf("Wyslano: %s\n", server_response);
	//printf("Do user %d room %d\n\n", userId, roomId);
}

// wątek tworzony dla każdego połączonego klienta. Cyklicznie nasłuchuje komunikatów, analizuje je i wykonuje polecenia.
// Następnie w odpowiedzi na polecenie odsyła stan aplikacji wszystkim klientom.
void *Thread_Listening(void *t_data) {

    pthread_detach(pthread_self());
    struct thread_data_t *th_data = (struct thread_data_t*)t_data;
    // dostęp: (*th_data).pole
    users[(*th_data).user_counter].name[0] = 'n';

    int i, j, k, l, bytes;
    char c;
    char helpful_string[MESSAGE_LENGTH], buf[128];
    int roomId, rc, fullMsg;

    while(1) {
        memset((*th_data).incoming_message, 0, sizeof((*th_data).incoming_message));
	
        //odczytanie wiadomości
		//(*th_data).bytes_read = read((*th_data).socket, (*th_data).incoming_message, sizeof((*th_data).incoming_message));
		fullMsg = 0;
		do{
			memset(buf, 0, sizeof(buf));
			rc = read((*th_data).socket, buf, sizeof(buf));
			for(j = fullMsg; j < fullMsg + rc; j++) (*th_data).incoming_message[j] = buf[j - fullMsg];
			fullMsg += rc;
		}
		while(buf[rc - 1] != '$' && rc != 0);
		(*th_data).bytes_read = fullMsg;
	
        if ((*th_data).bytes_read > 0) {
	    //printf("Otrzymana wiadomosc: %s\n\n", (*th_data).incoming_message);
            if ((*th_data).incoming_message[0] == '#') {

                pthread_mutex_lock(&mutex); // blokada wątku, komendy będą operować na pamięci współdzielonej
                memset(helpful_string, 0, sizeof(helpful_string));
                roomId = -1;
                switch ((*th_data).incoming_message[1]) {


                     // zmiana nazwy użytkownika, którego dotyczy proces
                    case '0':
                        for (i=3; i<(*th_data).bytes_read; i++){
                            c = (*th_data).incoming_message[i];
                            if (c != '$') users[(*th_data).user_counter].name[i-3] = c;
                            else break;
                        }
                        break;


                    // utworzenie pokoju o podanej nazwie
                    case '1':
                        for (i=3; i<(*th_data).bytes_read; i++) {
                            c = (*th_data).incoming_message[i];
                            if (c != '$') helpful_string[i-3] = c;
                            else break;
                        }

                        char name[NAME_LENGTH], password[PASSWORD_LENGTH];
						memset(name, 0, sizeof(name));
						memset(password, 0, sizeof(password));
						
                        i = 0;
                        while(helpful_string[i] != '^') {
							name[i] = helpful_string[i];
							i++;
						}
						

						j = 0;
                        for(j = i + 1; j < PASSWORD_LENGTH; j++) password[j - i - 1] = helpful_string[j];


                        if (getChatroomIDbyName(name) == -1) { // sprawdzenie, czy pokój o takiej nazwie już istnieje
                            i = getFirstFreeChatroomID();
                            // jeśli w pamięci serwera jest miejsce, stwórz pokój i dodaj tam użytkownika
                            if (i > -1) {
                                for (j=0; j<NAME_LENGTH; j++) chatrooms[i].name[j] = name[j];
                                for (j=0; j<PASSWORD_LENGTH; j++) chatrooms[i].password[j] = password[j];
                                joinChatroom((*th_data).user_counter, i);
								users[(*th_data).user_counter].roomId = i;
								roomId = i;
                            }
                        }
                        break;


                    // dołączenie do pokoju o podanej nazwie
                    case '2':
                        for (i=3; i<(*th_data).bytes_read; i++) {
                            c = (*th_data).incoming_message[i];
                            if (c != '$') helpful_string[i-3] = c;
                            else break;
                        }

                        i = getChatroomIDbyName(helpful_string);
                        if (i > -1) {
                            if (findUserInChatroom((*th_data).user_counter, i) == -1){ // jeśli pokój istnieje i nie ma w nim użytkownika
                                joinChatroom((*th_data).user_counter, i);
								users[(*th_data).user_counter].roomId = i;
								roomId = i;
                            }
                        }
                        break;


                    // opuszczenie pokoju o podanej nazwie
                    case '3':
                        for (i=3; i<(*th_data).bytes_read; i++) {
                            c = (*th_data).incoming_message[i];
                            if (c != '$') helpful_string[i-3] = c;
                            else break;
                        }

                        i = getChatroomIDbyName(helpful_string); // id pokoju, z którego wychodzi użytkownik
                        j = findUserInChatroom((*th_data).user_counter, i); // slot zajmowany obecnie przez użytkownika
                        if (i > -1 && j > -1){
							chatrooms[i].users[j] = -1;
							users[(*th_data).user_counter].roomId = -1;
							roomId = i;
						}

                        if (isChatroomEmpty(i)) deleteChatroom(i); // usuwamy pokój jeśli jest pusty
                        break;


                    // wysłanie wiadomości do pokoju, zapisanie jej jako "użytkownik:wiadomość"
                    case '4':
                        // odczytanie nazwy pokoju
                        for (i=3; i<(*th_data).bytes_read; i++) {
                            c = (*th_data).incoming_message[i];
                            if (c != '%') helpful_string[i-3] = c;
                            else break;
                        }

                        j = i + 1; // wiadomość zaczyna się od znaku j w tablicy incoming_message[]
                        i = getChatroomIDbyName(helpful_string); // id pokoju
						roomId = i;

                        // jeśli chatroom nie istnieje
                        if (i == -1) break;

                        // jeśli użytkownika nie ma w pokoju
                        if (findUserInChatroom((*th_data).user_counter, i) == -1) break;

                        // wczytaj wiadomość w ostatecznej formie do tablicy helpful_string[]
                        memset(helpful_string, 0, sizeof(helpful_string));
                        // nazwa użytkownika:
                        for (l=0; l<NAME_LENGTH; l++) {
                            c = users[(*th_data).user_counter].name[l];
                            if (c != 0) helpful_string[l] = c;
                            else break;
                        }
                        helpful_string[l] = ':';
                        l++;
                        // wiadomość:
                        for (k = 0; k<MESSAGE_LENGTH; k++) {
                            c = (*th_data).incoming_message[j + k];
                            if (c != '$') helpful_string[l + k] = (*th_data).incoming_message[j + k];
                            else break;
                        }
            
                        sendToChatroom(i, helpful_string, l+k+1);
                        break;


                    // klient prosi o wyslanie stanu serwera
                    case '5':
						for (i=3; i<(*th_data).bytes_read; i++) {
                            c = (*th_data).incoming_message[i];
                            if (c != '$') helpful_string[i-3] = c;
                            else break;
                        }
						roomId = getChatroomIDbyName(helpful_string);
                        break;
                }

                // po wykonaniu komendy, zaktualizuj i wyślij klientom obecny stan aplikacji
                bytes = update_server_response(roomId); // zmienna (int)bytes to długość wiadomości w bajtach
                broadcast_server_response(bytes, (*th_data).user_counter, users[(*th_data).user_counter].roomId);
                pthread_mutex_unlock(&mutex);
            }
            // jeśli wiadomość nie jest jednym z komunikatów (nie zaczyna się od #) nic się nie dzieje
            //
        }
        else {
            // jeśli użytkownik rozłączył się - wyczyść jego dane i zakończ wątek
            printf("Uzytkownik id: %d opuscil aplikacje\n", (*th_data).user_counter); 
	    deleteUser((*th_data).user_counter);
            free(t_data);
            pthread_exit(NULL);
        }
    }
}

void handleConnection(int connection_socket_descriptor) {

    pthread_mutex_lock(&mutex);
    // znajdź wolne miejsce dla użytkownika. Jeśli nie istnieje, odrzuć połączenie
    int user_counter = getFirstFreeUserSlot();
    if (user_counter != -1) {

        users[user_counter].socket = connection_socket_descriptor;
        printf("Nowy uzytkownik id: %d wszedl do aplikacji\n", user_counter);

        pthread_t thread1;

        struct thread_data_t *t_data1;
        t_data1 = malloc(sizeof(struct thread_data_t));

        t_data1->socket = connection_socket_descriptor;
        t_data1->user_counter = user_counter;

        // wątek będzie nasłuchiwał komunikatów od klienta
        if (pthread_create(&thread1, NULL, Thread_Listening, (void *)t_data1)) {
            printf("Błąd przy próbie utworzenia wątku\n");
            exit(1);
        };

    } else close(connection_socket_descriptor);
    pthread_mutex_unlock(&mutex);

}

int main(int argc, char*argv[]) {

    int port_num;
    int server_socket_descriptor;
    int connection_socket_descriptor;
    char reuse_addr_val = 1;
    struct sockaddr_in server_address;

    int i;

    // przypisanie numeru portu z argv[1] - pierwszy argument
    if (argc < 2) {
        printf("Podaj numer portu\n");
        exit(1);
    } else {
        sscanf (argv[1],"%d",&port_num);
    }

    printf("Serwer IRC\n");

    //initializacja gniazda serwera
    memset(&server_address, 0, sizeof(struct sockaddr));
    server_address.sin_family = AF_INET;
    server_address.sin_addr.s_addr = htonl(INADDR_ANY);
    server_address.sin_port = htons(port_num);

    server_socket_descriptor = socket(AF_INET, SOCK_STREAM, 0);
    if (server_socket_descriptor < 0) {
        fprintf(stderr, "%s: Błąd przy próbie utworzenia gniazda..\n", argv[0]);
        exit(1);
    }
    setsockopt(server_socket_descriptor, SOL_SOCKET, SO_REUSEADDR, (char*)&reuse_addr_val, sizeof(reuse_addr_val));

    if (bind(server_socket_descriptor, (struct sockaddr*)&server_address, sizeof(struct sockaddr)) < 0){
        fprintf(stderr, "%s: Błąd przy próbie dowiązania adresu IP i numeru portu do gniazda.\n", argv[0]);
        exit(1);
    }

    if (listen(server_socket_descriptor, QUEUE_SIZE) < 0){
        fprintf(stderr, "%s: Błąd przy próbie ustawienia wielkości kolejki.\n", argv[0]);
        exit(1);
    }

    //zerowanie struktur
    for (i=0; i<NUMBER_OF_USERS; i++) deleteUser(i);
    for (i=0; i<NUMBER_OF_CHATROOMS; i++) deleteChatroom(i);

    while(1) {
        connection_socket_descriptor = accept(server_socket_descriptor, NULL, NULL);
        if (connection_socket_descriptor < 0) {
            fprintf(stderr, "%s: Błąd przy próbie utworzenia gniazda dla połączenia.\n", argv[0]);
            exit(1);
        }

        handleConnection(connection_socket_descriptor);

    }

    close(server_socket_descriptor);
    return(0);

}
