#define _POSIX_C_SOURCE 200809L
#include <sys/select.h>
#include <unistd.h>
#include <sys/wait.h>
#include <stdlib.h>
#include <stdio.h>
#include <fcntl.h>
#include <string.h>
#include <errno.h>
#include <signal.h>

// Header files
#include "../include/process_log.h"
#define PROCESS_NAME "MAIN"
#include "../include/common.h"

static const char *process_names[] = {
    [IDX_B] = "Blackboard",
    [IDX_D] = "Drone",
    [IDX_I] = "Keyboard",
    [IDX_M] = "Map",
    [IDX_O] = "Obstacles",
    [IDX_T] = "Targets",
    [IDX_W] = "Watchdog"
};

pid_t pid_B, pid_D, pid_I, pid_M, pid_O, pid_T, pid_W;

void clean_children() {
    if (pid_W > 0) kill(pid_W, SIGTERM);
    if (pid_B > 0) kill(pid_B, SIGTERM);
    if (pid_D > 0) kill(pid_D, SIGTERM);
    if (pid_I > 0) kill(pid_I, SIGTERM);
    if (pid_M > 0) kill(pid_M, SIGTERM);
    if (pid_O > 0) kill(pid_O, SIGTERM);
    if (pid_T > 0) kill(pid_T, SIGTERM);
}

void handle_signal(int sig) {
    printf("\n[MAIN] Caught signal %d, terminating children...\n", sig);
    clean_children();
    
    // Wait for children to exit to avoid zombies
    while(wait(NULL) > 0);
    
    printf("[MAIN] All children terminated. Exiting.\n");
    exit(0);
}


int pipe_parent_to_child[NUM_PROCESSES][2]; // Child reads from [0], parent writes to [1]
int pipe_child_to_parent[NUM_PROCESSES][2]; // Parent reads from [0], child writes to [1]

// Route table structure
typedef struct{
    int num;
    int dest[NUM_PROCESSES];
} route_t;

int main(){
    LOG("Initialization");
    signal(SIGPIPE, SIG_IGN);
    
    struct sigaction sa;
    sa.sa_handler = handle_signal;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;
    sigaction(SIGINT, &sa, NULL);
    sigaction(SIGTERM, &sa, NULL);

    // Pipe creation
    for (int i = 0; i < NUM_PROCESSES; i++) {
        if (pipe(pipe_parent_to_child[i]) == -1 ||
            pipe(pipe_child_to_parent[i]) == -1) {
            perror("pipe");
            exit(EXIT_FAILURE);
        }
    }
    LOG("Pipe created");

    unlink("log/watchdog.log");
    unlink("log/processes_pid.log");
    unlink("log/system.log");

    unlink("log/system.log");

    char fd_pc[NUM_PROCESSES][16];
    char fd_cp[NUM_PROCESSES][16];

    // String conversion for file descriptors
    for (int i = 0; i < NUM_PROCESSES; i++) {
        sprintf(fd_pc[i], "%d", pipe_parent_to_child[i][0]); // Reading end
        sprintf(fd_cp[i], "%d", pipe_child_to_parent[i][1]); // Writing end
    }

    //WATCHDOG
    pid_W = fork();
    if (pid_W == 0) {
        // 
        for (int i = 0; i < NUM_PROCESSES; i++) {
            if (i != IDX_W) {
                close(pipe_parent_to_child[i][0]);
                close(pipe_parent_to_child[i][1]);
                close(pipe_child_to_parent[i][0]);
                close(pipe_child_to_parent[i][1]);
            }
        }

        close(pipe_parent_to_child[IDX_W][1]);
        close(pipe_child_to_parent[IDX_W][0]);
        execl("./build/Watchdog", "./build/Watchdog", fd_pc[IDX_W], fd_cp[IDX_W], NULL);
        perror("execl targets");
        exit(EXIT_FAILURE);
    }
    LOG("Watchdog fork");

    char watchdog_pid[16];
    sprintf(watchdog_pid, "%d", pid_W);

    //BLACKBOARD
    pid_B = fork();
    if (pid_B == 0) {
        // 
        for (int i = 0; i < NUM_PROCESSES; i++) {
            if (i != IDX_B) {
                close(pipe_parent_to_child[i][0]);
                close(pipe_parent_to_child[i][1]);
                close(pipe_child_to_parent[i][0]);
                close(pipe_child_to_parent[i][1]);
            }
        }
        close(pipe_parent_to_child[IDX_B][1]);
        close(pipe_child_to_parent[IDX_B][0]);

        // argv[1]=read_fd, argv[2]=write_fd
        execl("./build/Blackboard", "./build/Blackboard", fd_pc[IDX_B], fd_cp[IDX_B], watchdog_pid, NULL);
        perror("execl blackboard");
        _exit(EXIT_FAILURE);
    }
    LOG("Blackboard fork");

    //DRONE
    pid_D = fork();
    if (pid_D == 0){
        // 
        for (int i = 0; i < NUM_PROCESSES; i++) {
            if (i != IDX_D) {
                close(pipe_parent_to_child[i][0]);
                close(pipe_parent_to_child[i][1]);
                close(pipe_child_to_parent[i][0]);
                close(pipe_child_to_parent[i][1]);
            }
        }

        close(pipe_parent_to_child[IDX_D][1]);
        close(pipe_child_to_parent[IDX_D][0]);
        // 
        execl("./build/Drone", "./build/Drone", fd_pc[IDX_D], fd_cp[IDX_D], watchdog_pid, NULL);
        perror("execl drone");
        exit(EXIT_FAILURE);
    }
    LOG("Drone fork");

    //KEYBOARD
    pid_I = fork();
    if (pid_I == 0)  {
        for (int i = 0; i < NUM_PROCESSES; i++) {
            if (i != IDX_I) {
                close(pipe_parent_to_child[i][0]);
                close(pipe_parent_to_child[i][1]);
                close(pipe_child_to_parent[i][0]);
                close(pipe_child_to_parent[i][1]);
            }
        }
        
        close(pipe_parent_to_child[IDX_I][1]); // Keyboard doesn't write to parent->child
        close(pipe_child_to_parent[IDX_I][0]); // Keyboard doesn't read from child->parent

        //
        execlp("konsole", "konsole", "--geometry", "300x600", "-e", "./build/I_Keyboard",
            fd_pc[IDX_I], fd_cp[IDX_I], watchdog_pid, NULL);
        perror("execl keyboard");
        _exit(EXIT_FAILURE);
    }
    LOG("Keyboard fork");

    //MAP
    pid_M = fork();
    if (pid_M == 0) {
        for (int i = 0; i < NUM_PROCESSES; i++) {
            if (i != IDX_M) {
                close(pipe_parent_to_child[i][0]);
                close(pipe_parent_to_child[i][1]);
                close(pipe_child_to_parent[i][0]);
                close(pipe_child_to_parent[i][1]);
            }
        }
        // 
        close(pipe_parent_to_child[IDX_M][1]); // Child doesn't write to parent->child
        close(pipe_child_to_parent[IDX_M][0]); // Child doesn't read from child->parent

        execlp("konsole", "konsole", "--geometry", "1400x600", "-e", "./build/map", fd_pc[IDX_M], fd_cp[IDX_M], watchdog_pid, NULL);
        perror("execl konsole map");
        _exit(EXIT_FAILURE);
    }
    LOG("Map fork");

    //OBSTACLES
    pid_O = fork();
    if (pid_O == 0) {
        // 
        for (int i = 0; i < NUM_PROCESSES; i++) {
            if (i != IDX_O) {
                close(pipe_parent_to_child[i][0]);
                close(pipe_parent_to_child[i][1]);
                close(pipe_child_to_parent[i][0]);
                close(pipe_child_to_parent[i][1]);
            }
        }

        close(pipe_parent_to_child[IDX_O][1]);
        close(pipe_child_to_parent[IDX_O][0]);
        execl("./build/Obstacles", "./build/Obstacles", fd_pc[IDX_O], fd_cp[IDX_O], watchdog_pid, NULL);
        perror("execl obstacle");
        exit(EXIT_FAILURE);
    }
    LOG("Obstacles fork");

    //TARGETS
    pid_T = fork();
    if (pid_T == 0) {
        // 
        for (int i = 0; i < NUM_PROCESSES; i++) {
            if (i != IDX_T) {
                close(pipe_parent_to_child[i][0]);
                close(pipe_parent_to_child[i][1]);
                close(pipe_child_to_parent[i][0]);
                close(pipe_child_to_parent[i][1]);
            }
        }

        close(pipe_parent_to_child[IDX_T][1]);
        close(pipe_child_to_parent[IDX_T][0]);
        execl("./build/Targets", "./build/Targets", fd_pc[IDX_T], fd_cp[IDX_T], watchdog_pid, NULL);
        perror("execl targets");
        exit(EXIT_FAILURE);
    }
    LOG("Targets fork");

    // PARENT PROCESS MAIN LOOP
    for (int i = 0; i < NUM_PROCESSES; i++) {
        close(pipe_parent_to_child[i][0]);
        close(pipe_child_to_parent[i][1]);
    }

    route_t route_table[NUM_PROCESSES];
    for (int i = 0; i < NUM_PROCESSES; i++) route_table[i].num = 0;
    
    route_table[IDX_I].dest[route_table[IDX_I].num++] = IDX_B;  //I->BB
    route_table[IDX_B].dest[route_table[IDX_B].num++] = IDX_D;  //BB->D
    route_table[IDX_B].dest[route_table[IDX_B].num++] = IDX_M;  //BB->M
    route_table[IDX_D].dest[route_table[IDX_D].num++] = IDX_B;  //D->BB
    route_table[IDX_M].dest[route_table[IDX_M].num++] = IDX_B;  //M->BB
    route_table[IDX_B].dest[route_table[IDX_B].num++] = IDX_O;  //BB->O
    route_table[IDX_O].dest[route_table[IDX_O].num++] = IDX_B;  //O->BB
    route_table[IDX_B].dest[route_table[IDX_B].num++] = IDX_T;  //BB->T
    route_table[IDX_T].dest[route_table[IDX_T].num++] = IDX_B;  //T->BB

    LOG("Route table created");

    while (1) {
        LOG("waiting for a message to redirect");
        fd_set rfds;
        FD_ZERO(&rfds);

        int maxfd = -1;

        // The parent must monitor all read ends of child->parent pipes
        for (int i = 0; i < NUM_PROCESSES; i++) {
            int fd = pipe_child_to_parent[i][0];
            if (fd >= 0) {
                FD_SET(fd, &rfds);
                if (fd > maxfd) maxfd = fd;
            }
        }

        // Wait for a message from ANY child
        int ret = select(maxfd + 1, &rfds, NULL, NULL, NULL);
        if (ret < 0) {
            perror("select");
            break;
        }

        int esc_received = 0;
        // Check which child sent data
        for (int src = 0; src < NUM_PROCESSES; src++) {
            int read_fd = pipe_child_to_parent[src][0];

            if (FD_ISSET(read_fd, &rfds)) {
                struct msg m;
                int n = read(read_fd, &m, sizeof(m));

                if (n <= 0) {
                    // Child terminated -> close its reading end
                    close(read_fd);
                    pipe_child_to_parent[src][0] = -1;
                    continue;
                }

                if (strncmp(m.data, "ESC", 3) == 0){
                    printf("ESC RECEIVED\n");
                    esc_received = 1;
                }

                // Destination from route_table
                for (int d = 0; d < route_table[src].num; d++) {
                    int dst = route_table[src].dest[d];
                    int write_fd = pipe_parent_to_child[dst][1];

                    ssize_t w = write(write_fd, &m, sizeof(m));
                    if (w == -1) {
                        perror("write to child");
                        fprintf(stderr, "SIGPIPE likely on src=%d -> dst=%d fd=%d\n", src, dst, write_fd);
                    } else if (w != n) {
                        fprintf(stderr, "Partial write src=%d -> dst=%d fd=%d written=%zd expected=%d\n",
                            src, dst, write_fd, w, n);
                    }
                    char log_msg[128];
                    snprintf(log_msg, sizeof(log_msg), "Message redirected from %s to %s", process_names[src], process_names[dst]);
                    LOG(log_msg);
                }
                
            }
        }
        if (esc_received){
            LOG("ESC received, closing...");
            for (int i = 0; i < NUM_PROCESSES; i++) {
                int write_fd = pipe_parent_to_child[i][1];
                struct msg esc_msg;
                esc_msg.src = IDX_B;
                strncpy(esc_msg.data, "ESC", MSG_SIZE);
                write(write_fd, &esc_msg, sizeof(esc_msg));
                //printf("[MAIN] EXIT\n");
            }
            break;
        }
    }

    waitpid(pid_B, NULL, 0);
    waitpid(pid_D, NULL, 0);
    waitpid(pid_I, NULL, 0);
    waitpid(pid_M, NULL, 0);
    waitpid(pid_O, NULL, 0);
    waitpid(pid_T, NULL, 0);
    waitpid(pid_W, NULL, 0);
    LOG("Execution terminated correctly");
    return 0;
}