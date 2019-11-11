#include <stdio.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
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

pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;

void *DotProduct(void *arg) {
    ThreadData *myArgs = (ThreadData *) arg;
    Msg* myMessage = malloc(sizeof(Msg));
    pthread_mutex_lock(&lock);
    // printf("thread: %d %d\n", myArgs->rv, myArgs->cv);
    myMessage->rowvec = myArgs->rv;
    myMessage->colvec = myArgs->cv;
    myMessage->innerDim = myArgs->inner;
    pthread_mutex_unlock(&lock);
    return (void *)myMessage;
}

int** LoadMatrix(FILE** fptr, int* numRows, int* numCols) {
    // Save row and column values
    fseek(*fptr, 0, SEEK_SET);
    *numRows = fgetc(*fptr) - 48;
    fgetc(*fptr); // Skips space
    *numCols = fgetc(*fptr) - 48;

    // Allocate space for matrix
    int** matrix = (int **)malloc(*numRows * sizeof(int *));
    for (int i = 0; i < *numRows; i++) {
        matrix[i] = (int *)malloc(*numCols * sizeof(int));
    }

    // Populate matrices with data from files
    char ch, buffer[32];
    int i = 0, j = 0, arr[*numRows * *numCols];

    // Load matrix data into array
    while (1) {
        ch = fgetc(*fptr);
        if (ch == EOF) {
            arr[j] = atoi(buffer);
            break;
        }
        else if (ch == ' ') {
            arr[j] = atoi(buffer);
            j++;
            bzero(buffer, 32);
            i = 0;
        }
        else {
            buffer[i] = ch;
            i++;
        }
    }

    // Load array of data into matrix
    int q = 0;
    for (int row = 0; row < *numRows; row++) {
        for (int col = 0; col < *numCols; col++) {
            matrix[row][col] = arr[q++];
        }
    }

    // Close the file
    fclose(*fptr);

    return matrix;
}


int main(int argc, char* argv[]) {
    // Open matrix file 1 and load data
    FILE *fptr1;
    fptr1 = fopen(argv[1], "r");
    if (NULL == fptr1) {
        printf("Cannot open file %s\n", argv[1]);
        exit(0);
    }
    int numRows1 = 0;
    int numCols1 = 0;
    int** matrix1 = LoadMatrix(&fptr1, &numRows1, &numCols1);

    // Open matrix file 2 and load data
    FILE *fptr2;
    fptr2 = fopen(argv[2], "r");
    if (NULL == fptr2) {
        printf("Cannot open file %s\n", argv[2]);
        exit(0);
    }
    int numRows2 = 0;
    int numCols2 = 0;
    int** matrix2 = LoadMatrix(&fptr2, &numRows2, &numCols2);

    // // Set up Message Queue for Writer Process
    // key_t key;
    // int msgid;
    // if ((key = ftok("aeyuk", 'b')) == -1) {
    //     printf("ftok() error!\n");
    //     exit(1);
    // }
    
    // if ((msgid = msgget(key, 0666 | IPC_CREAT)) == -1) {
    //     printf("mssget error!\n");
    //     exit(1);
    // }

    Matrices M;
    M.type = 1;
    M.m1 = matrix1;
    M.m2 = matrix2;
    M.r1 = numRows1;
    M.c1 = numCols1;
    M.r2 = numRows2;
    M.c2 = numCols2;

    // msgsnd(msgid, &M, sizeof(M), 0);


    // Create thread to package subtasks
    int job = 0;
    int totalJobs = numRows1 * numCols1;
    pthread_t threads[totalJobs];
    Msg* message;
    ThreadData* threadData = malloc(totalJobs * sizeof(ThreadData));
    // int rc = 0;
    for (int i = 0; i < numRows1; i++) {
        for (int j = 0; j < numCols2; j++) {
            threadData[job].sleep = atoi(argv[3]);
            threadData[job].rv = i;
            threadData[job].cv = j;
            threadData[job].inner = numCols1;
            threadData[job].job = job;
            pthread_create(&threads[job], NULL, &DotProduct, &threadData[job]);
            job++;
        }
    }
    for (int i = 0; i < totalJobs; i++) {
        pthread_join(threads[i], (void **)&message);
        printf("join: %d %d\n", message->rowvec, message->colvec);
        // rc = msgsnd(msgid, &message, sizeof(Msg), 0);
        // printf("Sending job id %d type %lu size %lu (rc=%d)\n", 
        // message->jobid, message->type, sizeof(message), rc);
        free(message);
    }

    //Free Matrices
    for (int i = 0; i < numRows1; i++)
        free(matrix1[i]);
    free(matrix1);

    for (int i = 0; i < numRows2; i++)
        free(matrix2[i]);
    free(matrix2);

    free(threadData);

    return 0;
}

