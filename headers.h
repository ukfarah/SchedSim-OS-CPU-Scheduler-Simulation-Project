#ifndef HEADERS_H
#define HEADERS_H

#include <stdio.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/file.h>
#include <sys/ipc.h>
#include <sys/shm.h>
#include <sys/sem.h>
#include <sys/msg.h>
#include <sys/wait.h>
#include <stdlib.h>
#include <unistd.h>
#include <signal.h>
#include <string.h>
#include <errno.h>
#include <math.h>

typedef short bool;
#define true 1
#define false 0

#define SHKEY 300

#define MSG_KEY 7777

typedef struct processData
{
    int id;
    int arrivaltime;
    int runningtime;
    int priority;
} processData;

typedef struct ProcessMessage
{
    long mtype;
    processData data;
} ProcessMessage;

extern int *shmaddr;

static inline int getClk()
{
    return *shmaddr;
}

static inline void initClk()
{
    int shmid = shmget(SHKEY, 4, 0444);
    while (shmid == -1)
    {
        printf("Wait! The clock not initialized yet!\n");
        sleep(1);
        shmid = shmget(SHKEY, 4, 0444);
    }
    shmaddr = (int *) shmat(shmid, (void *)0, 0);
}

static inline void destroyClk(bool terminateAll)
{
    shmdt(shmaddr);
    if (terminateAll)
    {
        killpg(getpgrp(), SIGINT);
    }
}

#endif