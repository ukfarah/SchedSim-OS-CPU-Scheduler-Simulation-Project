#include "headers.h"

static FILE *g_log_fp = NULL;

static void fmt_2dec_trim(double val, char *out, size_t out_sz)
{
    double r = round(val * 100.0) / 100.0;
    snprintf(out, out_sz, "%.2f", r);

    size_t n = strlen(out);
    while (n > 0 && out[n - 1] == '0')
    {
        out[n - 1] = '\0';
        n--;
    }
    if (n > 0 && out[n - 1] == '.')
        out[n - 1] = '\0';
}

static void logger_init(const char *log_filename)
{
    g_log_fp = fopen(log_filename, "w");
    if (!g_log_fp)
    {
        perror("fopen scheduler.log");
        exit(1);
    }
    fprintf(g_log_fp, "#At time x process y state arr w total z remain y wait k\n");
    fflush(g_log_fp);
}

static void logger_close(void)
{
    if (g_log_fp) 
    { 
        fclose(g_log_fp); 
        g_log_fp = NULL; 
    }
}

typedef struct PerfStats
{
    int first_arrival_time;
    int last_finish_time;
    int busy_time;
    int n_finished;
    double sum_wta;
    double sum_wta_sq;
    double sum_wait;
} PerfStats;

static void perf_init(PerfStats *ps)
{
    memset(ps, 0, sizeof(*ps));
    ps->first_arrival_time = -1;
}

static void perf_on_process_finished(PerfStats *ps, int arrival, int runtime, int finish_time)
{
    int TA = finish_time - arrival;
    double WTA = (runtime > 0) ? ((double)TA / (double)runtime) : 0.0;
    int waiting = TA - runtime;

    if (ps->first_arrival_time == -1 || arrival < ps->first_arrival_time)
        ps->first_arrival_time = arrival;
    if (finish_time > ps->last_finish_time)
        ps->last_finish_time = finish_time;

    ps->busy_time += runtime;
    ps->n_finished++;
    ps->sum_wta += WTA;
    ps->sum_wta_sq += (WTA * WTA);
    ps->sum_wait += (double)waiting;
}

static void perf_write_file(const PerfStats *ps, const char *perf_filename)
{
    FILE *fp = fopen(perf_filename, "w");
    if (!fp)
    {
        perror("fopen scheduler.perf");
        exit(1);
    }

    double avg_wta = 0.0, avg_wait = 0.0, std_wta = 0.0;
    double cpu_util = 0.0;

    if (ps->n_finished > 0)
    {
        avg_wta = ps->sum_wta / (double)ps->n_finished;
        avg_wait = ps->sum_wait / (double)ps->n_finished;

        double ex2 = ps->sum_wta_sq / (double)ps->n_finished;
        double var = ex2 - (avg_wta * avg_wta);
        if (var < 0) var = 0;
        std_wta = sqrt(var);

        int total_window = ps->last_finish_time - ps->first_arrival_time;
        if (total_window <= 0) 
            cpu_util = 100.0;
        else 
            cpu_util = ((double)ps->busy_time / (double)total_window) * 100.0;
    }

    char util_str[32], avgwta_str[32], avgwait_str[32], std_str[32];
    fmt_2dec_trim(cpu_util, util_str, sizeof(util_str));
    fmt_2dec_trim(avg_wta, avgwta_str, sizeof(avgwta_str));
    fmt_2dec_trim(avg_wait, avgwait_str, sizeof(avgwait_str));
    fmt_2dec_trim(std_wta, std_str, sizeof(std_str));

    fprintf(fp, "CPU utilization = %s%%\n", util_str);
    fprintf(fp, "Avg WTA = %s\n", avgwta_str);
    fprintf(fp, "Avg Waiting = %s\n", avgwait_str);
    fprintf(fp, "Std WTA = %s\n", std_str);

    fclose(fp);
}

typedef enum {
    READY = 0,
    RUNNING,
    STOPPED
} ProcState;

typedef struct PCB {
    int id;
    int pid;
    int arrival;
    int total_runtime;
    int remaining_time;
    int priority;
    int waiting_time;

    int last_start_time;
    int last_ready_time;

    ProcState state;
} PCB;

#define QMAX 10000
static PCB* rr_q[QMAX];
static int rr_h=0, rr_t=0;
static int rr_empty(void){ return rr_h==rr_t; }
static void rr_push(PCB* p){ rr_q[rr_t%QMAX]=p; rr_t++; }
static PCB* rr_pop(void){ PCB* p=rr_q[rr_h%QMAX]; rr_h++; return p; }

typedef int (*cmp_fn)(const PCB*, const PCB*);
typedef struct {
    PCB* a[QMAX];
    int sz;
    cmp_fn cmp;
} PQ;

static void pq_init(PQ* pq, cmp_fn cmp){ pq->sz=0; pq->cmp=cmp; }
static int pq_empty(PQ* pq){ return pq->sz==0; }
static void pq_swap(PCB**x, PCB**y){ PCB*t=*x; *x=*y; *y=t; }

static void pq_push(PQ* pq, PCB* p){
    int i=pq->sz++;
    pq->a[i]=p;
    while(i>0){
        int par=(i-1)/2;
        if(pq->cmp(pq->a[i], pq->a[par])>=0) break;
        pq_swap(&pq->a[i], &pq->a[par]);
        i=par;
    }
}

static PCB* pq_pop(PQ* pq){
    if(pq->sz==0) return NULL;
    PCB* top=pq->a[0];
    pq->sz--;
    pq->a[0]=pq->a[pq->sz];
    int i=0;
    while(1){
        int l=2*i+1, r=2*i+2, s=i;
        if(l<pq->sz && pq->cmp(pq->a[l], pq->a[s])<0) s=l;
        if(r<pq->sz && pq->cmp(pq->a[r], pq->a[s])<0) s=r;
        if(s==i) break;
        pq_swap(&pq->a[i], &pq->a[s]);
        i=s;
    }
    return top;
}

static int cmp_sjn(const PCB* x, const PCB* y){
    if(x->total_runtime != y->total_runtime) 
        return x->total_runtime - y->total_runtime;
    if(x->arrival != y->arrival) 
        return x->arrival - y->arrival;
    return x->id - y->id;
}

static int cmp_hpf(const PCB* x, const PCB* y){
    if(x->priority != y->priority) 
        return x->priority - y->priority;
    if(x->arrival != y->arrival) 
        return x->arrival - y->arrival;
    return x->id - y->id;
}

int *shmaddr;
static int msqid=-1;
static int algo=0;
static int quantum=0;

static volatile sig_atomic_t child_finished_flag=0;

static PCB* current=NULL;
static int current_quantum_start=-1;

static PQ ready_pq;
static PerfStats perf;

static void on_child_finished(int sig){
    (void)sig;
    child_finished_flag=1;
}

static void update_running_remaining(int now){
    if(!current) return;
    if(current->state != RUNNING) return;
    int ran = now - current->last_start_time;
    if(ran > 0){
        current->remaining_time -= ran;
        if(current->remaining_time < 0) 
            current->remaining_time = 0;
        current->last_start_time = now;
    }
}

static void log_state(int now, PCB* p, const char* state){
    if(!g_log_fp || !p) return;

    if(strcmp(state, "finished") != 0){
        fprintf(g_log_fp,
            "At time %d process %d %s arr %d total %d remain %d wait %d\n",
            now, p->id, state, p->arrival, p->total_runtime, 
            p->remaining_time, p->waiting_time);
    }else{
        int TA = now - p->arrival;
        double WTA = (p->total_runtime>0)? 
                     ((double)TA/(double)p->total_runtime) : 0.0;
        char wta_str[32];
        fmt_2dec_trim(WTA, wta_str, sizeof(wta_str));

        fprintf(g_log_fp,
            "At time %d process %d finished arr %d total %d remain %d wait %d TA %d WTA %s\n",
            now, p->id, p->arrival, p->total_runtime, 0, 
            p->waiting_time, TA, wta_str);
    }
    fflush(g_log_fp);
}

static void start_new_process(PCB* p, int now){
    int pid=fork();
    if(pid==-1){ 
        perror("fork"); 
        exit(1); 
    }
    if(pid==0){
        char id_str[16], rt_str[16];
        snprintf(id_str, sizeof(id_str), "%d", p->id);
        snprintf(rt_str, sizeof(rt_str), "%d", p->total_runtime);
        execl("./process.out", "process.out", id_str, rt_str, (char*)NULL);
        perror("execl process.out");
        exit(1);
    }
    p->pid=pid;
    p->state=RUNNING;
    p->last_start_time=now;
    current_quantum_start=now;
}

static void resume_process(PCB* p, int now){
    kill(p->pid, SIGCONT);
    p->state=RUNNING;
    p->last_start_time=now;
    current_quantum_start=now;
}

static void stop_process(PCB* p, int now){
    (void)now;
    kill(p->pid, SIGSTOP);
    p->state=STOPPED;
}

static void push_ready(PCB* p, int now){
    p->state = READY;
    p->last_ready_time = now;

    if(algo == 3) 
        rr_push(p);
    else 
        pq_push(&ready_pq, p);
}

static PCB* pop_ready(void){
    if(algo == 3){
        if(rr_empty()) return NULL;
        return rr_pop();
    }else{
        return pq_pop(&ready_pq);
    }
}

static int ready_empty(void){
    if(algo == 3) 
        return rr_empty();
    return pq_empty(&ready_pq);
}

static PCB* peek_best_ready(void){
    if(algo == 3){
        if(rr_empty()) return NULL;
        return rr_q[rr_h % QMAX];
    }else{
        if(pq_empty(&ready_pq)) return NULL;
        return ready_pq.a[0];
    }
}

static PCB* SJN_Scheduler(PCB* currentProcess) {
    if (currentProcess != NULL && currentProcess->remaining_time > 0) {
        return currentProcess;
    }
    
    if (ready_empty()) {
        return NULL;
    }
    
    return pop_ready();
}

static PCB* HPF_Scheduler(PCB* currentProcess) {
    if (ready_empty() && currentProcess == NULL) {
        return NULL;
    }
    
    PCB* bestCandidate = peek_best_ready();
    
    if (currentProcess == NULL) {
        return pop_ready();
    }
    
    if (bestCandidate != NULL) {
        if (bestCandidate->priority < currentProcess->priority) {
            return pop_ready();
        }
    }
    
    return currentProcess;
}

static PCB* RR_Scheduler(PCB* currentProcess) {
    if (currentProcess == NULL && ready_empty()) {
        return NULL;
    }
    
    if (currentProcess == NULL) {
        return pop_ready();
    }
    
    return currentProcess;
}

static void handle_arrivals(int now, int *generator_done){
    ProcessMessage msg;
    
    while(1){
        int r = msgrcv(msqid, &msg, sizeof(processData), 0, IPC_NOWAIT);
        if(r==-1){
            if(errno==ENOMSG) break;
            perror("msgrcv");
            exit(1);
        }

        if(msg.mtype==2 || msg.data.id == -1){
            *generator_done=1;
            continue;
        }

        PCB* p = (PCB*)malloc(sizeof(PCB));
        if(!p){ 
            perror("malloc"); 
            exit(1); 
        }
        p->id = msg.data.id;
        p->pid = 0;
        p->arrival = msg.data.arrivaltime;
        p->total_runtime = msg.data.runningtime;
        p->remaining_time = msg.data.runningtime;
        p->priority = msg.data.priority;
        p->waiting_time = 0;
        p->last_start_time = now;
        p->last_ready_time = now;
        p->state = READY;

        push_ready(p, now);

        if(algo == 1 && current && current->state==RUNNING){
            PCB* decision = HPF_Scheduler(current);
            
            if(decision != current){
                update_running_remaining(now);
                
                stop_process(current, now);
                log_state(now, current, "stopped");
                push_ready(current, now);
                
                current = NULL;
            }
        }
    }
}

static void dispatch_if_idle(int now){
    if(current != NULL) return;
    if(ready_empty()) return;

    PCB* next_proc = NULL;
    
    if(algo == 1) {
        next_proc = HPF_Scheduler(NULL);
    } 
    else if(algo == 2) {
        next_proc = SJN_Scheduler(NULL);
    }
    else if(algo == 3) {
        next_proc = RR_Scheduler(NULL);
    }
    
    if(!next_proc) return;
    
    current = next_proc;
    
    current->waiting_time += (now - current->last_ready_time);

    if(current->pid == 0){
        start_new_process(current, now);
        log_state(now, current, "started");
    }else{
        resume_process(current, now);
        log_state(now, current, "resumed");
    }
}

static void rr_check_quantum(int now){
    if(algo != 3) return;
    if(!current) return;
    if(current->state != RUNNING) return;

    update_running_remaining(now);
    int ran_q = now - current_quantum_start;
    
    if(quantum > 0 && ran_q >= quantum && current->remaining_time > 0){
        stop_process(current, now);
        log_state(now, current, "stopped");
        push_ready(current, now);
        current = NULL;
    }
}

static void check_preemption(int now){
    if(!current) return;
    if(current->state != RUNNING) return;
    
    PCB* next_proc = NULL;
    
    if(algo == 1) {
        next_proc = HPF_Scheduler(current);
    } 
    else if(algo == 2) {
        next_proc = SJN_Scheduler(current);
    }
    else if(algo == 3) {
        next_proc = RR_Scheduler(current);
    }
    
    if(next_proc != NULL && next_proc != current) {
        update_running_remaining(now);
        
        stop_process(current, now);
        log_state(now, current, "stopped");
        push_ready(current, now);
        
        current = next_proc;
        current->waiting_time += (now - current->last_ready_time);
        
        if(current->pid == 0) {
            start_new_process(current, now);
            log_state(now, current, "started");
        } else {
            resume_process(current, now);
            log_state(now, current, "resumed");
        }
    }
}

int main(int argc, char *argv[])
{
    algo = (argc >= 2) ? atoi(argv[1]) : 1;
    quantum = (argc >= 3) ? atoi(argv[2]) : 0;

    if(algo == 2) 
        pq_init(&ready_pq, cmp_sjn);
    else if(algo == 1) 
        pq_init(&ready_pq, cmp_hpf);
    else 
        pq_init(&ready_pq, cmp_hpf);

    signal(SIGUSR1, on_child_finished);

    key_t key = ftok("scheduler.c", MSG_KEY);
    msqid = msgget(key, 0666);
    if(msqid == -1){
        perror("msgget scheduler");
        exit(1);
    }

    initClk();

    logger_init("scheduler.log");
    perf_init(&perf);

    int generator_done = 0;
    int last_tick = -1;

    while(1){
        int now = getClk();
        
        if(now == last_tick){
            usleep(2000);
            continue;
        }
        last_tick = now;

        handle_arrivals(now, &generator_done);

        if(child_finished_flag && current){
            update_running_remaining(now);

            int status;
            while(waitpid(-1, &status, WNOHANG) > 0) { }

            current->remaining_time = 0;

            log_state(now, current, "finished");
            perf_on_process_finished(&perf, current->arrival, 
                                    current->total_runtime, now);

            free(current);
            current = NULL;
            child_finished_flag = 0;
        }

        rr_check_quantum(now);

        dispatch_if_idle(now);

        if(generator_done && current==NULL && ready_empty())
            break;
    }

    perf_write_file(&perf, "scheduler.perf");
    logger_close();

    if(msqid != -1)
        msgctl(msqid, IPC_RMID, NULL);

    destroyClk(false);
    return 0;
}