#define _POSIX_C_SOURCE 200809L
#define _DEFAULT_SOURCE
#include <signal.h>
#include <stdio.h>
#include <unistd.h>
#include <fcntl.h>
#include <string.h>
#include <errno.h>
#include <time.h>
#include <stdlib.h>

#include "../include/process_log.h"
#define PROCESS_NAME "WATCHDOG"
#include "../include/common.h"


typedef struct {
    pid_t pid;
    char name[32];
    time_t last_signal;
    int alive;
} process_status;

#define TIMEOUT 3

process_status process_table[NUM_PROCESSES];

static pid_t watchdog_pid;

void wait_for_all_processes(void) {
    int lines = 0;

    while (lines < NUM_PROCESSES) {
        lines = 0;

        FILE *f = fopen("log/processes_pid.log", "r");
        if (f) {
            char buf[256];
            while (fgets(buf, sizeof(buf), f))
                lines++;
            fclose(f);
        }
        usleep(100000); // 100ms
    }
}

int load_process(){
    printf("[WD] loading processes...\n");
    //fflush(stdout);

    FILE *f = fopen("log/processes_pid.log", "r");
    if (!f){
        perror("fopen processes_PID.log");
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
   update_process(pid, time);
}

void watchdog_log(){
    int fd = open("log/watchdog.log", O_WRONLY | O_CREAT | O_APPEND, 0644);
    if (fd == -1) {
        perror("open watchdog log");
        return;
    }

    // lock the file
    struct flock lock;
    memset(&lock, 0, sizeof(lock));
    lock.l_type = F_WRLCK;
    lock.l_whence = SEEK_SET;
    if (fcntl(fd, F_SETLKW, &lock) == -1) {
        perror("fcntl lock");
        close(fd);
        return;
    }

    // current time
    time_t now = time(NULL);
    struct tm *tm_info = localtime(&now);
    char time_str[16];
    strftime(time_str, sizeof(time_str), "%H:%M:%S", tm_info);

    // Write cycle header
    dprintf(fd, "%s WATCHDOG: check cycle\n", time_str);

    // Write the status of each process
    for (int i = 0; i < NUM_PROCESSES-1; i++) {
        const char *status = process_table[i].alive ? "ALIVE" : "DEAD";
        dprintf(fd, "%s %s PID=%d status=%s\n", time_str, process_table[i].name, process_table[i].pid, status);
    }

    // Flush
    fsync(fd);

    // Unlock the file
    lock.l_type = F_UNLCK;
    if (fcntl(fd, F_SETLK, &lock) == -1) {
        perror("fcntl unlock");
    }

    close(fd);
}

int main(int argc, char *argv[]) {
    watchdog_pid = getpid();
    //register the process on the file
    register_process("Watchdog");
    LOG("Watchdog process started");

    sigset_t block_set, old_set;

    // 1. Block SIGUSR1
    sigemptyset(&block_set);
    sigaddset(&block_set, SIGUSR1);
    sigprocmask(SIG_BLOCK, &block_set, &old_set);

    for (int i = 0; i < NUM_PROCESSES; i++) {
        process_table[i].pid = 0;
        process_table[i].alive = 0;
        process_table[i].name[0] = '\0';
    }

    //wait for the list of process to be complete
    wait_for_all_processes();
    //load all teh process to start monitoring
    load_process();
    LOG("All processes registered, starting monitoring");
    printf("[WD] All processes registered, starting monitoring\n");
    fflush(stdout);
    
    //signal handler definition
    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_flags = SA_SIGINFO;
    sa.sa_sigaction = signal_handler;
    sigemptyset(&sa.sa_mask);
    sigaction(SIGUSR1, &sa, NULL);

    sigprocmask(SIG_SETMASK, &old_set, NULL);

    struct timespec last_log_time;
    clock_gettime(CLOCK_MONOTONIC, &last_log_time);

    if (argc < 3) {
        fprintf(stderr, "Usage: %s <read_fd> <write_fd>\n", argv[0]);
        return 1;
    }

    // fd_in: read from parent/router
    int fd_in = atoi(argv[1]);
    fcntl(fd_in, F_SETFL, O_NONBLOCK);

    while (1) {
        time_t now = time(NULL);

        //check if all the process are alive
        for (int i = 0; i < NUM_PROCESSES; i++){
            if(now - process_table[i].last_signal > TIMEOUT){
                if (process_table[i].alive){
                    // Log the timeout
                    char log_msg[82];
                    snprintf(log_msg, sizeof(log_msg), "Alert: Process %s (PID %d) not responding!", process_table[i].name, process_table[i].pid);
                    LOG(log_msg);
                    process_table[i].alive = 0;
                }
            } else {
                process_table[i].alive = 1;
            }
        }
        
        // --- Log at most once per second ---
        struct timespec curr_time;
        clock_gettime(CLOCK_MONOTONIC, &curr_time);
        long diff_ms = (curr_time.tv_sec - last_log_time.tv_sec) * 1000 +
                       (curr_time.tv_nsec - last_log_time.tv_nsec) / 1000000;
        if (diff_ms >= 1000) {
            watchdog_log();
            last_log_time = curr_time;
        }

        //terminate the execution if the user pressed ESC
        struct msg m;
        ssize_t n = read(fd_in, &m, sizeof(m));
        if(n > 0){
            if (strncmp(m.data, "ESC", 3)== 0){
                printf("[WATCHDOG] EXIT\n");
                LOG("Watchdog received ESC, exiting");
                break;
            }
        }

        usleep(50000); // 50ms
    }
    close(fd_in);
    LOG("Watchdog terminated");
    return 0;
}

