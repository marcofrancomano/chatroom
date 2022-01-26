#include <pthread.h>
#include <semaphore.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#include "common.h"
#include "methods.h"

// variabili globali di altri moduli possono essere "richiamate" tramite extern
extern sem_t user_data_sem;
extern user_data_t* users[];
extern unsigned int current_users;

/*
 * Processa un messaggio #join ed estrae il nickname in esso specificato
 * scrivendolo nel buffer passato come terzo argomento.
 */
char join_msg_prefix[MSG_SIZE];
size_t join_msg_prefix_len = 0;

int parse_join_msg(char* msg, size_t msg_len, char* nickname) {
    // vogliamo eseguire le operazioni nel blocco una volta sola per efficienza
    if (join_msg_prefix_len == 0) {
        sprintf(join_msg_prefix, "%c%s ", COMMAND_CHAR, JOIN_COMMAND);
        join_msg_prefix_len = strlen(join_msg_prefix);
    }

    if (msg_len > join_msg_prefix_len && !strncmp(msg, join_msg_prefix, join_msg_prefix_len)) {
        sprintf(nickname, "%s", msg + join_msg_prefix_len);
        return 0;
    } else {
        return -1;
    }
}

/*
 * Gestisce il tentativo di accedere alla chatroom da parte di un utente
 * appena connessosi al server. In caso di successo registra l'utente
 * nella struttura dati del server e notifica tutti gli utenti.
 */
int user_joining(int socket, const char *nickname, struct sockaddr_in* address) {
    int ret;

    ret = sem_wait(&user_data_sem);
    ERROR_HELPER(ret, "Errore nella chiamata sem_wait su user_data_sem");

    // verifica limite sul massimo numero di utenti
    if (current_users == MAX_USERS) {
        ret = sem_post(&user_data_sem);
        ERROR_HELPER(ret, "Errore nella chiamata sem_post su user_data_sem");
        return TOO_MANY_USERS;
    }

    // verifica disponibilità nickname
    int i;
    for (i = 0; i < current_users; i++)
        if (strcmp(nickname, users[i]->nickname) == 0) {
            ret = sem_post(&user_data_sem);
            ERROR_HELPER(ret, "Errore nella chiamata sem_post su user_data_sem");
            return NICKNAME_NOT_AVAILABLE;
        }

    // creazione nuovo utente
    user_data_t* new_user = (user_data_t*)malloc(sizeof(user_data_t));
    new_user->socket = socket;
    sprintf(new_user->nickname, "%s", nickname);
    inet_ntop(AF_INET, &(address->sin_addr), new_user->address, INET_ADDRSTRLEN);
    new_user->port = ntohs(address->sin_port);
    new_user->sent_msgs = 0,
    new_user->rcvd_msgs = 0;
    users[current_users++] = new_user;

    // notifica la presenza a tutti gli utenti
    char msg[MSG_SIZE];
    sprintf(msg, "L'utente %s è entrato nella chatroom", new_user->nickname);
    if (LOG) printf("%s\n", msg);
    enqueue(SERVER_NICKNAME, msg); // anche il nuovo utente lo riceverà

    ret = sem_post(&user_data_sem);
    ERROR_HELPER(ret, "Errore nella chiamata sem_post su user_data_sem");

    return 0;
}

/*
 * Notifica gli utenti dell'imminente uscita di un utente.
 */
int user_leaving(int socket) {
    int ret;

    ret = sem_wait(&user_data_sem);
    ERROR_HELPER(ret, "Errore nella chiamata sem_wait su user_data_sem");

    int i;
    for (i = 0; i < current_users; i++) {
        if (users[i]->socket == socket) {
            char msg[MSG_SIZE];

            // notifica a tutti gli utenti che users[i] sta lasciando la chatroom
            sprintf(msg, "L'utente %s ha lasciato la chatroom", users[i]->nickname);
            if (LOG) printf("%s\n", msg);

            enqueue(SERVER_NICKNAME, msg);

            free(users[i]);
            for (; i < current_users - 1; i++)
                users[i] = users[i+1]; // shift di 1 per tutti gli elementi successivi
            current_users--;

            ret = sem_post(&user_data_sem);
            ERROR_HELPER(ret, "Errore nella chiamata sem_post su user_data_sem");

            return 0;
        }
    }

    ret = sem_post(&user_data_sem);
    ERROR_HELPER(ret, "Errore nella chiamata sem_post su user_data_sem");

    return USER_NOT_FOUND;
}

/*
 * Aggiunge al messaggio passato come argomento un prefisso contenente
 * il nickname dell'utente prima di inviarlo sulla socket desiderata.
 */
void send_msg_by_server(int socket, const char *msg) {
    char msg_by_server[MSG_SIZE];
    sprintf(msg_by_server, "%s%c%s", SERVER_NICKNAME, MSG_DELIMITER_CHAR, msg);
    send_msg(socket, msg_by_server);
}

/*
 * Inoltra un messaggio di un utente a tutti gli altri nella chatroom.
 *
 * Nel caso in cui il messaggio sia originato da SERVER_NICKNAME, esso
 * viene inviato a tutti gli utenti connessi.
 */
void broadcast(msg_t* msg) {

    int ret;
    ret = sem_wait(&user_data_sem);
    ERROR_HELPER(ret, "Errore nella chiamata sem_wait su user_data_sem");

    int i;
    char msg_to_send[MSG_SIZE];
    sprintf(msg_to_send, "%s%c%s", msg->nickname, MSG_DELIMITER_CHAR, msg->msg);

    for (i = 0; i < current_users; i++) {
        if (strcmp(msg->nickname, users[i]->nickname) != 0) {
            send_msg(users[i]->socket, msg_to_send);
            users[i]->rcvd_msgs++;
        } else {
            users[i]->sent_msgs++;
        }
    }

    ret = sem_post(&user_data_sem);
    ERROR_HELPER(ret, "Errore nella chiamata sem_post su user_data_sem");
}

/*
 * Chiude i descrittori aperti e libera la memoria per conto del metodo
 * end_chat_session(). Il suffisso "_for_closed_socket" indica che esso
 * può essere usato anche nel caso in cui il client abbia chiuso la
 * sua connessione in modo inatteso per il server.
 */
void end_chat_session_for_closed_socket(session_thread_args_t* args) {
    int ret = close(args->socket);
    ERROR_HELPER(ret, "Errore nella chiusura di una socket");
    free(args->address);
    free(args);
    pthread_exit(NULL);
}

/*
 * Invia un messaggio di chiusura al client, e procede chiudendo i
 * descrittori aperti e liberando la memoria per conto di chat_session().
 */
void end_chat_session(session_thread_args_t* args, const char *msg) {
    send_msg_by_server(args->socket, msg);
    end_chat_session_for_closed_socket(args);
}

/*
 * Eseguito in risposta ad un comando #help.
 */
void send_help(int socket) {
    char msg[MSG_SIZE];

    sprintf(msg, "Oltre ai messaggi da condividere con gli altri utenti, è possibile inviare "
                    "dei comandi che verranno visualizzati ed interpretati solo dal server.");
    send_msg_by_server(socket, msg);

    sprintf(msg, "La lista dei comandi disponibili è la seguente:");
    send_msg_by_server(socket, msg);

    sprintf(msg, "\t%c%s: stampa la lista degli utenti correntemente connessi", COMMAND_CHAR, LIST_COMMAND);
    send_msg_by_server(socket, msg);

    sprintf(msg, "\t%c%s: termina la sessione", COMMAND_CHAR, QUIT_COMMAND);
    send_msg_by_server(socket, msg);

    sprintf(msg, "\t%c%s: stampa alcune statistiche sulla sessione corrente", COMMAND_CHAR, STATS_COMMAND);
    send_msg_by_server(socket, msg);

    sprintf(msg, "\t%c%s: mostra nuovamente la lista dei comandi disponibili", COMMAND_CHAR, HELP_COMMAND);
    send_msg_by_server(socket, msg);

    sprintf(msg, "Un messaggio che inizia per %c viene sempre interpretato come comando.", COMMAND_CHAR);
    send_msg_by_server(socket, msg);
}

/*
 * Eseguito in risposta ad un comando #list.
 */
void send_list(int socket) {
    char msg[MSG_SIZE];
    int ret;

    ret = sem_wait(&user_data_sem);
    ERROR_HELPER(ret, "Errore nella chiamata sem_wait su user_data_sem");

    sprintf(msg, "Lista utenti connessi (%d): ", current_users);
    int i;
    for (i = 0; i < current_users; i++) {
        char tmp[MSG_SIZE];
        sprintf(tmp, "%s (%s:%u), ", users[i]->nickname, users[i]->address, users[i]->port);
        strcat(msg, tmp);
    }

    ret = sem_post(&user_data_sem);
    ERROR_HELPER(ret, "Errore nella chiamata sem_post su user_data_sem");

    send_msg_by_server(socket, msg);
}

/*
 * Eseguito in risposta ad un comando #stats.
 */
void send_stats(int socket) {
    char msg[MSG_SIZE];
    int ret;

    ret = sem_wait(&user_data_sem);
    ERROR_HELPER(ret, "Errore nella chiamata sem_wait su user_data_sem");

    int i;
    for (i = 0; i < current_users; i++)
        if (users[i]->socket == socket) {
            sprintf(msg, "Messaggi inviati ad altri utenti: %u, messaggi ricevuti: %u", users[i]->sent_msgs, users[i]->rcvd_msgs);
            break;
        }

    ret = sem_post(&user_data_sem);
    ERROR_HELPER(ret, "Errore nella chiamata sem_post su user_data_sem");

    send_msg_by_server(socket, msg);
}
