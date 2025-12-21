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
    time_t last_seen;
    int last_area;
} process_status;

#define NUM_PROCESSES 7
enum { IDX_B = 0, IDX_D, IDX_I, IDX_M, IDX_O, IDX_T, IDX_W};

process_status process_table[NUM_PROCESSES];

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

void update_process(pid_t pid, int area){
    for (int i = 0; i < NUM_PROCESSES; i++){
        if(process_table[i].pid == pid){
            process_table[i].last_seen = time(NULL);
            process_table[i].last_area = area;
            return;
        }
    }
}

void signal_handler(int sig, siginfo_t *info, void *context){
   pid_t pid = info->si_pid;
   int area = info->si_value.sival_int;
   printf("heartbeat recived, pid: %d\n", pid);
   //update_process(pid, area);
}


int main(int argc, char *argv[]) {
    
    //write on the processes.log
    register_process("Watchdog");

    //struct to handle signals
    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));

    //more information of the signal
    sa.sa_flags = SA_SIGINFO;
    //set the handler for SIGUSR1
    sa.sa_sigaction = signal_handler;

    //multiple signals allowed
    sigemptyset(&sa.sa_mask);

    if(sigaction(SIGUSR1, &sa, NULL) == -1){
        perror("sigaction");
    }

    while (1) {
        pause();
    }

    return 0;
}

