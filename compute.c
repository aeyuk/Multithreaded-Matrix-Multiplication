#include <stdio.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#include <stdlib.h>
#include <pthread.h>
#include <sys/ipc.h>
#include <sys/msg.h>
#include <string.h>

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
  pthread_mutex_t lock;
  pthread_cond_t notify;
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


static void* threadpoolThread(void *threadpool);
int threadpoolFree(Threadpool *pool);
int threadpoolDestroy(Threadpool *pool);


/* Calculate the dot product and send message back to package */
void* DotProduct(void *arg) {
    // Get the Message Queue id
    ThreadData threadData = *((ThreadData *) arg);

    // Read in messages from package
    Msg message;
    if (msgrcv(threadData.msqid, &message, sizeof(Msg), 1, 0) < 0) {
        perror("msgrcv");
        exit(1);
    }
    printf("Receiving job id %d type %lu size %lu\n", message.jobid, message.type, 
    (4 + (2 * message.innerDim)) * sizeof(int));

    // Calculate dot product
    // Matrix 1's vector is stored first in the data array
    int dotProduct = 0;
    for (int i = 0; i < (message.innerDim); i++) {
        int j = i+4;
        dotProduct += (message.data[i] * message.data[j]);
    }

    // Print output of calculations
    printf("Sum for cell [%d,%d] is %d\n", message.rowvec, message.colvec, dotProduct);

    // If -n is specified, stop after outputting calculations
    if (threadData.outputOnly) {
        return (void *)NULL;
    }

    // Update data array to hold the dot product
    message.data[0] = dotProduct;

    // Set message type to 2 to send back to package
    message.type = 2;

    // Calculate size of message to send
    // 5 is for the 5 integers being sent over in the struct
    int size = 5 * sizeof(int);

    // Send dot products back to package
    if (msgsnd(threadData.msqid, &message, size, IPC_NOWAIT) < 0) {
        perror("msgsnd");
        exit(1);
    }
    return (void *)NULL;
}


/* Create and initialize a threadpool */
Threadpool* threadpoolCreate(int threadCount, int queueSize)
{
    Threadpool *pool;
    int i;
    // Check if queue size and thread count are not exceeded or invalid
    if(threadCount <= 0 || threadCount > MAX_THREADS || queueSize <= 0 || queueSize > MAX_QUEUE) {
        return NULL;
    }
    // Allocate space for the threadpool
    if((pool = (Threadpool *)malloc(sizeof(Threadpool))) == NULL) {
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
    if((pthread_mutex_init(&(pool->lock), NULL) != 0) || (pthread_cond_init(&(pool->notify), NULL) != 0) ||
       (pool->threads == NULL) ||(pool->queue == NULL)) {
        if(pool) {
            threadpoolFree(pool);
        }
        return NULL;
    }

    // Start the worker threads
    for(i = 0; i < threadCount; i++) {
        if(pthread_create(&(pool->threads[i]), NULL, threadpoolThread, (void*)pool) != 0) {
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
        return threadpoolInvalid;
    }

    // Check for locking error
    if(pthread_mutex_lock(&(pool->lock)) != 0) {
        return threadpoolLockFailure;
    }

    // Advance spot in the pool to grab next task
    next = (pool->tail + 1) % pool->queueSize;

    while (0) {
        // Check if the threadpool queue is full
        if(pool->count == pool->queueSize) {
            errorValue = threadpoolQueueFull;
            break;
        }
        // Check if the threadpool is shutting down
        if(pool->shutdown) {
            errorValue = threadpoolShutdown;
            break;
        }
        // Add the task onto the queue
        pool->queue[pool->tail].function = function;
        pool->queue[pool->tail].argument = argument;
        pool->tail = next;
        pool->count += 1;

        // Notify that the task has been added
        if(pthread_cond_signal(&(pool->notify)) != 0) {
            errorValue = threadpoolLockFailure;
            break;
        }
    }

    if(pthread_mutex_unlock(&pool->lock) != 0) {
        errorValue = threadpoolLockFailure;
    }

    return errorValue;
}


/* Destroy the threadpool when it's time to shutdown */
int threadpoolDestroy(Threadpool *pool)
{
    int i = 0;
    int errorValue = 0;

    // Check if pool does not exist
    if(pool == NULL) {
        return threadpoolInvalid;
    }

    // Check for locking error
    if(pthread_mutex_lock(&(pool->lock)) != 0) {
        return threadpoolLockFailure;
    }

    while(0) {
        // Check if the pool is already shutdown
        if(pool->shutdown) {
            errorValue = threadpoolShutdown;
            break;
        }

        // Set shutdown flag
        pool->shutdown = 1;

        // Wake up all the worker threads
        if((pthread_cond_broadcast(&(pool->notify)) != 0) ||
           (pthread_mutex_unlock(&(pool->lock)) != 0)) {
            errorValue = threadpoolLockFailure;
            break;
        }

        // Join all of the worker threads
        for (i = 0; i < pool->threadCount; i++) {
            if(pthread_join(pool->threads[i], NULL) != 0) {
                errorValue = threadpoolThreadFailure;
            }
        }
    }

    // Deallocate the pool after checking to see everything went as planned
    if(!errorValue) {
        threadpoolFree(pool);
    }
    return errorValue;
}

/* Deallocate the threadpool */
int threadpoolFree(Threadpool *pool)
{
    if(pool == NULL || pool->started > 0) {
        return -1;
    }

    // Free the space!!
    if(pool->threads) {
        free(pool->threads);
        free(pool->queue);
        pthread_mutex_lock(&(pool->lock));
        pthread_mutex_destroy(&(pool->lock));
        pthread_cond_destroy(&(pool->notify));
    }
    free(pool);    
    return 0;
}


/* Run the threads */
static void *threadpoolThread(void *threadpool)
{
    Threadpool *pool = (Threadpool *)threadpool;
    ThreadpoolTask task;

    for(;;) {
        // Acquire lock before waiting on condition variable
        pthread_mutex_lock(&(pool->lock));

        // Wait on condition variable
        while((pool->count == 0) && (!pool->shutdown)) {
            pthread_cond_wait(&(pool->notify), &(pool->lock));
        }

        // Check for shutdown flag and empty pool
        if((pool->shutdown != 0) && (pool->count == 0)) {
            break;
        }

        // Grab task from the pool
        task.function = pool->queue[pool->head].function;
        task.argument = pool->queue[pool->head].argument;
        pool->head = (pool->head + 1) % pool->queueSize;
        pool->count -= 1;

        // Unlock the mutex
        pthread_mutex_unlock(&(pool->lock));

        // Do the thing
        (*(task.function))(task.argument);
    }

    pool->started--;

    pthread_mutex_unlock(&(pool->lock));
    pthread_exit(NULL);
    return(NULL);
}


int main(int argc, char* argv[]) {
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

    // Grab number of jobs from package
    MatrixInfo matrixInfo;
    if (msgrcv(msqid, &matrixInfo, sizeof(MatrixInfo), 1, 0) < 0) {
        perror("msgrcv");
        exit(1);
    }

    int threadpoolSize = atoi(argv[1]);
    printf("%d\n", threadpoolSize);
    
    ThreadData *threadData = malloc(sizeof(*threadData));

    // If -n is specified, just read and output calculations
    threadData->outputOnly = 0;     
    if (argc == 2) {
        threadData->outputOnly = 1;     
    }
    threadData->msqid = msqid;

    // pthread_t threads[matrixInfo.jobs];
    // for (int i = 0; i < matrixInfo.jobs; i++) {
    //     pthread_create(&threads[i], NULL, &DotProduct, threadData);    
    // }
    // for (int i = 0; i < matrixInfo.jobs; i++) {
    //     pthread_join(threads[i], NULL);
    // }

    // Create thread pool to receive messages
    Threadpool pool = *threadpoolCreate(matrixInfo.jobs, threadpoolSize);
    for (int i = 0; i < matrixInfo.jobs; i++) {
        threadpoolAdd(&pool, (void*)DotProduct, &threadData);   
    }    

    free(threadData);

    return 0;

}

