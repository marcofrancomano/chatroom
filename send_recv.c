//  Author:         (c) 2021 Bonifacio Marco Francomano

#include <string.h>
#include <sys/socket.h>

#include "common.h"
#include "methods.h"

/*
 * Invia il messaggio contenuto nel buffer sulla socket desiderata.
 */
void send_msg(int socket, const char *msg) {
    int ret;

    // preparo msg_to_send copiando la stringa msg e aggiungendo '\n'
    char msg_to_send[MSG_SIZE];
    sprintf(msg_to_send, "%s\n", msg);

   int written_bytes=0;
   int msg_len=strlen(msg_to_send);
   while(written_bytes<msg_len){
    ret=send(socket,msg_to_send+written_bytes,msg_len-written_bytes,0);
    if(ret==-1 && errno==EINTR) continue;
    if(ret==-1) ERROR_HELPER(ret,"errore scrittura socket");
    written_bytes+=ret;

   }
}

/*
 * Riceve un messaggio dalla socket desiderata e lo memorizza nel
 * buffer buf di dimensione massima buf_len bytes.
 *
 * La fine di un messaggio in entrata è contrassegnata dal carattere
 * speciale '\n'. Il valore restituito dal metodo è il numero di byte
 * letti ('\n' escluso), o -1 nel caso in cui il client ha chiuso la
 * connessione in modo inaspettato.
 */

size_t recv_msg(int socket, char *buf, size_t buf_len) {
    int ret;
    int bytes_read = 0;

    // messaggi più lunghi di buf_len bytes vengono troncati
    while (bytes_read <= buf_len) {
       
       
        ret = recv(socket, buf + bytes_read, 1, 0);

        if (ret == 0) return -1; // il client ha chiuso la socket
        if (ret == -1 && errno == EINTR) continue;
        if(ret==-1) ERROR_HELPER(ret, "Errore nella lettura da socket");

        // controllo ultimo byte letto
        if (buf[bytes_read++] == '\n') break; // fine del messaggio: non incrementare bytes_read
    }

    /* Quando un messaggio viene ricevuto correttamente, il carattere
     * finale '\n' viene sostituito con un terminatore di stringa. */
    buf[bytes_read] = '\0';
    return bytes_read; // si noti che ora bytes_read == strlen(buf)
}
