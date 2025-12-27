#define _POSIX_C_SOURCE 200809L
#include <signal.h>
#include <stdio.h>
#include <unistd.h>
#include <fcntl.h>
#include <string.h>
#include <errno.h>
#include <time.h>

typedef struct {
    pid_t pid;
    char name[32];
    time_t last_signal;
    int alive;
} process_status;

#define TIMEOUT 3
#define NUM_PROCESSES 7

enum { IDX_B = 0, IDX_D, IDX_I, IDX_M, IDX_O, IDX_T, IDX_W};

process_status process_table[NUM_PROCESSES];

static pid_t watchdog_pid;

void register_process(const char *name){
    int fd = open("processes.log", O_WRONLY | O_CREAT | O_APPEND, 0644);
    if (fd == -1){
        perror("open process file");
        return;
    }
    struct flock lock;
    memset(&lock, 0, sizeof(lock));

    lock.l_type = F_WRLCK;
    lock.l_whence = SEEK_SET;

    if (fcntl(fd, F_SETLKW, &lock) == -1){
        perror("fcntl lock");
        close(fd);
        return;
    }

    char buffer[128];
    int len = snprintf(buffer, sizeof(buffer), "%s PID=%d\n", name, getpid());

    if (write(fd, buffer, len) == -1){
        perror("write process file");
    }

    //flush
    fsync(fd);

    //unlock
    lock.l_type = F_UNLCK;
    if (fcntl(fd, F_SETLK, &lock) == -1){
        perror("fcntl unlock");
    }

    close(fd);
}

void wait_for_all_processes(void) {
    int lines = 0;

    while (lines < NUM_PROCESSES) {
        lines = 0;

        FILE *f = fopen("processes.log", "r");
        if (f) {
            char buf[256];
            while (fgets(buf, sizeof(buf), f))
                lines++;
                printf("[WD] lines=%d\n", lines);
            fclose(f);
        }
        usleep(100000); // 100ms
    }
}

int load_process(){
    printf("[WD] loading processes...\n");
    //fflush(stdout);

    FILE *f = fopen("processes.log", "r");
    if (!f){
        perror("fopen processes.log");
        return 0;
    }
    char name[64];
    pid_t pid;
    int idx = 0;

    char line[128];
    while (fgets(line, sizeof(line), f)) {
        if (sscanf(line, "%63s PID=%d", name, &pid) == 2) {
            if (pid == watchdog_pid)
                continue;
            if (idx >= NUM_PROCESSES) break;

            strncpy(process_table[idx].name, name, sizeof(process_table[idx].name)-1);
            process_table[idx].name[sizeof(process_table[idx].name)-1] = '\0';
            process_table[idx].pid = pid;
            process_table[idx].last_signal = time(NULL); 
            process_table[idx].alive = 1;

            printf("%s PID=%d registered\n", process_table[idx].name, process_table[idx].pid);
            //fflush(stdout);
            idx++;
        }
    }
    fclose(f);

    return idx;
}

void update_process(pid_t pid, int time){
    for (int i = 0; i < NUM_PROCESSES; i++){
        if(process_table[i].pid == pid){
            process_table[i].last_signal = time;
            if (process_table[i].alive == 0){
                process_table[i].alive = 1;
                printf("%s PID=%d is alive\n", process_table[i].name, process_table[i].pid);
            }
            
            return;
        }
    }
}

void signal_handler(int sig, siginfo_t *info, void *context){
   pid_t pid = info->si_pid;
   int time = info->si_value.sival_int;
   //printf("heartbeat recived, pid: %d\n", pid);
   update_process(pid, time);
}


int main(int argc, char *argv[]) {
    watchdog_pid = getpid();
    sigset_t block_set, old_set;

    // 1. Blocca SIGUSR1
    sigemptyset(&block_set);
    sigaddset(&block_set, SIGUSR1);
    sigprocmask(SIG_BLOCK, &block_set, &old_set);

    //write on the processes.log
    register_process("Watchdog");
    //printf("[WD] wrote\n");

    printf("[WD] waiting for all processes...\n");
    wait_for_all_processes();
    load_process();

    printf("[WD] All process registered\n");

    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_flags = SA_SIGINFO;
    sa.sa_sigaction = signal_handler;
    sigemptyset(&sa.sa_mask);
    sigaction(SIGUSR1, &sa, NULL);

    // 6. Sblocca SIGUSR1 (i segnali pendenti arrivano ora)
    sigprocmask(SIG_SETMASK, &old_set, NULL);

    while (1) {
        sleep(1);

        time_t now = time(NULL);

        for (int i = 0; i < NUM_PROCESSES; i++){
            if(now - process_table[i].last_signal > TIMEOUT){
                if (process_table[i].alive){
                    printf("ALERT: process %s (PID %d) not responding!\n",
                            process_table[i].name, process_table[i].pid);
                    process_table[i].alive = 0;        
                }
            } else {
                process_table[i].alive = 1;
            }
        }
    }

    return 0;
}

