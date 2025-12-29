#define _POSIX_C_SOURCE 200809L
#include <signal.h>
#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <sys/select.h>
#include <sys/wait.h>
#include <errno.h>
#include <string.h>
#include <math.h>
#include <fcntl.h>
#include <time.h>

#include "../include/process_log.h"
#define PROCESS_NAME "BLACKBOARD"
#include "../include/common.h"

#define MAX_OBS 100

struct blackboard {
    //Drone
    int drone_x;
    int drone_y;
    //obstacle
    int obs_x[MAX_OBS];
    int obs_y[MAX_OBS];
    int num_obs;
    //targets
    int tgs_x[MAX_OBS];
    int tgs_y[MAX_OBS];
    int num_tgs;
    //map
    int W, H;
    int running;
};


int main(int argc, char *argv[]) {
    
    //write on the processes.log
    register_process("Blackboard");

    int ni = 1.0;
    double d0 = 5.0;

    int tmp_obs_x[MAX_OBS];
    int tmp_obs_y[MAX_OBS];
    int tmp_num_obs = 0;
    
    int received_obs = 0;

    int tmp_tgs_x[MAX_OBS];
    int tmp_tgs_y[MAX_OBS];
    int tmp_num_tgs = 0;

    int received_tgs = 0;

    int received_resize = 0;

    if(argc < 3){
        fprintf(stderr, "Usage: %s <read_fd> <write_fd>\n", argv[0]);
        exit(EXIT_FAILURE);
    }

    // fd_in: read from father
    int fd_in = atoi(argv[1]);

    // fd_out: write to father
    int fd_out = atoi(argv[2]);

    // watchdog pid to send signals
    pid_t watchdog_pid = atoi(argv[3]);

    struct blackboard bb = {0, 0, {0}, {0}, 0, {0}, {0}, 0, 190, 30, 1};
    int expected_obs = (int)roundf(bb.H*bb.W/1000);
    int expected_tgs = (int)roundf(bb.H*bb.W/1000);

    while (bb.running) {
        fd_set fds;
        FD_ZERO(&fds);
        FD_SET(fd_in, &fds);
        int max_fd = fd_in + 1;

        struct timeval tv = {0, 50000};
        int ready = select(max_fd, &fds, NULL, NULL, &tv);

        if (ready < 0) {
            if (errno == EINTR) continue; // interrupt handler
            perror("select");
            break;
        }

        //READ FROM ROUTER
        if (FD_ISSET(fd_in, &fds)){
            //read the msg
            struct msg m;
            ssize_t n = read(fd_in, &m, sizeof(m));

            //if it is from the keyboard
            if (m.src == IDX_I) {
                //printf("[I->BB] Ricevuto codice: %d\n", m.data[0]);

                // send the msg to the drone
                if (write(fd_out, &m, sizeof(m)) < 0) {
                    perror("write to drone via router");
                }
                // ESC 
                if (m.data[0] == 27){
                    printf("[BB] EXIT\n");
                    snprintf(m.data, MSG_SIZE, "ESC");
                    if(write(fd_out, &m, sizeof(m)) < 0){
                        perror("write to map via router");
                    } 
                    bb.running = 0;
                }
            }
            //if it is from the drone
            else if(m.src == IDX_D){
                sscanf(m.data, "%d,%d", &bb.drone_x, &bb.drone_y);
                //printf("[D->BB] Ricevuto codice: %s\n", m.data);

                struct msg map_msg;
                map_msg.src = IDX_B;
                snprintf(map_msg.data, MSG_SIZE, "D=%d,%d", bb.drone_x,bb.drone_y); 
                if(write(fd_out, &map_msg, sizeof(map_msg)) < 0){
                    perror("write to map via router");
                }
            }
            //if it is from the map
            else if (m.src == IDX_M && strncmp(m.data, "RESIZE", 6) == 0){
                
                sscanf(m.data, "RESIZE %d %d", &bb.W, &bb.H);
                //printf("[M->BB] RESIZE ricevuto %d, %d\n", bb.W, bb.H);
                
                //printf("RESET");
                
                expected_obs = (int)roundf(bb.H*bb.W/1000);
                struct msg obs_msg = m;
                obs_msg.src = IDX_B;
                if(write(fd_out, &obs_msg, sizeof(obs_msg))< 0){
                    perror("write to obstacles and targets via router");
                }  
            }
            //from the obstacles 
            else if(m.src == IDX_O){ 
                if (strncmp(m.data, "RESET", 5) == 0){ 
                    
                    tmp_num_obs = 0;
                    //receiving_obs = 1;
                    printf("[BB] RESET obstacles ricevuto\n");
                    continue;
                } 

                if (tmp_num_obs < expected_obs){
                    int x, y; 
                    sscanf(m.data, "%d,%d", &x, &y);
                
                    tmp_obs_x[tmp_num_obs] = x;
                    tmp_obs_y[tmp_num_obs] = y;
                    tmp_num_obs++;
                    //printf("[BB] obstacle position: %d,%d; n%d\n", x, y, tmp_num_obs);
                }

                //printf("[BB] exp obs = %d, tmp_obs = %d \n", expected_obs, tmp_num_obs);
                if (tmp_num_obs == expected_obs){

                    //bb.num_obs = 0;
                    struct msg map_msg;
                    map_msg.src = IDX_B;

                    snprintf(map_msg.data, MSG_SIZE, "STOP_O");
                    write(fd_out, &map_msg, sizeof(map_msg));
                    //printf("[BB->O] STOP INVIATO\n");

                    for(int i=0; i<tmp_num_obs; i++) {
                        bb.obs_x[i] = tmp_obs_x[i];
                        bb.obs_y[i] = tmp_obs_y[i];
                        bb.num_obs++;

                        snprintf(map_msg.data, MSG_SIZE, "O=%d,%d",  bb.obs_x[i], bb.obs_y[i]);
                        write(fd_out, &map_msg, sizeof(map_msg));
                    }

                    //printf("[BB] obs = %d\n", expected_obs);  
                    snprintf(map_msg.data, MSG_SIZE, "REDRAW_O"); 
                    write(fd_out, &map_msg, sizeof(map_msg)); 
                    //printf("[BB] REDRAW_O\n"); 
                } 
            }
            //from the targets
            else if (m.src == IDX_T) {
                if (strncmp(m.data, "RESET", 5) == 0) {
                    tmp_num_tgs = 0;
                    //receiving_tgs = 1;
                    printf("[BB] RESET targets ricevuto\n");
                    continue;
                }

                int x, y;
                sscanf(m.data, "%d,%d", &x, &y);

                expected_tgs = (int)roundf(bb.H * bb.W / 1000);

                if (tmp_num_tgs < expected_tgs) {
                    tmp_tgs_x[tmp_num_tgs] = x;
                    tmp_tgs_y[tmp_num_tgs] = y;
                    tmp_num_tgs++;

                    //printf("[BB] target position: %d,%d; n%d\n", x, y, tmp_num_tgs);
                }

                //printf("[BB] exp tgs = %d, tmp_tgs = %d\n", expected_tgs, tmp_num_tgs);

                // Se ho ricevuto abbastanza target → aggiorno BB e notifico la mappa
                if (tmp_num_tgs == expected_tgs) {

                    struct msg map_msg;
                    map_msg.src = IDX_B;

                    // Blocca il generatore di targets
                    snprintf(map_msg.data, MSG_SIZE, "STOP_T");
                    write(fd_out, &map_msg, sizeof(map_msg));

                    //printf("[BB->T] STOP INVIATO\n");

                    // Salvo target nella BB e li mando alla mappa
                    bb.num_tgs = 0;
                    for (int i = 0; i < tmp_num_tgs; i++) {
                        bb.tgs_x[i] = tmp_tgs_x[i];
                        bb.tgs_y[i] = tmp_tgs_y[i];
                        bb.num_tgs++;

                        snprintf(map_msg.data, MSG_SIZE, "T=%d,%d",
                                bb.tgs_x[i], bb.tgs_y[i]);
                        write(fd_out, &map_msg, sizeof(map_msg));
                    }

                    // Richiedi ridisegno target
                    snprintf(map_msg.data, MSG_SIZE, "REDRAW_T");
                    write(fd_out, &map_msg, sizeof(map_msg));
                    //printf("[BB] REDRAW_T\n");
                }
            }

            for (int i = 0; i < bb.num_obs; i++){
                int dx = (bb.drone_x - bb.obs_x[i]);
                int dy = (bb.drone_y - bb.obs_y[i]);

                double dis = sqrt(dx*dx + dy*dy);
                if (dis <= d0 && dis > 0.0){
                    struct msg msg_f;
                    msg_f.src = IDX_B;

                    snprintf(msg_f.data, MSG_SIZE, "OBS_POS= %d,%d", dx, dy);
                    write(fd_out, &msg_f, sizeof(msg_f));
                    //printf("OBSTACLE NEAR\n");
                }
            }
            
        }
        //signal the watchdog
        union sigval val;
        val.sival_int = time(NULL);
        
        //printf("[BB] signals: IM ALIVE\n");
        sigqueue(watchdog_pid, SIGUSR1, val);
        usleep(50000); 
    }
    
    close(fd_in);
    close(fd_out);
    return 0;
}