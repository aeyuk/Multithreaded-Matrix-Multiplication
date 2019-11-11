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

int main(void) {
    // Set up the Message Queue for Reader Process
    key_t key;
    int msgid;

    if ((key = ftok("aeyuk", 'b')) == -1) {
        printf("ftok() error!\n");
        exit(1);
    }

    if ((msgid = msgget(key, 0666 | IPC_CREAT)) == -1) {
        printf("mssget error!\n");
        exit(1);
    } 

    Matrices M;
    msgrcv(msgid, &M, sizeof(Matrices), 1, 0);
    printf("%d\n", M.c2);

    Msg message;
    msgrcv(msgid, &message, sizeof(Msg), 1, 0);

    printf("Message received. colvec %d\n", message.colvec);



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
