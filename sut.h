#ifndef __SUT_H__
#define __SUT_H__
#include "a1_lib.h"
#include <stdbool.h>
#include <ucontext.h>
#include <pthread.h>

#define MAX_THREADS                        32
#define BUFSIZE                            1024
#define THREAD_STACK_SIZE                  1024*64

typedef struct {
    int threadId;
    char *threadStack;
    void *threadFunc;
    ucontext_t threadContext;
} USER_THREAD;

typedef struct {
    char *socket_dest;
    int socket_port;
    int socket_id;
    int socket_fd;
} SOCKET_IO;

typedef struct {
    char *write_buf;
    int write_size;
} WRITE_INFO;

typedef void (*sut_task_f)();

void sut_init();
bool sut_create(void (*f_newT)());
void sut_yield();
void sut_exit();
void sut_open(char *dest, int port);
void sut_write(char *buf, int size);
void sut_close();
char *sut_read();
void sut_shutdown();

#endif
