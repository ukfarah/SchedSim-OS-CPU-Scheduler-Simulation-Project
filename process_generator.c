#include "headers.h"

#define MAX_PROCESSES 1000

processData processes[MAX_PROCESSES];
int processCount = 0;

static int msqid = -1;
static int clk_pid = -1;
static int sch_pid = -1;

int *shmaddr;

void clearResources(int signum)
{
    (void)signum;
    
    if (msqid != -1)
    {
        msgctl(msqid, IPC_RMID, NULL);
        msqid = -1;
    }
    
    destroyClk(false);
    exit(0);
}

int main(int argc, char * argv[])
{
    signal(SIGINT, clearResources);

    FILE *fp = fopen("processes.txt", "r");
    if (!fp)
    {
        perror("Failed to open processes.txt");
        exit(1);
    }

    char line[256];
    while (fgets(line, sizeof(line), fp))
    {
        if (line[0] == '#' || line[0] == '\n' || line[0] == '\r')
            continue;

        processData p;
        int fields = sscanf(line, "%d %d %d %d", 
                           &p.id, &p.arrivaltime, &p.runningtime, &p.priority);

        if (fields != 4)
        {
            fprintf(stderr, "Invalid line in processes.txt: %s", line);
            continue;
        }

        if (processCount >= MAX_PROCESSES)
        {
            fprintf(stderr, "Too many processes (max %d)\n", MAX_PROCESSES);
            break;
        }

        processes[processCount++] = p;
    }
    fclose(fp);

    if (processCount == 0)
    {
        printf("No processes to schedule. Exiting.\n");
        return 0;
    }

    for (int i = 0; i < processCount - 1; i++)
    {
        for (int j = i + 1; j < processCount; j++)
        {
            if (processes[i].arrivaltime > processes[j].arrivaltime)
            {
                processData temp = processes[i];
                processes[i] = processes[j];
                processes[j] = temp;
            }
        }
    }

    int algorithm;
    int quantum = 0;

    printf("Choose scheduling algorithm:\n");
    printf("1. HPF (Highest Priority First - Preemptive)\n");
    printf("2. SJN (Shortest Job Next - Non-preemptive)\n");
    printf("3. RR (Round Robin)\n");
    printf("Enter choice (1-3): ");
    fflush(stdout);
    scanf("%d", &algorithm);

    if (algorithm < 1 || algorithm > 3)
    {
        printf("Invalid choice, defaulting to HPF (1)\n");
        algorithm = 1;
    }

    if (algorithm == 3)
    {
        printf("Enter time quantum for Round Robin: ");
        fflush(stdout);
        scanf("%d", &quantum);
        if (quantum <= 0) quantum = 1;
    }

    key_t key = ftok("scheduler.c", MSG_KEY);
    if (key == -1)
    {
        perror("ftok failed");
        clearResources(SIGINT);
    }

    msqid = msgget(key, IPC_CREAT | 0666);
    if (msqid == -1)
    {
        perror("msgget failed in generator");
        clearResources(SIGINT);
    }

    clk_pid = fork();
    if (clk_pid == -1)
    {
        perror("fork clk");
        exit(1);
    }
    if (clk_pid == 0)
    {
        execl("./clk.out", "clk.out", NULL);
        perror("Failed to exec clk.out");
        exit(1);
    }

    sch_pid = fork();
    if (sch_pid == -1)
    {
        perror("fork scheduler");
        exit(1);
    }
    if (sch_pid == 0)
    {
        char algo_str[16], q_str[16];
        snprintf(algo_str, sizeof(algo_str), "%d", algorithm);
        snprintf(q_str, sizeof(q_str), "%d", quantum);

        execl("./scheduler.out", "scheduler.out", algo_str, q_str, NULL);
        perror("Failed to exec scheduler.out");
        exit(1);
    }

    sleep(1);
    initClk();

    printf("Process Generator: Clock and Scheduler started.\n");
    printf("Starting simulation at time %d\n", getClk());

    int nextProcessIdx = 0;
    int lastTime = -1;

    while (nextProcessIdx < processCount)
    {
        int currentTime = getClk();

        if (currentTime == lastTime)
        {
            usleep(2000);
            continue;
        }
        lastTime = currentTime;

        while (nextProcessIdx < processCount && 
               processes[nextProcessIdx].arrivaltime <= currentTime)
        {
            ProcessMessage msg;
            msg.mtype = 1;
            msg.data = processes[nextProcessIdx];

            if (msgsnd(msqid, &msg, sizeof(processData), 0) == -1)
            {
                perror("msgsnd failed");
                clearResources(SIGINT);
            }

            printf("At time %d: Sent process %d (arrival=%d, runtime=%d, priority=%d)\n",
                   currentTime, msg.data.id, msg.data.arrivaltime,
                   msg.data.runningtime, msg.data.priority);

            nextProcessIdx++;
        }
    }

    ProcessMessage endMsg;
    endMsg.mtype = 2;
    endMsg.data.id = -1;
    endMsg.data.arrivaltime = -1;
    endMsg.data.runningtime = -1;
    endMsg.data.priority = -1;

    if (msgsnd(msqid, &endMsg, sizeof(processData), 0) == -1)
    {
        perror("msgsnd termination message failed");
    }
    else
    {
        printf("At time %d: Sent termination signal to scheduler.\n", getClk());
    }

    int status;
    waitpid(sch_pid, &status, 0);
    printf("Scheduler has terminated. Generator exiting.\n");

    if (clk_pid > 0) 
        kill(clk_pid, SIGINT);

    clearResources(SIGINT);
    return 0;
}