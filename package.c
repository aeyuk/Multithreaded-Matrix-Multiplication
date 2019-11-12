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
    long type;      // Message type
    int jobid;      // Thread #
    int rowvec;     // Row index of new matrix
    int colvec;     // Col index of new matrix
    int innerDim;   // Inner dimension for mult
    int data[100];  // Vector data for mult
} Msg;

typedef struct MatixInfo {
    long type;   // Message type
    int r1;      // #Rows in Matrix 1 
    int c2;      // #Cols in Matrix 2
} MatrixInfo;

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
    ThreadData *myArgs = (ThreadData *) arg;
    Msg* myMessage = malloc(sizeof(Msg));
    // Enter the critical section
    pthread_mutex_lock(&lock);
    sleep(myArgs->sleep);
    // Package message to send to compute
    myMessage->type = 1;
    myMessage->jobid = myArgs->job;
    myMessage->rowvec = myArgs->rv;
    myMessage->colvec = myArgs->cv;
    myMessage->innerDim = myArgs->inner;
    // Copy data array over
    for (int i = 0; i < myArgs->inner * 2; i++) {
        myMessage->data[i] = myArgs->d[i];
    }
    // Send message
    int rc = 0;
    if ((rc = msgsnd(myArgs->queueid, &myMessage, sizeof(myMessage), 0)) < 0) {
        printf("msgsnd error!\n");
    }
    // Status message
    printf("Sending job id %d type %lu size %lu (rc = %d)\n",
    myMessage->jobid, myMessage->type, sizeof(myMessage), rc);
    // Exit the critical section
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
            memset(&buffer, 0, 32);
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

    // Set up Message Queue for Writer Process
    key_t key;
    int msqid;
    if ((key = ftok("aeyuk", 'b')) == -1) {
        printf("ftok() error!\n");
        exit(1);
    }
    
    if ((msqid = msgget(key, 0666 | IPC_CREAT)) == -1) {
        printf("mssget error!\n");
        exit(1);
    }

    MatrixInfo M;
    M.r1 = numRows1;
    M.c2 = numCols2;
    M.type = 1;

    printf("Sending matrix data\n");
    msgsnd(msqid, (void *) &M, sizeof(M), 0);

    // Create threads to package subtasks
    // One subtask per dot product
    int job = 0;
    int totalJobs = numRows1 * numCols2;
    pthread_t threads[totalJobs];
    ThreadData* threadData = malloc(totalJobs * sizeof(ThreadData));
    int index = 0;
    for (int i = 0; i < numRows1; i++) {
        for (int j = 0; j < numCols2; j++) {
            // Send array of structs with correct data
            threadData[job].sleep = atoi(argv[3]);
            threadData[job].rv = i;
            threadData[job].cv = j;
            threadData[job].inner = numCols1;
            threadData[job].job = job;
            threadData[job].queueid = msqid;
            // Add Matrix 1's row vector values to data array
            for (index = 0; index < numCols1; index++) {
                threadData[job].d[index] = matrix1[i][index];
            }
            // Add Matrix 2's vol vector values to data array
            for (index = numCols1; index < numCols1*2; index++) {
                threadData[job].d[index] = matrix2[index-numCols1][j];
            }
            pthread_create(&threads[job], NULL, &DotProduct, &threadData[job]);
            job++;
        }
    }

    // Join threads
    for (int i = 0; i < totalJobs; i++) {
        pthread_join(threads[i], NULL);
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


