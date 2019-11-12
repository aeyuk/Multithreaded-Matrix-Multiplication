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

typedef struct msg_buf {
    long msg_type;
    char text[100];
} msg_buf;

typedef struct QueueMessage {
    long type;
    int jobid;
    int rowvec;
    int colvec;
    int innerDim;
    int data[100];
} Msg;

typedef struct Matrices {
    long type;
    int** m1;
    int r1;
    int c1;
    int r2;
    int c2;
    int** m2;
} Matrices;

typedef struct ThreadData {
    int rv;
    int cv;
    int inner;
    int job;
    int sleep;
} ThreadData;

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

    // Receive matrix data
    Msg* message = malloc(15 * sizeof(message));
    for (int i = 0; i < 15; i++) {
        if (msgrcv(msqid, &message[i], sizeof(message[i]), 1, IPC_NOWAIT) <= 0) {
            perror("msgrcv");
            exit(3);
        }
        printf("received %d %d\n", message[i].rowvec, message[i].colvec);
    }

    return 0;

}


/*
How do i share the entire matrices? Row/Col Nums?
What am I even doing?
Why is a threadpool so complex?
What am I packaging?
How do you return a value from a thread?
When do you join threads?
*/
