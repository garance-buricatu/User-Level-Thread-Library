#include "sut.h"
#include "queue.h"
#include "a1_lib.h"
#include <stddef.h>
#include <stdlib.h>
#include <stdio.h>
#include <sys/queue.h>
#include <ucontext.h>
#include <unistd.h>
#include <stdbool.h>
#include <pthread.h>
#include <sys/ioctl.h>

pthread_t C_EXEC_handler;
pthread_t I_EXEC_handler;

pthread_mutex_t myMutex = PTHREAD_MUTEX_INITIALIZER;

struct queue ready_queue;

SOCKET_IO *mySocket;
int sockfd;
int socket_ready_flag = 0;

char read_here_buff[BUFSIZE];
int read_from_socket = 0;
int read_complete = 0;

WRITE_INFO *writeOperation;
int write_to_socket = 0;

ucontext_t c_exec_context; 
ucontext_t main_context;

USER_THREAD threadarr[MAX_THREADS];
int threadarr_counter = 0;
int numthreads;

int exit_counter = 0;

//lastly dequeued task by c_exec, ie. running task
USER_THREAD *running;
// running task blocked by sut_open() or sut_read()
USER_THREAD *running_blocked;

void *C_EXEC(){
 
    getcontext(&c_exec_context);
    
    usleep(10000); // wait for threads to be created before starting to loop through ready_queue

    while (true){
        struct queue_entry *ptr = queue_pop_head(&ready_queue);
        ucontext_t *prev_context = &main_context; // set previous context as main context
        usleep(100000);
        
        while(ptr){  
            running = ptr->data; // dequeued thread is currently running thread
            swapcontext(prev_context, &(running->threadContext)); //start running dequeued thread

            // dequeue next element
            ptr = queue_pop_head(&ready_queue);
        }
    }
}

void *I_EXEC(){

    while(true){
        if (socket_ready_flag){
            socket_ready_flag = 0;
            // https://www.tutorialspoint.com/unix_sockets/socket_server_example.htm
            if (connect_to_server(mySocket->socket_dest, mySocket->socket_port, &(mySocket->socket_fd)) < 0){
                fprintf(stderr, "BAD SOCKET CONNECTION\n");
            }
            else {
                 // reschedule blocked task  
                pthread_mutex_lock(&myMutex);
                struct queue_entry *node = queue_new_node(running_blocked);
                queue_insert_tail(&ready_queue, node);
                pthread_mutex_unlock(&myMutex);
            }
        }

        if (read_from_socket){
            read_from_socket = 0;
            int len_read = 0;
            // see how much data is avaliable on the server - store number of bytes in "len"
            ioctl(mySocket->socket_fd, FIONREAD, &len_read);

            if (len_read > 0){
                // reads len bytes from server and stores result in read_here_buff
                len_read = recv_message(mySocket->socket_fd, read_here_buff, len_read);
            }
            if (len_read < 0){
                fprintf(stderr,"ERROR READING FROM SOCKET");
            }
            else{
                // signal sut_read() to retrive buffer written to by the socket
                read_complete = 1;

                // reschedule blocked task
                pthread_mutex_lock(&myMutex);
                struct queue_entry *node = queue_new_node(running_blocked);
                queue_insert_tail(&ready_queue, node);
                pthread_mutex_unlock(&myMutex);

            }
        }
        if (write_to_socket){
            write_to_socket = 0;
            int len_write = send_message(mySocket->socket_fd, writeOperation->write_buf, writeOperation->write_size);
            if (len_write < 0){
                fprintf(stderr,"ERROR WRITING FROM SOCKET");
            }
        }
    }
}

void sut_init() {

    getcontext(&main_context);

    pthread_create(&C_EXEC_handler, NULL, C_EXEC, 0);
    pthread_create(&I_EXEC_handler, NULL, I_EXEC, 0);

    mySocket = (SOCKET_IO *)malloc(sizeof(SOCKET_IO)); //initialize mySocket global variable, id = -1 since no thread is associated with the socket yet
    mySocket->socket_id = -1;

    ready_queue = queue_create();
    queue_init(&ready_queue);

    // used in sut_create() - keep track of number of threads created
    numthreads = 0;
}

bool sut_create( void (threadfunc)() ){

    if (numthreads >= 16) 
	{
		fprintf(stderr, "FATAL: Maximum thread limit reached... creation failed! \n");
        return false;
	}

    USER_THREAD *newT = (USER_THREAD *)malloc(sizeof(USER_THREAD));
    getcontext(&(newT->threadContext));
    newT->threadId = numthreads;
    newT->threadStack = (char *)malloc(THREAD_STACK_SIZE);
    newT->threadContext.uc_stack.ss_sp = newT->threadStack;
    newT->threadContext.uc_stack.ss_size = THREAD_STACK_SIZE;
	newT->threadContext.uc_link = &c_exec_context;
	newT->threadContext.uc_stack.ss_flags = 0;
	newT->threadFunc = threadfunc;
    makecontext(&(newT->threadContext), threadfunc, 0);
    
    numthreads++;

    //add new task to array - will prevent data from getting overwritten
    threadarr[threadarr_counter] = *newT;

    // add new thread to ready queue
    pthread_mutex_lock(&myMutex);
    struct queue_entry *node = queue_new_node(&(threadarr[threadarr_counter]));
    queue_insert_tail(&ready_queue, node);
    pthread_mutex_unlock(&myMutex);

    threadarr_counter++;

    return true;
}

void sut_yield(){
    // save context of currently running task into new node and add it to end of ready queue
    pthread_mutex_lock(&myMutex);
    struct queue_entry *node = queue_new_node(running);
    queue_insert_tail(&ready_queue, node);
    pthread_mutex_unlock(&myMutex);

    // c_exec schedules next avaliable task
    swapcontext(&(running->threadContext), &c_exec_context);
}

void sut_exit(){
    exit_counter++;

    if (exit_counter == threadarr_counter){
        // last task created is calling exit - shutdown is ready
        printf("SHUTTING DOWN\n");
        pthread_cancel(C_EXEC_handler);
        pthread_cancel(I_EXEC_handler);
        exit(0);
    }
    else{
        // c_exec schedules next avaliable task
        swapcontext(&(running->threadContext), &c_exec_context);
    }
}

void sut_open(char *dest, int port){

    //copy running task in running_blocked
    running_blocked = (USER_THREAD *)malloc(sizeof(USER_THREAD));
    (*running_blocked).threadId = running->threadId;
    (*running_blocked).threadStack = running->threadStack;
    (*running_blocked).threadFunc = running->threadFunc;
    (*running_blocked).threadContext = running->threadContext;

    // initialize socket struct, which holds connection information
    mySocket->socket_dest = dest;
    mySocket->socket_port = port;
    mySocket->socket_id = running_blocked->threadId;
    mySocket->socket_fd = sockfd;

    // notify I_EXEC that connection is ready to be established
    socket_ready_flag = 1;

    // c_exec schedules next avaliable task
    swapcontext(&(running_blocked->threadContext), &c_exec_context); 
}

char *sut_read(){
    if (mySocket->socket_id != running->threadId){
        printf("MUST CALL sut_open() BEFORE CALLING sut_read()\n");
    }
    else{
        // copy state / context of task that called sut_read
        (*running_blocked).threadId = running->threadId;
        (*running_blocked).threadStack = running->threadStack;
        (*running_blocked).threadFunc = running->threadFunc;
        (*running_blocked).threadContext = running->threadContext;

        //notify I_EXEC to start reading from socket
        read_from_socket = 1;

    }
    // c_exec schedules next avaliable task
    swapcontext(&(running_blocked->threadContext), &c_exec_context); 

    // waits for I_EXEC to complete read operation - return result
    while(true){
        if (read_complete){
            read_complete = 0;
            return read_here_buff;
        }
    }
}

void sut_write(char *buf, int size){
    if (mySocket->socket_id != running->threadId){
        printf("MUST CALL sut_open() BEFORE CALLING sut_write()\n");
    }
    else{
        writeOperation = (WRITE_INFO *)malloc(sizeof(WRITE_INFO));
        writeOperation->write_buf = buf;
        writeOperation->write_size = size;

        write_to_socket = 1;

        // insert the task that called write (ie. running task) to front of the ready_queue
        pthread_mutex_lock(&myMutex);
        struct queue_entry *node = queue_new_node(running);
        queue_insert_head(&ready_queue, node);
        pthread_mutex_unlock(&myMutex);

        // c_exec schedules next avaliable task
        swapcontext(&(running->threadContext), &c_exec_context);
    }
}

void sut_close(){
    if (mySocket->socket_id != running->threadId){
        printf("MUST CALL sut_open() BEFORE CALLING sut_close()\n");
    }
    else {
        mySocket->socket_id = -1;
        close(mySocket->socket_fd);

        // insert the task that called close (ie. running task) to front of the ready_queue
        pthread_mutex_lock(&myMutex);
        struct queue_entry *node = queue_new_node(running);
        queue_insert_head(&ready_queue, node);
        pthread_mutex_unlock(&myMutex);

        // c_exec schedules next avaliable task
        swapcontext(&(running->threadContext), &c_exec_context);
    }
}

void sut_shutdown(){
    pthread_join(C_EXEC_handler, NULL);
    pthread_join(I_EXEC_handler, NULL);
}
