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
    // Drone state
    int drone_x;
    int drone_y;
    // Obstacles state
    int obs_x[MAX_OBS];
    int obs_y[MAX_OBS];
    int num_obs;
    // Targets state
    int tgs_x[MAX_OBS];
    int tgs_y[MAX_OBS];
    int num_tgs;
    // Map state
    int W, H;
    int running;
};


int main(int argc, char *argv[]) {
    
    // Register process for logging
    register_process("Blackboard");
    LOG("Blackboard process started");

    double d0 = 5.0;
    int waiting_reply = 0;

    int tmp_obs_x[MAX_OBS];
    int tmp_obs_y[MAX_OBS];
    int tmp_num_obs = 0;
    
    int received_obs = 0;

    int tmp_tgs_x[MAX_OBS];
    int tmp_tgs_y[MAX_OBS];
    int tmp_num_tgs = 0;

    int received_tg = 0;

    int received_resize = 0;

    if(argc < 3){
        fprintf(stderr, "Usage: %s <read_fd> <write_fd>\n", argv[0]);
        exit(EXIT_FAILURE);
    }

    // fd_in: read from parent/router
    int fd_in = atoi(argv[1]);

    // fd_out: write to father
    int fd_out = atoi(argv[2]);

    // Watchdog PID to send alive signals
    pid_t watchdog_pid = atoi(argv[3]);
    
    struct blackboard bb = {0, 0, {0}, {0}, 0, {0}, {0}, 0, 155, 30, 1};
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

        // READ FROM ROUTER/PARENT
        if (FD_ISSET(fd_in, &fds)){
            // Read incoming message
            struct msg m;
            ssize_t n = read(fd_in, &m, sizeof(m));

            // Message from Keyboard (I)
            if (m.src == IDX_I) {
                // Forward the message to the drone
                if (write(fd_out, &m, sizeof(m)) < 0) {
                    perror("write to drone via router");
                }
                // ESC 
                if (m.data[0] == 27){
                    printf("[BB] EXIT\n");
                    LOG("Received ESC from Keyboard, shutting down");
                    snprintf(m.data, MSG_SIZE, "ESC");
                    if(write(fd_out, &m, sizeof(m)) < 0){
                        perror("write to map via router");
                    } 
                    bb.running = 0;
                }
            }
            // Message from Drone (D)
            else if(m.src == IDX_D){
                // Check if it's a STATS message first
                if (strncmp(m.data, "STATS", 5) == 0) {
                     // Forward STATS to Map
                    struct msg map_msg = m;
                    map_msg.src = IDX_B; // Mark as coming from Blackboard forwarding
                    if(write(fd_out, &map_msg, sizeof(map_msg)) < 0){
                        perror("write to map forwarding stats");
                    }
                } else {
                    // Assume it's a position update
                    sscanf(m.data, "%d,%d", &bb.drone_x, &bb.drone_y);
                    {
                        char log_msg[64];
                        snprintf(log_msg, sizeof(log_msg), "Received drone position: %d,%d", bb.drone_x, bb.drone_y);
                        LOG(log_msg);
                    } 
                    //printf("[D->BB] Ricevuto codice: %s\n", m.data);

                    struct msg map_msg;
                    map_msg.src = IDX_B;
                    snprintf(map_msg.data, MSG_SIZE, "D=%d,%d", bb.drone_x,bb.drone_y); 
                    if(write(fd_out, &map_msg, sizeof(map_msg)) < 0){
                        perror("write to map via router");
                    }
                }
            }
            // Message from Map (M)
            else if (m.src == IDX_M && strncmp(m.data, "RESIZE", 6) == 0){
                
                sscanf(m.data, "RESIZE %d %d", &bb.W, &bb.H);
                //printf("[M->BB] RESIZE ricevuto %d, %d\n", bb.W, bb.H);
                {
                    char log_msg[64];
                    snprintf(log_msg, sizeof(log_msg), "Forwarding RESIZE to Obstacles and Targets: %dx%d", bb.W, bb.H);
                    LOG(log_msg);
                }
                
                //printf("RESET");
                
                expected_obs = (int)roundf(bb.H*bb.W/1000);
                expected_tgs = (int)roundf(bb.H*bb.W/1000);
                tmp_num_obs = 0;
                tmp_num_tgs = 0;

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
                    LOG("Obstacles reset received");
                    continue;
                } 
                else if (strncmp(m.data, "NEW", 3) == 0){
                    int x, y;
                    sscanf(m.data, "NEW: %d,%d", &x, &y);
                    if (bb.num_obs > 0) {
                        // Shift internal obstacle list
                        for (int i = 0; i < bb.num_obs - 1; i++) {
                            bb.obs_x[i] = bb.obs_x[i + 1];
                            bb.obs_y[i] = bb.obs_y[i + 1];
                        }
                        bb.obs_x[bb.num_obs - 1] = x;
                        bb.obs_y[bb.num_obs - 1] = y;

                        struct msg map_msg;
                        map_msg.src = IDX_B;
                        snprintf(map_msg.data, MSG_SIZE, "O_SHIFT=%d,%d", x, y);
                        write(fd_out, &map_msg, sizeof(map_msg));
                        
                        snprintf(map_msg.data, MSG_SIZE, "REDRAW_O");
                        write(fd_out, &map_msg, sizeof(map_msg));
                        LOG("Shifted obstacle list and notified Map");
                    }
                }

                if (tmp_num_obs < expected_obs){
                    int x, y; 
                    sscanf(m.data, "%d,%d", &x, &y);
                
                    tmp_obs_x[tmp_num_obs] = x;
                    tmp_obs_y[tmp_num_obs] = y;
                    tmp_num_obs++;
                    {
                        char log_msg[64];
                        snprintf(log_msg, sizeof(log_msg), "Received Obstacle at %d,%d", x, y);
                        LOG(log_msg);
                    }
                    //printf("[BB] obstacle position: %d,%d; n%d\n", x, y, tmp_num_obs);
                    
                    if (tmp_num_obs == expected_obs){
                        struct msg map_msg;
                        map_msg.src = IDX_B;

                        snprintf(map_msg.data, MSG_SIZE, "RESET_O");
                        write(fd_out, &map_msg, sizeof(map_msg));

                        snprintf(map_msg.data, MSG_SIZE, "STOP_O");
                        write(fd_out, &map_msg, sizeof(map_msg));
                        LOG("sent STOP_O and RESET_O");

                        for(int i=0; i<tmp_num_obs; i++) {
                            bb.obs_x[i] = tmp_obs_x[i];
                            bb.obs_y[i] = tmp_obs_y[i];
                            bb.num_obs++;

                            snprintf(map_msg.data, MSG_SIZE, "O=%d,%d",  bb.obs_x[i], bb.obs_y[i]);
                            write(fd_out, &map_msg, sizeof(map_msg));
                        }

                        snprintf(map_msg.data, MSG_SIZE, "REDRAW_O"); 
                        write(fd_out, &map_msg, sizeof(map_msg)); 
                        LOG("Forwarding REDRAW_O to Map");
                        
                        tmp_num_obs = 1000; // Safe sentinel
                    }
                }
            }
            // Message from Targets (T)
            else if (m.src == IDX_T) {
                if (strncmp(m.data, "RESET", 5) == 0) {
                    tmp_num_tgs = 0;
                    LOG("Targets reset received");
                    continue;
                }
                else if (strncmp(m.data, "NEW", 3) == 0){
                    int x, y;
                    sscanf(m.data, "NEW: %d,%d", &x, &y);
                    if (bb.num_tgs < MAX_OBS) {
                        bb.tgs_x[bb.num_tgs] = x;
                        bb.tgs_y[bb.num_tgs] = y;
                        bb.num_tgs++;
                        
                        // Reset waiting flag
                        waiting_reply = 0; 
                        
                        struct msg map_msg;
                        map_msg.src = IDX_B;
                        snprintf(map_msg.data, MSG_SIZE, "GOAL=%d,%d", x, y);
                        write(fd_out, &map_msg, sizeof(map_msg));
                        
                        snprintf(map_msg.data, MSG_SIZE, "REDRAW_T"); 
                        write(fd_out, &map_msg, sizeof(map_msg));
                    }
                }
                else {
                    int x, y;
                    sscanf(m.data, "%d,%d", &x, &y);

                    expected_tgs = (int)roundf(bb.H * bb.W / 1000);

                    if (tmp_num_tgs < expected_tgs) {
                        tmp_tgs_x[tmp_num_tgs] = x;
                        tmp_tgs_y[tmp_num_tgs] = y;
                        tmp_num_tgs++;

                        {
                            char log_msg[64];
                            snprintf(log_msg, sizeof(log_msg), "Received Target at %d,%d", x, y);
                            LOG(log_msg);
                        }
                        //printf("[BB] target position: %d,%d; n%d\n", x, y, tmp_num_tgs);
                        
                        if (tmp_num_tgs == expected_tgs) {

                            struct msg map_msg;
                            map_msg.src = IDX_B;

                            snprintf(map_msg.data, MSG_SIZE, "RESET_T");
                            write(fd_out, &map_msg, sizeof(map_msg));

                            // Blocca il generatore di targets
                            snprintf(map_msg.data, MSG_SIZE, "STOP_T");
                            write(fd_out, &map_msg, sizeof(map_msg));
                            LOG("sent STOP_T and RESET_T");
                            //printf("[BB->T] STOP INVIATO\n");

                            // Salvo target nella BB e li mando alla mappa
                            bb.num_tgs = 0;
                            for (int i = 0; i < tmp_num_tgs; i++) {
                                bb.tgs_x[i] = tmp_tgs_x[i];
                                bb.tgs_y[i] = tmp_tgs_y[i];
                                bb.num_tgs++;

                                snprintf(map_msg.data, MSG_SIZE, "T[%d]=%d,%d",
                                        i, bb.tgs_x[i], bb.tgs_y[i]);
                                write(fd_out, &map_msg, sizeof(map_msg));
                            }

                            // Redraw targets
                            snprintf(map_msg.data, MSG_SIZE, "REDRAW_T");
                            write(fd_out, &map_msg, sizeof(map_msg));
                            LOG("Forwarding REDRAW_T to Map");
                            
                            tmp_num_tgs = 1000; // Sentinel value
                        }
                    }
                }
            }

            // Check distance between drone and obstacles
            for (int i = 0; i < bb.num_obs; i++){
                int dx = (bb.drone_x - bb.obs_x[i]);
                int dy = (bb.drone_y - bb.obs_y[i]);

                double dis = sqrt(dx*dx + dy*dy);
                if (dis <= d0 && dis > 0.0){
                    struct msg msg_f;
                    msg_f.src = IDX_B;

                    snprintf(msg_f.data, MSG_SIZE, "OBS_POS= %d,%d", bb.obs_x[i], bb.obs_y[i]);
                    write(fd_out, &msg_f, sizeof(msg_f));
                    LOG("Sent OBS_POS near to Drone");
                }
            }

            // ---------------------------------------------------------------------------------------------------
            // Logic to check the distance between drone and the first target: correction of "impossible grab goal"
            // ---------------------------------------------------------------------------------------------------

            // Check distance between drone and the current target
            if (bb.num_tgs > 0 && !waiting_reply) {
                int dx = (bb.drone_x - bb.tgs_x[0]);
                int dy = (bb.drone_y - bb.tgs_y[0]);

                double dis = sqrt(dx*dx + dy*dy);
                if (dis <= 1.0){ // Threshold reached
                    struct msg msg_t;
                    msg_t.src = IDX_B;

                    // Shift remaining targets
                    for (int i = 0; i < bb.num_tgs - 1; i++) {
                        bb.tgs_x[i] = bb.tgs_x[i + 1];
                        bb.tgs_y[i] = bb.tgs_y[i + 1];
                    }
                    bb.num_tgs--;
                    snprintf(msg_t.data, MSG_SIZE, "TARGET_REACHED");
                    write(fd_out, &msg_t, sizeof(msg_t));
                    LOG("Goal reached by the drone");
                    
                    waiting_reply = 1;
                }
            }
        }
        // Send alive signal to watchdog
        union sigval val;
        val.sival_int = time(NULL);
        
        //printf("[BB] signals: IM ALIVE\n");
        sigqueue(watchdog_pid, SIGUSR1, val);
        usleep(50000); 
    }
    
    close(fd_in);
    close(fd_out);
    LOG("Blackboard terminated");
    return 0;
}