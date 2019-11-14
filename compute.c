#include <stdio.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#include <stdlib.h>
#include <pthread.h>
#include <sys/ipc.h>
#include <sys/msg.h>

#define MAX_THREADS = 2500 // Largest dimension is 50; 50 * 50 = 2500
#define MAX_QUEUE = 65536

typedef struct QueueMessage {
    long type;      // Message type
    int jobid;      // Thread #
    int rowvec;     // Row index of new matrix
    int colvec;     // Col index of new matrix
    int innerDim;   // Inner dimension for mult
    int data[100];  // Vector data for mult
} Msg;

typedef struct ThreadData {
    long type;	 // Message type
    int rv;      // Row vector
    int cv;      // Col vector
    int inner;   // Inner dim
    int job;     // Thread #
    int d[100];  // Data for dot product
    int sleep;   // Sleep in seconds
    int queueid; // Message queue id
} ThreadData;

// pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;

// void *DotProduct(void *arg) {
//     int id = *((int *) arg);
    
//     pthread_mutex_lock(&lock);
//     Msg *myMessage = malloc(sizeof *myMessage);
//     printf("%lu\n", sizeof(myMessage));
//     if (msgrcv(id, &myMessage, sizeof(*myMessage), 1, 0) < 0) {
//         perror("msgrcv");
//         exit(3);
//     }
//     printf("%d\n %d\n", myMessage->colvec, myMessage->innerDim);
//     // free(myMessage);
//     pthread_mutex_unlock(&lock);
//     return (void *)NULL;
// }

// void *DotProduct(void *arg) {
//     int id = *((int *) arg);
    
//     pthread_mutex_lock(&lock);
//     Msg message;
//     printf("%lu\n", sizeof(message));
//     if (msgrcv(id, &message, sizeof(message), 1, 0) < 0) {
//         perror("msgrcv");
//         exit(3);
//     }
//     printf("%d\n %d\n", message.colvec, message.innerDim);
//     // free(myMessage);
//     pthread_mutex_unlock(&lock);
//     return (void *)NULL;
// }

int main(void) {
    // Set up the Message Queue for Reader Process
    key_t key;
    int msqid;

    if ((key = ftok("aeyuk", 'b')) < 0) {
        perror("ftok");
        exit(1);
    }

    if ((msqid = msgget(key, 0666 | IPC_CREAT)) < 0) {
        perror("mssget");
        exit(1);
    }  

/////////////////////////////////
    printf("%d\n", msqid);
/////////////////////////////////

    // pthread_t threads[numThreads];

    // int *id = malloc(sizeof(*id));
    // *id = msqid;        

    // while(1) {
    //     pthread_create(&threads[i], NULL, &DotProduct, id);    
    // }

    // free(id);

    Msg myMessage;
    while(1) {

        if (msgrcv(msqid, &myMessage, sizeof(Msg), 1, 0) < 0) {
            perror("msgrcv");
            exit(3);
        }
        printf("%d %d %d \n", myMessage.rowvec, myMessage.colvec, myMessage.jobid);
    }
    return 0;

}

