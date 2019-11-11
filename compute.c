#include <stdio.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#include <stdlib.h>
#include <pthread.h>
#include <sys/ipc.h>
#include <sys/msg.h>

typedef struct QueueMessage {
    long type;
    int jobid;
    int rowvec;
    int colvec;
    int innerDim;
    int data[100];
} Msg;

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

    Msg message;

    msgrcv(msgid, &message, sizeof(Msg), 1, 0);

    printf("Message received. Inner dim: %d\n", message.innerDim);

}
