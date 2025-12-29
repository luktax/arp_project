#define _POSIX_C_SOURCE 200809L
#include <signal.h>
#include <unistd.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <string.h>
#include <math.h>
#include <fcntl.h>

#include "../include/process_log.h"
#define PROCESS_NAME "TARGETS"
#include "../include/common.h"


int main(int argc, char *argv[]) {

    //write on the processes.log
    register_process("Targets");

    if (argc < 3) {
        fprintf(stderr, "Usage: %s <fd>\n", argv[0]);
        return 1;
    }

    int fd_in = atoi(argv[1]);
    int fd_out = atoi(argv[2]);
    // watchdog pid to send signals
    pid_t watchdog_pid = atoi(argv[3]);

    fcntl(fd_in, F_SETFL, O_NONBLOCK);

    int W = 190, H = 30;

    srand(time(NULL)^ getpid());

    struct msg bb_msg;
    bb_msg.src = IDX_T;

    int window_changed = 1;
    int reset_sent = 0;

    while(1){
        struct msg m;
        ssize_t n = read(fd_in, &m, sizeof(m));
        

        if (n > 0) {
            if (m.src == IDX_B){
                if (strncmp(m.data, "RESIZE", 6)==0){
                    int newW, newH;
                    sscanf(m.data, "RESIZE %d %d", &newW, &newH);
                    printf("[BB->T] RESIZE ricevuto %d, %d\n", W, H);
                    if (newW != W || newH != H){
                        W = newW;
                        H = newH;
                        window_changed = 1;
                    }
                }
                if (strncmp(m.data, "STOP_T", 6) == 0){
                    window_changed = 0;
                    //printf("[T] STOP\n");
                }
            }
            if (strncmp(m.data, "ESC", 3)== 0){
                printf("[TARGETS] EXIT\n");
                break;
            }
        }

        bb_msg.data[0] = '\0';

        if(window_changed){
            if (!reset_sent){
                snprintf(bb_msg.data, MSG_SIZE, "RESET"); 
                write(fd_out, &bb_msg, sizeof(bb_msg)); 
                reset_sent = 1;
                printf("[T] RESET INVIATO\n");
            }

            int x = (rand() % (W-2)) + 1;
            int y = (rand() % (H-2)) + 1;

            snprintf(bb_msg.data, MSG_SIZE, "%d,%d", x, y);
            write(fd_out, &bb_msg, sizeof(bb_msg));

            printf("[T] posizione target %d, %d\n", x, y);
        } else {
            reset_sent = 0;
        }

        //signals the watchdog
        union sigval val;
        val.sival_int = time(NULL);
        
        //printf("[T] signals: IM ALIVE\n");
        sigqueue(watchdog_pid, SIGUSR1, val);

        usleep(50000);
    }
}