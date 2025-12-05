#include <sys/select.h>
#include <unistd.h>
#include <sys/wait.h>
#include <stdlib.h>
#include <stdio.h>
#include <fcntl.h>
#include <string.h>
#include <errno.h>
#include <signal.h>

#define NUM_PROCESSES 6
#define MSG_SIZE 64

enum { IDX_B = 0, IDX_D, IDX_I, IDX_M, IDX_O, IDX_T};

struct msg {
    int src;
    char data[MSG_SIZE];
};

int pipe_parent_to_child[NUM_PROCESSES][2]; //the child reads from [0], the father write on [1]
int pipe_child_to_parent[NUM_PROCESSES][2]; //the father reads from [0], the child write on [1]

//route table
typedef struct{
    int num;
    int dest[NUM_PROCESSES];
} route_t;

int main(){

    signal(SIGPIPE, SIG_IGN);

    //pipe creation
    for (int i = 0; i < NUM_PROCESSES; i++) {
        if (pipe(pipe_parent_to_child[i]) == -1 ||
            pipe(pipe_child_to_parent[i]) == -1) {
            perror("pipe");
            exit(EXIT_FAILURE);
        }
    }

    
    pid_t pid_B, pid_D, pid_I, pid_M, pid_O, pid_T;

    char fd_pc[NUM_PROCESSES][16];
    char fd_cp[NUM_PROCESSES][16];

    // string conversion
    for (int i = 0; i < NUM_PROCESSES; i++) {
        sprintf(fd_pc[i], "%d", pipe_parent_to_child[i][0]); // lettura
        sprintf(fd_cp[i], "%d", pipe_child_to_parent[i][1]); // scrittura
    }

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
        execl("./Blackboard", "./Blackboard", fd_pc[IDX_B], fd_cp[IDX_B], NULL);
        perror("execl blackboard");
        _exit(EXIT_FAILURE);
    }

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
        execl("./Drone", "./Drone", fd_pc[IDX_D], fd_cp[IDX_D], NULL);
        perror("execl drone");
        exit(EXIT_FAILURE);
    }

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
        
        close(pipe_parent_to_child[IDX_I][1]); // non scrive su parent->child
        close(pipe_child_to_parent[IDX_I][0]); // non legge da child->parent

        //
        execlp("konsole", "konsole", "--geometry", "300x600", "-e", "./I_Keyboard",
            fd_pc[IDX_I], fd_cp[IDX_I], NULL);
        perror("execl keyboard");
        _exit(EXIT_FAILURE);
    }

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
        close(pipe_parent_to_child[IDX_M][1]); // child non deve scrivere su p->c
        close(pipe_child_to_parent[IDX_M][0]); // child non legge da c->p
        //close(pipe_child_to_parent[IDX_M][1]); // child non scrive al padre

        execlp("konsole", "konsole", "--geometry", "1400x600", "-e", "./map", fd_pc[IDX_M], fd_cp[IDX_M], NULL);
        perror("execl konsole map");
        _exit(EXIT_FAILURE);
    }

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
        execl("./Obstacles", "./Obstacles", fd_pc[IDX_O], fd_cp[IDX_O], NULL);
        perror("execl obstacle");
        exit(EXIT_FAILURE);
    }

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
        execl("./Targets", "./Targets", fd_pc[IDX_T], fd_cp[IDX_T], NULL);
        perror("execl targets");
        exit(EXIT_FAILURE);
    }
    
    // FATHER BLOCK
    for (int i = 0; i < NUM_PROCESSES; i++) {
        close(pipe_parent_to_child[i][0]);
        //close(pipe_parent_to_child[i][1]);
        //close(pipe_child_to_parent[i][0]);
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


    while (1) {
        fd_set rfds;
        FD_ZERO(&rfds);

        int maxfd = -1;

        // il padre deve monitorare tutti i read end dei child->parent
        for (int i = 0; i < NUM_PROCESSES; i++) {
            int fd = pipe_child_to_parent[i][0];
            if (fd >= 0){
                FD_SET(fd, &rfds);
                if (fd > maxfd) maxfd = fd;
            }
        }

        // aspetta un messaggio da QUALUNQUE figlio
        int ret = select(maxfd + 1, &rfds, NULL, NULL, NULL);
        if (ret < 0) {
            perror("select");
            break;
        }

        int esc_received = 0;
        // verifica quale figlio ha inviato dati
        for (int src = 0; src < NUM_PROCESSES; src++) {
            int read_fd = pipe_child_to_parent[src][0];

            if (FD_ISSET(read_fd, &rfds)) {
                struct msg m;
                int n = read(read_fd, &m, sizeof(m));

                if (n <= 0) {
                    // il figlio è terminato → chiudi solo il suo fd di lettura
                    close(read_fd);
                    pipe_child_to_parent[src][0] = -1;
                    continue;
                }

                if (strncmp(m.data, "ESC", 3) == 0){
                    printf("ESC RECEIVED");
                    esc_received = 1;
                }

                //destination from route_table
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
                }
            }
        }
        if (esc_received){
            for (int i = 0; i < NUM_PROCESSES; i++) {
                int write_fd = pipe_parent_to_child[i][1];
                struct { char data[4]; } esc_msg;
                strncpy(esc_msg.data, "ESC", 4);
                write(write_fd, &esc_msg, 4);
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
    
    return 0;
}