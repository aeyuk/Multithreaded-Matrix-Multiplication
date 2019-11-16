#include <stdio.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#include <stdlib.h>
#include <pthread.h>
#include <sys/ipc.h>
#include <sys/msg.h>
#include <string.h>
#include <errno.h>

#define MAX_THREADS 2500 // Largest dimension is 50; 50 * 50 = 2500
#define MAX_QUEUE 65536

/*
Code for structure of the threadpool heavily referenced
mbrossard's repository "A simple C Thread pool implementation"
https://github.com/mbrossard/threadpool
*/

/* Struct of message to be sent and received */
typedef struct QueueMessage {
    long type;      // Message type
    int jobid;      // Thread #
    int rowvec;     // Row index of new matrix
    int colvec;     // Col index of new matrix
    int innerDim;   // Inner dimension for mult
    int data[100];  // Vector data for mult
} Msg;

/* Struct holding number of dot products */
typedef struct MatrixInfo {
    long type;
    int jobs;
} MatrixInfo;

typedef struct ThreadData {
    int msqid;
    int outputOnly;
} ThreadData;

typedef enum ThreadpoolErrors {
    threadpoolInvalid       = -1,
    threadpoolLockFailure   = -2,
    threadpoolQueueFull     = -3,
    threadpoolShutdown      = -4,
    threadpoolThreadFailure = -5
} ThreadpoolError;

typedef struct ThreadpoolTask{
    void (*function)(void *);
    void *argument;
} ThreadpoolTask;

typedef struct Threadpool {
  pthread_t *threads;
  ThreadpoolTask *queue;
  int threadCount;
  int queueSize;
  int head;
  int tail;
  int count;
  int shutdown;
  int started;
} Threadpool;

// Lock and notify were made global for all functions to properly access
// and direct which functions were to act next
pthread_mutex_t lock;
pthread_cond_t notify;
int jobsSent;
int jobsReceived;

static void* threadpoolThread(void *threadpool);
int threadpoolFree(Threadpool *pool);
int threadpoolDestroy(Threadpool *pool);

/* Signal Handler for SIGINT */
void sigHandler(int sig_num) { 
    signal(SIGINT, sigHandler); 
    printf("Jobs Sent %d Jobs Received %d\n", jobsSent, jobsReceived); 
    fflush(stdout); 
}

/* Calculate the dot product and send message back to package */
void* DotProduct(void *arg) {
    // Get the Message Queue id
    ThreadData threadData = *((ThreadData *) arg);

    // Read in messages from package
    Msg message;
    if (msgrcv(threadData.msqid, &message, sizeof(Msg), 1, 0) < 0) {
        if (errno == EINTR);
        else {
            perror("msgrcv");
            exit(1);
        }
    }
    jobsReceived++;
    printf("Receiving job id %d type %lu size %lu\n", message.jobid, message.type, 
    (4 + (2 * message.innerDim)) * sizeof(int));

    // Calculate dot product
    // Matrix 1's vector is stored first in the data array
    int dotProduct = 0;
    for (int i = 0; i < (message.innerDim); i++) {
        int j = i+4;
        dotProduct += (message.data[i] * message.data[j]);
    }

    // If -n is specified, stop after outputting calculations
    if (threadData.outputOnly) {
        printf("Sum for cell [%d,%d] is %d\n", message.rowvec, message.colvec, dotProduct);
        return (void *)NULL;
    }

    // Update data array to hold the dot product
    message.data[0] = dotProduct;

    // Calculate size of message to send
    // 5 is for the 5 integers being sent over in the struct
    int size = 5 * sizeof(int);

    // Set message type to 2 to send back to package
    message.type = 2;

    // Send dot products back to package
    if (msgsnd(threadData.msqid, &message, size, IPC_NOWAIT) < 0) {
        perror("msgsnd");
        exit(1);
    }
    jobsSent++;
    printf("Sending job id %d type %lu size %d\n", message.jobid, message.type, size);
    return (void *)NULL;
}


/* Create and initialize a threadpool */
Threadpool* threadpoolCreate(int threadCount, int queueSize) {
    Threadpool *pool;
    int i;
    // Check if queue size and thread count are not exceeded or invalid
    if(threadCount <= 0 || threadCount > MAX_THREADS || queueSize <= 0 || queueSize > MAX_QUEUE) {
        printf("Error: threadpoolCreate: invalid/exceeded thread count or queue size\n");
        return NULL;
    }
    // Allocate space for the threadpool
    if((pool = (Threadpool *)malloc(sizeof(Threadpool))) == NULL) {
        printf("Error: threadpoolCreate: mallocating threadool\n");
        if (pool) {
            threadpoolFree(pool);
        }
        return NULL;
    }

    // Initialize the threadpool
    pool->threadCount = 0;
    pool->queueSize = queueSize;
    pool->head = 0;
    pool->tail = 0;
    pool->count = 0;
    pool->shutdown = 0;
    pool->started = 0;

    // Allocate space for a new thread and spot in the queue
    pool->threads = (pthread_t *)malloc(sizeof(pthread_t) * threadCount);
    pool->queue = (ThreadpoolTask *)malloc(sizeof(ThreadpoolTask) * queueSize);

    // Initialize mutex and conditional variables
    // Check for errors when initializing
    if((pthread_mutex_init(&lock, NULL) != 0) || (pthread_cond_init(&notify, NULL) != 0) ||
       (pool->threads == NULL) ||(pool->queue == NULL)) {
        printf("Error: threadpoolCreate: initializing cond variables and mutex\n");
        if(pool) {
            threadpoolFree(pool);
        }
        return NULL;
    }

    // Start the worker threads
    for(i = 0; i < threadCount; i++) {
        if(pthread_create(&(pool->threads[i]), NULL, threadpoolThread, (void*)pool) != 0) {
            printf("Error: threadpoolCreate: creating threads\n");
            threadpoolDestroy(pool);
            return NULL;
        }
        pool->threadCount++;
        pool->started++;
    }
    return pool;
}


/* Add tasks to the threadpool */
int threadpoolAdd(Threadpool *pool, void (*function)(void *), void *argument) {
    int errorValue = 0;
    int next;

    // Check if pool or function does not exist, returning an error
    if(pool == NULL || function == NULL) {
        printf("Error: threadpoolAdd: threadpool invalid\n");
        return threadpoolInvalid;
    }

    // Check for locking error
    if(pthread_mutex_lock(&lock) != 0) {
        printf("Error: threadpoolAdd: threadpool lock failure\n");
        return threadpoolLockFailure;
    }

    // Advance spot in the pool to grab next task
    next = (pool->tail + 1) % pool->queueSize;

    do {
        // Check if the threadpool queue is full
        if(pool->count == pool->queueSize) {
            printf("Error: threadpoolAdd: threadpool queue full\n");
            errorValue = threadpoolQueueFull;
            break;
        }
        // Check if the threadpool is shutting down
        if(pool->shutdown) {
            errorValue = threadpoolShutdown;
            printf("Error: threadpoolAdd: threadpool shutdown");
            break;
        }
        // Add the task onto the queue
        pool->queue[pool->tail].function = function;
        pool->queue[pool->tail].argument = argument;
        pool->tail = next;
        pool->count += 1;

        // Notify that the task has been added
        if(pthread_cond_signal(&notify) != 0) {
            printf("Error: threadpoolAdd: threadpool lock failure\n");
            errorValue = threadpoolLockFailure;
            break;
        }
    } while (0);

    if(pthread_mutex_unlock(&lock) != 0) {
        errorValue = threadpoolLockFailure;
        printf("Error: threadpoolAdd: threadpool lock failure\n");
    }

    return errorValue;
}


/* Destroy the threadpool when it's time to shutdown */
int threadpoolDestroy(Threadpool *pool) {
    int i = 0;
    int errorValue = 0;

    // Check if pool does not exist
    if(pool == NULL) {
        printf("Error: threadpoolDestroy: threadpool invalid\n");
        return threadpoolInvalid;
    }

    // Check for locking error
    if(pthread_mutex_lock(&lock) != 0) {
        return threadpoolLockFailure;
        printf("Error: threadpoolDestroy: threadpool lock failure\n");
    }

    do {
        // Check if the pool is already shutdown
        if(pool->shutdown) {
            errorValue = threadpoolShutdown;
            printf("Error: threadpoolDestroy: threadpool shutdown\n");
            break;
        }

        // Set shutdown flag
        pool->shutdown = 1;

        // Wake up all the worker threads
        if((pthread_cond_broadcast(&notify) != 0) ||
           (pthread_mutex_unlock(&lock) != 0)) {
            errorValue = threadpoolLockFailure;
            printf("Error: threadpoolDestroy: threadpool lock failure\n");
            break;
        }

        // Join all of the worker threads
        for (i = 0; i < pool->threadCount; i++) {
            if(pthread_join(pool->threads[i], NULL) != 0) {
                errorValue = threadpoolThreadFailure;
                printf("Error: threadpoolDestroy: threadpool thread failure\n");
            }
        }
    } while (0);

    // Deallocate the pool after checking to see everything went as planned
    if(!errorValue) {
        threadpoolFree(pool);
    }
    return errorValue;
}

/* Deallocate the threadpool */
int threadpoolFree(Threadpool *pool) {
    if(pool == NULL || pool->started > 0) {
        return -1;
    }

    // Free the space!!
    if(pool->threads) {
        free(pool->threads);
        free(pool->queue);
        pthread_mutex_lock(&lock);
        pthread_mutex_destroy(&lock);
        pthread_cond_destroy(&notify);
    }
    free(pool);    
    return 0;
}


/* Run the threads */
static void *threadpoolThread(void *threadpool) {
    Threadpool *pool = (Threadpool *)threadpool;
    ThreadpoolTask task;

    for(;;) {
        // Acquire lock before waiting on condition variable
        pthread_mutex_lock(&lock);

        // Wait on condition variable
        while((pool->count == 0) && (!pool->shutdown)) {
            pthread_cond_wait(&notify, &lock);
        }

        // Check for shutdown flag and empty pool
        if((pool->shutdown != 0) && (pool->count == 0)) {
            printf("threadpoolThread: shutting down or empty pool\n");
            break;
        }

        // Grab task from the pool
        task.function = pool->queue[pool->head].function;
        task.argument = pool->queue[pool->head].argument;
        pool->head = (pool->head + 1) % pool->queueSize;
        pool->count -= 1;

        // Unlock the mutex
        pthread_mutex_unlock(&lock);

        // Send to DotProduct
        (*(task.function))(task.argument);
    }

    pool->started--;

    pthread_mutex_unlock(&lock);
    pthread_exit(NULL);
    return(NULL);
}


int main(int argc, char* argv[]) {
    // Initialize job values
    jobsReceived = 0;
    jobsSent = 0;

    signal(SIGINT, sigHandler);
    
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

    int threadpoolSize = atoi(argv[1]);

    // Grab number of jobs from package
    // Done to reduce size allocated to the threadpool queue
    MatrixInfo matrixInfo;
    if (msgrcv(msqid, &matrixInfo, sizeof(MatrixInfo)*MAX_THREADS, 4, 0) < 0) {
        perror("msgrcv");
        exit(1);
    }
    
    ThreadData *threadData = malloc(sizeof(*threadData));

    // If -n is specified, just read and output calculations
    threadData->outputOnly = 0;     
    if (argc == 3) {
        threadData->outputOnly = 1;     
    }
    threadData->msqid = msqid;

    // Create thread pool to receive messages
    Threadpool pool = *threadpoolCreate(threadpoolSize, matrixInfo.jobs);

    for (int i = 0; i < matrixInfo.jobs; i++) {
        threadpoolAdd(&pool, (void*)&DotProduct, threadData);
    }
    threadpoolThread(&pool);

    free(threadData);
    threadpoolFree(&pool);

    return 0;

}

