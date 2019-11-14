#include <stdio.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#include <stdlib.h>
#include <pthread.h>
#include <sys/ipc.h>
#include <sys/msg.h>
#include <string.h>

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

pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;

void *DotProduct(void *arg) {
    // Get the Message Queue id
    int id = *((int *) arg);
    
    pthread_mutex_lock(&lock);

    // Read in messages from package
    Msg message;
    if (msgrcv(id, &message, sizeof(Msg), 1, 0) < 0) {
        perror("msgrcv");
        exit(3);
    }

    // Calculate dot product
    // Matrix 1's vector is stored first in the data array
    int dotProduct = 0;
    for (int i = 0; i < (message.innerDim); i++) {
        int j = i+4;
        dotProduct += (message.data[i] * message.data[j]);
    }
    printf("Dot product: %d\n", dotProduct);

    // Update data array to hold the dot product
    message.data[0] = dotProduct;

    // Set message type to 2 to send back to package
    message.type = 2;

    // Calculate size of message to send
    // 5 is for the 5 integers being sent over in the struct
    int size = 5 * sizeof(int);

    // Send dot products back to package
    if (msgsnd(id, &message, size, IPC_NOWAIT) < 0) {
        perror("msgsnd");
        exit(3);
    }

    pthread_mutex_unlock(&lock);
    return (void *)NULL;
}

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

    // // Receive number of jobs
    // int numThreads;
    // if (msgrcv(msqid, &numThreads, sizeof(int), 1, 0) < 0) {
    //     perror("msgrcv");
    //     exit(3);
    // }

    pthread_t threads[15];
    int *id = malloc(sizeof(*id));
    *id = msqid;        

    for (int i = 0; i < 15; i++) {
        pthread_create(&threads[i], NULL, &DotProduct, id);    
    }

    for (int i = 0; i < 15; i++) {
        pthread_join(threads[i], NULL);
    }

    free(id);

    return 0;

}

