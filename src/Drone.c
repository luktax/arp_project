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
#define PROCESS_NAME "DRONE"
#include "../include/common.h"

#define MSG_SIZE 64


struct params{
    float M; // Mass
    float K; // Viscous coefficient
    float T; // Simulation time step (sec)
    float USER_FORCE; // User input force scaling
    float RHO; // Repulsive force range
    float NI; // Repulsive force gain
};

struct drone{
    int x; // Horizontal position
    int y; // Vertical position
    float Fx; // Resulting force (X)
    float Fy; // Resulting force (Y)
    float vx; // Velocity (X)
    float vy; // Velocity (Y)
};


int load_params(const char *filename, struct params *p){
    FILE *f = fopen(filename, "r");
    if (!f){
        perror("open ParameterFile");
        return -1;
    }

    char line[128];
    while (fgets(line, sizeof(line), f)) {
        char key[64];
        float value;

        // Read the line with format %63[^=]: all characters until '=', saved in key, %f float saved in value
        if (sscanf(line, "%63[^=] = %f", key, &value) == 2){
            for(char *c = key; *c; c++) {
                if (*c == ' ') *c = '\0';
            }
            if (strcmp(key, "M")== 0) p->M = value;
            else if (strcmp(key, "K") == 0) p->K = value;
            else if (strcmp(key, "T") == 0) p->T = value;
            else if (strcmp(key, "USER_FORCE") == 0) p->USER_FORCE = value;
            else if (strcmp(key, "RHO") == 0) p->RHO = value;
            else if (strcmp(key, "NI") == 0) p->NI = value;
        }
    }
    
    fclose(f);
    return 0;
}

int main(int argc, char *argv[]) {

    // Register process for logging
    register_process("Drone");
    LOG("Process initialized");

    int x, y;
    if (argc < 3) {
        fprintf(stderr, "Usage: %s <fd>\n", argv[0]);
        return 1;
    }
    // Drone initialization
    struct drone D;
    D.x = 5;
    D.y = 5;
    D.Fx = 0;
    D.Fy = 0;
    D.vx = 0;
    D.vy = 0;

    // fd_in: read from parent/router
    int fd_in = atoi(argv[1]);

    // fd_out: write to parent/router
    int fd_out = atoi(argv[2]);

    // Watchdog PID to send alive signals
    pid_t watchdog_pid = atoi(argv[3]);

    float X = D.x;
    float Y = D.y;

    //map
    int height = 30;
    int width = 155;

    int flag_reset = 0;
    int running = 1;

    int flags = fcntl(fd_in, F_GETFL, 0);
    fcntl(fd_in, F_SETFL, flags | O_NONBLOCK);

    // Initial message for the position
    struct msg out_msg;
    out_msg.src = IDX_D;
    snprintf(out_msg.data, MSG_SIZE, "%d,%d", D.x, D.y);
    if (write(fd_out, &out_msg, sizeof(out_msg)) < 0) {
        perror("write to router");
    }

    while(running){
        struct params params;
        load_params("config/ParameterFile.txt", &params);

        if (flag_reset){
            D.x = 5;
            D.y = 5;
            D.vx = 0;
            D.vy = 0;
            D.Fx = 0;
            D.Fy = 0;
            X = D.x;
            Y = D.y;
            flag_reset = 0;
            snprintf(out_msg.data, MSG_SIZE, "%d,%d", D.x, D.y);
            if (write(fd_out, &out_msg, sizeof(out_msg)) < 0) {
                perror("write to router");
    }
        }
        
        x = D.x;
        y = D.y;
        int dx = -100; // Large sentinel
        int dy = -100; // Large sentinel

        int last_ch_in_burst = -1;
        while (1) {
            struct msg m;
            ssize_t n = read(fd_in, &m, sizeof(m));
            if (n > 0) {
                 char dbg[64];
                 snprintf(dbg, sizeof(dbg), "DRONE: Received key '%c' from router (src=%d)", m.data[0], m.src);
                 LOG(dbg);
            }
            if (n < 0) {
                if (errno == EAGAIN || errno == EWOULDBLOCK) {
                    break; // No more messages in pipe
                }
                perror("read drone");
                break;
            }
            if (n == 0) break; // EOF

            int ch = (m.data[0]);
            
            // Dynamics Stabilization: Skip redundant movement keys in a single burst
            // (e.g., multiple 'w' keys arriving in one select wake-up after network lag)
            if (ch == last_ch_in_burst && (ch == 'w' || ch == 'x' || ch == 'a' || ch == 'd' || 
                                          ch == 'e' || ch == 'c' || ch == 'q' || ch == 'z')) {
                continue;
            }
            last_ch_in_burst = ch;

            if (strncmp(m.data, "RESIZE", 6)== 0){
                ch = 'r';
                sscanf(m.data, "RESIZE %d %d", &width, &height);
                {
                    char log_msg[64];
                    snprintf(log_msg, sizeof(log_msg), "Window resized to %dx%d", width, height);
                    LOG(log_msg);
                }
            }
            // Movement
            if (ch == 'w') D.Fy--;
            else if (ch == 'x') D.Fy++;
            else if (ch == 'a') D.Fx--;
            else if (ch == 'd') {D.Fx++; LOG("received d");}
            else if (ch == 'e') { D.Fx++; D.Fy--; }
            else if (ch == 'c') { D.Fx++; D.Fy++; }
            else if (ch == 'q') { D.Fx--; D.Fy--; }
            else if (ch == 'z') { D.Fx--; D.Fy++; }
            else if (ch == 's') { D.Fx = 0; D.Fy = 0; }
            else if (ch == 'r') { 
                flag_reset = 1; 
                printf("RESET\n");
                LOG("Reset command received");
            } // Reset
            else if (ch == 27) {
                printf("[DRONE] EXIT\n");
                LOG("Received ESC, shutting down");
                running = 0; 
                break;
            } // ESC closes everything

            if (strncmp(m.data, "OBS_POS", 7)== 0){
                sscanf(m.data, "OBS_POS= %d,%d", &dx, &dy);
                LOG("Obstacle detected");
                //printf("[D] Obstacle NEAR\n");
            }
            else if (strncmp(m.data, "ESC", 3)== 0){
                printf("[DRONE] EXIT\n");
                running = 0;
                break;
            }
        }
        
        if (!running) break;
        // Repulsive Force x and y from obstacles
        float dist_x = X - dx;
        float dist_y = Y - dy;
        float dist = sqrt(dist_x*dist_x + dist_y*dist_y);
        float Frep_x = 0;
        float Frep_y = 0;
        if (dist < params.RHO && dist>0){
            Frep_x = params.NI * (1.0/dist - 1.0/params.RHO) * (dist_x / dist)*5;
            Frep_y = params.NI * (1.0/dist - 1.0/params.RHO) * (dist_y / dist)*5;
        }

        // Repulsive Force x and y from the walls
        // Wall left
        dist_x = X;
        if (dist_x < params.RHO){ Frep_x += params.NI * (1.0/dist_x - 1.0/params.RHO)*(dist_x/dist_x);}

        // Wall right
        dist_x = abs(X - width);
        if (dist_x < params.RHO) {Frep_x -= params.NI * (1.0/dist_x - 1.0/params.RHO)*(dist_x/dist_x);}

        // Wall top
        dist_y = Y;
        if (dist_y < params.RHO) {Frep_y += params.NI * (1.0/dist_y - 1.0/params.RHO)*(dist_y/dist_y);}

        // Wall bottom
        dist_y = abs(Y- height);
        if (dist_y < params.RHO) {Frep_y -= params.NI * (1.0/dist_y - 1.0/params.RHO)*(dist_y/dist_y);}

        //printf("Repulsive force: %f, %f", Frep_x, Frep_y);

        // Viscous force X
        float Fvisc_x = params.K * D.vx;

        // Resulting Force X
        float Fx_TOT = D.Fx * params.USER_FORCE - Fvisc_x + Frep_x;
        
        // Viscous force Y
        float Fvisc_y = params.K * D.vy;
        // Resulting Force Y
        float Fy_TOT = D.Fy * params.USER_FORCE - Fvisc_y + Frep_y;

        // X position update
        float ax = Fx_TOT/params.M;
        D.vx = D.vx + ax * params.T;
        X = X + D.vx * params.T;
        // Y position update
        float ay = Fy_TOT/params.M;
        D.vy = D.vy + ay * params.T;
        Y = Y + D.vy * params.T;

        // Zero out small velocities to prevent jitter
        if (fabs(D.vx) < 0.01) D.vx = 0;
        if (fabs(D.vy) < 0.01) D.vy = 0;
        
        // Send STATS to Blackboard for Diagnostics
        // Send STATS to Blackboard for Diagnostics (Reduced frequency)
        static int stats_count = 0;
        if (stats_count++ % 100 == 0) {
            struct msg stats_msg;
            stats_msg.src = IDX_D;
            snprintf(stats_msg.data, MSG_SIZE, "STATS %.2f %.2f %.2f %.2f %.2f %.2f", Fx_TOT, Fy_TOT, D.vx, D.vy, X, Y);
            write(fd_out, &stats_msg, sizeof(stats_msg));
            LOG(stats_msg.data);
        }
        // Update the discrete position
        D.x = (int)roundf(X);
        D.y = (int)roundf(Y);

        if (D.x < 1){ D.x = 1; X = 1.0; D.vx = 0;}
        if (D.y < 1) {D.y = 1; Y = 1.0; D.vy = 0;}
        if (D.x >= width - 1) {D.x = width - 2; X = (float)(width - 2); D.vx = 0;}
        if (D.y >= height - 1) {D.y = height - 2; Y = (float)(height - 2); D.vy = 0;}

        //printf("x=%d, y=%d\n", D.x, D.y);
        //printf("Fx=%f, Fy=%f\n", Fx_TOT, Fy_TOT);
    
        if (!(D.x == x && D.y == y)){
            snprintf(out_msg.data, MSG_SIZE, "%d,%d", D.x, D.y);
            if (write(fd_out, &out_msg, sizeof(out_msg)) < 0) {
                perror("write to router");
            }
            {
                char log_msg[64];
                snprintf(log_msg, sizeof(log_msg), "DRONE: Sent position [%d,%d] to router", D.x, D.y);
                LOG(log_msg);
            }
        }
        // Send alive signal to watchdog
        union sigval val;
        val.sival_int = time(NULL);
        
        //printf("[D] signals: IM ALIVE\n");
        if (watchdog_pid > 0) {
            sigqueue(watchdog_pid, SIGUSR1, val);
        }

        usleep(params.T * 1e6);
    }

    close(fd_in);
    close(fd_out);
    LOG("Drone terminated");
    return 0;
}
