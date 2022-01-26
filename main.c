
//  Author:         (c) 2021 Bonifacio Marco Francomano

#include <errno.h>
#include <pthread.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <semaphore.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#include "common.h"     // macro per gestione errori e parametri di configurazione
#include "methods.h"    // prototipi dei metodi definiti nei vari moduli C

// strutture dati per gestire il pool di utenti connessi
user_data_t* users[MAX_USERS];
unsigned int current_users;
sem_t user_data_sem;

/*
 * Metodo eseguito dal thread che deve processare la coda dei messaggi.
 */
void* broadcast_routine(void *args) {
    while (1) {
        msg_t *msg = dequeue();
        broadcast(msg);
        free(msg);
    }
}

/*
 * Metodo eseguito da ogni thread che deve processare una connessione in ingresso.
 */
void *chat_session(void *arg) {
    session_thread_args_t* args = (session_thread_args_t*)arg;

    char msg[MSG_SIZE];
    char error_msg[MSG_SIZE];
    char nickname[NICKNAME_SIZE];

    int ret;

    // lettura del messaggio #join <nick> dal client
    size_t join_msg_len = recv_msg(args->socket, msg, MSG_SIZE);
    if (join_msg_len < 0) {
        end_chat_session_for_closed_socket(args);
    } else if (parse_join_msg(msg, join_msg_len, nickname) != 0) { // setta nickname
        sprintf(error_msg, "Join fallita, messaggio ricevuto: %s", msg);
        end_chat_session(args, error_msg);
    }
    if (LOG) printf("Ricevuta richiesta di join con nickname %s\n", nickname);

    // registrazione dell'utente nella chat room
    ret = user_joining(args->socket, nickname, args->address);
    if (ret == TOO_MANY_USERS) {
        sprintf(error_msg, "Join fallita, troppi utenti connessi (%d)", MAX_USERS);
        end_chat_session(args, error_msg);
    } else if (ret == NICKNAME_NOT_AVAILABLE) {
        sprintf(error_msg, "Join fallita, nickname non disponibile");
        end_chat_session(args, error_msg);
    }
    sprintf(error_msg, "Utente %s, benvenuto nella chatroom!!!", nickname);
    send_msg_by_server(args->socket, error_msg);
    send_help(args->socket);

    // fase di join completata, inizio sessione di chat
    int quit = 0;
    do {
        size_t len = recv_msg(args->socket, msg, MSG_SIZE);

        if (len > 0) {
            // determina se l'input ricevuto è un comando o un messaggio da inoltrare
            if (msg[0] == COMMAND_CHAR) {
                if (strcmp(msg + 1, LIST_COMMAND) == 0) {
                    printf("Ricevuto comando list dall'utente %s\n", nickname);
                    send_list(args->socket);
                } else if (strcmp(msg + 1, QUIT_COMMAND) == 0) {
                    printf("Ricevuto comando quit dall'utente %s\n", nickname);
                    quit = 1;
                } else if (strcmp(msg + 1, STATS_COMMAND) == 0) {
                    printf("Ricevuto comando stats dall'utente %s\n", nickname);
                    send_stats(args->socket);
                } else if (strcmp(msg + 1, HELP_COMMAND) == 0) {
                    if (LOG) printf("Invio help all'utente %s\n", nickname);
                    send_help(args->socket);
                } else {
                    sprintf(error_msg, "Comando sconosciuto, inviare %c%s per la lista dei comandi disponibili.", COMMAND_CHAR, HELP_COMMAND);
                    send_msg_by_server(args->socket, error_msg);
                }
            } else {
                // inserisci il messaggio nella coda dei messaggi da inviare
                enqueue(nickname, msg);
            }
        } else if (len < 0) {
            quit = -1; // errore: connessione chiusa dal client inaspettatamente
        } else {
            // ignora messaggi vuoti (len == 0) inviati dal client
        }

    } while(!quit);

    ret = user_leaving(args->socket);
    if (quit > 0) { // quit == 1: invio un messaggio di uscita al client
        if (ret == USER_NOT_FOUND) {
            sprintf(error_msg, "Utente non trovato: %s", nickname); // bug nel server?
        } else {
            sprintf(error_msg, "%s, grazie per aver partecipato alla chatroom!!!", nickname);
        }
        end_chat_session(args, error_msg);
    } else { // quit == -1: il client ha chiuso la connessione inaspettatamente
        end_chat_session_for_closed_socket(args);
    }

    return NULL; // il compilatore non "sa" che end_chat_session() esegue phtread_exit()
}

/*
 * Metodo che prepara le socket ed esegue il loop per accettare connessioni in ingresso.
 */
void listen_on_port(unsigned short port_number_no) {
    int ret;
    int server_desc, client_desc;

    struct sockaddr_in server_addr = {0};
    int sockaddr_len = sizeof(struct sockaddr_in); // usato da accept()

    // impostazioni per le connessioni in ingresso
    server_desc = socket(AF_INET , SOCK_STREAM , 0);
    ERROR_HELPER(server_desc, "Impossibile creare socket server_desc");

    server_addr.sin_addr.s_addr = INADDR_ANY; // accettiamo connessioni da tutte le interfacce (es. lo 127.0.0.1)
    server_addr.sin_family      = AF_INET;
    server_addr.sin_port        = port_number_no;

    int reuseaddr_opt = 1; // SO_REUSEADDR permette un riavvio rapido del server dopo un crash
    ret = setsockopt(server_desc, SOL_SOCKET, SO_REUSEADDR, &reuseaddr_opt, sizeof(reuseaddr_opt));
    ERROR_HELPER(ret, "Impossibile settare l'opzione SO_REUSEADDR");

    // binding dell'indirizzo alla socket
    ret = bind(server_desc, (struct sockaddr*) &server_addr, sockaddr_len);
    ERROR_HELPER(ret, "Impossibile eseguire bind su socket_desc");

    // marca la socket come passiva per mettersi in ascolto
    ret = listen(server_desc, MAX_CONN_QUEUE);
    ERROR_HELPER(ret, "Impossibile eseguire listen su socket_desc");

    struct sockaddr_in* client_addr = calloc(1, sizeof(struct sockaddr_in)); 

    // accetta connessioni in ingresso
    while (1) {
        /** client_addr è il buffer da usare per la connessione in ingresso che sto per accettare **/

        client_desc = accept(server_desc, (struct sockaddr*)client_addr, (socklen_t*) &sockaddr_len);
        if (client_desc == -1 && errno == EINTR) continue;
        ERROR_HELPER(client_desc, "Impossibile eseguire accept su socket_desc");

        
        
        session_thread_args_t* args=(session_thread_args_t*)malloc(sizeof(session_thread_args_t));
        args->socket=client_desc;
        args->address=client_addr;
        
        pthread_t thread;
        ret=pthread_create(&thread,NULL,chat_session,args);
        if(ret)ERROR_HELPER(ret,"errore creazione thread");

        ret=pthread_detach(thread);
        if(ret)ERROR_HELPER(ret,"errore detach");


        // alloco un nuovo buffer per servire la prossima connessione in ingresso
        client_addr = calloc(1, sizeof(struct sockaddr_in));
    }
}

/*
 * Metodo main eseguito per primo nel server.
 */
int main(int argc, char* argv[]) {
    if (argc != 2) {
        fprintf(stderr, "Sintassi: %s <port_number>\n", argv[0]);
        exit(EXIT_FAILURE);
    }

    int ret;

    unsigned short port_number_no; // il suffisso "_no" sta per network byte order

    // ottieni il numero di porta da usare per il server dall'argomento del comando
    long tmp = strtol(argv[1], NULL, 0);
    if (tmp < 1024 || tmp > 49151) {
        fprintf(stderr, "Errore: utilizzare un numero di porta compreso tra 1024 e 49151.\n");
        exit(EXIT_FAILURE);
    }
    port_number_no = htons((unsigned short)tmp);

    // inizializza strutture dati per gli utenti
    current_users = 0;
    ret = sem_init(&user_data_sem, 0, 1);
    ERROR_HELPER(ret, "Errore nell'inizializzazione del semaforo user_data_sem");

    // inizializza coda per i messaggi
    initialize_queue();

    

    pthread_t thread;
    ret=pthread_create(&thread,NULL,broadcast_routine,NULL);
    if(ret)ERROR_HELPER(ret,"errore creazione thread");

    ret=pthread_detach(thread);
    if(ret)ERROR_HELPER(ret,"errore detach");


    // inizia ad accettare connessioni in ingresso sulla porta data
    listen_on_port(port_number_no);

    exit(EXIT_SUCCESS);
}
