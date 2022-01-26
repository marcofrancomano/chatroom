//  Author:         (c) 2021 Bonifacio Marco Francomano

#include <stdio.h>
#include <semaphore.h>

#include "common.h"
#include "methods.h"

#define MSG_BUFFER_SIZE 256

// buffer circolare per gestire una coda FIFO (First In First Out)
msg_t* msg_buffer[MSG_BUFFER_SIZE];
int read_index;
int write_index;


sem_t empty,filled,writesem;
/*
 * Inizializza la coda.
 */

void initialize_queue() {

    int ret;

    read_index = 0,
    write_index = 0;

    

    ret=sem_init(&empty,0,MSG_BUFFER_SIZE);
    if(ret<0) ERROR_HELPER(ret,"errore sem_init");
    ret=sem_init(&filled,0,0);
    if(ret<0) ERROR_HELPER(ret,"errore sem_init");
    ret=sem_init(&writesem,0,1);
    if(ret<0) ERROR_HELPER(ret,"errore sem_init");
}

/*
 * Genera un messaggio a partire dagli argomenti e lo inserisce nella coda.
 *
 * Può essere eseguito da più thread contemporaneamente.
 */
void enqueue(const char *nickname, const char *msg) {

    int ret;

    // preparo l'elemento msg_t* msg_data da inserire nella coda
    msg_t* msg_data = (msg_t*)malloc(sizeof(msg_t));
    sprintf(msg_data->nickname, "%s", nickname);
    sprintf(msg_data->msg, "%s", msg);

    

    ret=sem_wait(&empty);
    if(ret<0) ERROR_HELPER(ret,"errore nella wait");

    ret=sem_wait(&writesem);
    if(ret<0) ERROR_HELPER(ret,"errore nella wait");

    msg_buffer[write_index] = msg_data;
    write_index = (write_index + 1) % MSG_BUFFER_SIZE;

    ret=sem_post(&writesem);
    if(ret<0) ERROR_HELPER(ret,"errore nella post");

    ret=sem_post(&filled);
    if(ret<0) ERROR_HELPER(ret,"errore nella post");

}

/*
 * Estrae e restituisce il primo messaggio dalla coda (politica FIFO).
 *
 * Il thread che esegue questo metodo è uno soltanto.
 */

msg_t* dequeue() {

    int ret;
    msg_t *msg = NULL;

    ret=sem_wait(&filled);
    if(ret<0) ERROR_HELPER(ret,"errore nella wait");


    msg = msg_buffer[read_index];
    read_index = (read_index + 1) % MSG_BUFFER_SIZE;

    ret=sem_post(&empty);
    if(ret<0) ERROR_HELPER(ret,"errore nella post");

    return msg;
}
