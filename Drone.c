#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <sys/select.h>
#include <sys/wait.h>
#include <errno.h>
#include <string.h>
#include <math.h>
#include <fcntl.h>

#define MSG_SIZE 64

enum { IDX_B = 0, IDX_D, IDX_I, IDX_M, IDX_O, IDX_T};

struct msg {
    int src;
    char data[MSG_SIZE];
};

struct params{
    float M; //mass
    float K; //viscous coeff
    float T; //simulation time (sec)
    float USER_FORCE; // user input force
    float RHO;
    float NI;
};

struct drone{
    int x; //position
    int y;
    float Fx; //resulting force x
    float Fy; //resulting force y
    float vx; //velocity x
    float vy; //velocity y
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

        //read the line with format %63[^=]: all character untill '=', saved in key, %f float saved in value
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
    int x, y;
    if (argc < 3) {
        fprintf(stderr, "Usage: %s <fd>\n", argv[0]);
        return 1;
    }
    //drone initialization
    struct drone D;
    D.x = 5;
    D.y = 5;
    D.Fx = 0;
    D.Fy = 0;
    D.vx = 0;
    D.vy = 0;

    // fd_in: read from father
    int fd_in = atoi(argv[1]);

    // fd_out: write to father
    int fd_out = atoi(argv[2]);

    float X = D.x;
    float Y = D.y;

    //map
    int height = 30;
    int width = 190;

    int flag_reset = 0;

    int flags = fcntl(fd_in, F_GETFL, 0);
    fcntl(fd_in, F_SETFL, flags | O_NONBLOCK);

    //initial message for the position
    struct msg out_msg;
    out_msg.src = IDX_D;
    snprintf(out_msg.data, MSG_SIZE, "%d,%d", D.x, D.y);
    if (write(fd_out, &out_msg, sizeof(out_msg)) < 0) {
        perror("write to router");
    }

    while(1){
        struct params params;
        load_params("ParameterFile.txt", &params);

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
        int dx = 0;
        int dy = 0;

        struct msg m;
        ssize_t n = read(fd_in, &m, sizeof(m));
        //printf("[BB->D] Ricevuto codice: %d\n", ch);

        if (n > 0) {
            int ch = (m.data[0]);
            if (strncmp(m.data, "RESIZE", 6)== 0){
                ch = 'r';
                sscanf(m.data, "RESIZE %d %d", &width, &height);
            }
            // Movimento
            if (ch == 'w') D.Fy--;
            else if (ch == 'x') D.Fy++;
            else if (ch == 'a') D.Fx--;
            else if (ch == 'd') D.Fx++;
            else if (ch == 'e') { D.Fx++; D.Fy--; }
            else if (ch == 'c') { D.Fx++; D.Fy++; }
            else if (ch == 'q') { D.Fx--; D.Fy--; }
            else if (ch == 'z') { D.Fx--; D.Fy++; }
            else if (ch == 's') { D.Fx = 0; D.Fy = 0; }
            else if (ch == 'r') { flag_reset = 1; printf("RESET\n");
            } //reset
            else if (ch == 27) {
                printf("[DRONE] EXIT\n");
                break;} // ESC chiude tutto

            if (strncmp(m.data, "OBS_POS", 7)== 0){
                sscanf(m.data, "OBS_POS= %d,%d", &dx, &dy);
                //printf("[D] Obstacle NEAR\n");
            }
            else if (strncmp(m.data, "ESC", 3)== 0){
                printf("[DRONE] EXIT\n");
                break;
            }
        }
        //repulsive Force x and y from obstacles
        float dist = sqrt(dx*dx + dy*dy);
        float Frep_x = 0;
        float Frep_y = 0;
        if (dist < params.RHO && dist>0){
            Frep_x = params.NI * (1.0/dist - 1.0/params.RHO) * (dx / dist);
            Frep_y = params.NI * (1.0/dist - 1.0/params.RHO) * (dy / dist);
        }

        //repulsive Force x and y from the walls
        //wall left
        dx = D.x;
        if (dx < params.RHO){ Frep_x += params.NI * (1.0/dx - 1.0/params.RHO)*(dx/dx);}

        //wall right
        dx = abs(D.x - width);
        if (dx < params.RHO) {Frep_x -= params.NI * (1.0/dx - 1.0/params.RHO)*(dx/dx);}

        //wall top
        dy = D.y;
        if (dy < params.RHO) {Frep_y += params.NI * (1.0/dy - 1.0/params.RHO)*(dy/dy);}

        //wall bottom
        dy = abs(D.y- height);
        if (dy < params.RHO) {Frep_y -= params.NI * (1.0/dy - 1.0/params.RHO)*(dy/dy);}

        //printf("Repulsive force: %f, %f", Frep_x, Frep_y);

        //viscous force x
        float Fvisc_x = params.K * D.vx;

        //resulting Force x
        float Fx_TOT = D.Fx * params.USER_FORCE - Fvisc_x + Frep_x;
        
        //viscous force y
        float Fvisc_y = params.K * D.vy;
        //resulting Force y
        float Fy_TOT = D.Fy * params.USER_FORCE - Fvisc_y + Frep_y;
        
        //x position
        float ax = Fx_TOT/params.M;
        D.vx = D.vx + ax * params.T;
        X = X + D.vx * params.T;
        //y position
        float ay = Fy_TOT/params.M;
        D.vy = D.vy + ay * params.T;
        Y = Y + D.vy * params.T;
        
        //update the position
        D.x = (int)roundf(X);
        D.y = (int)roundf(Y);

        if (D.x < 1){ D.x = 1; D.vx = 0;}
        if (D.y < 1) {D.y = 1; D.vy = 0;}
        if (D.x >= width - 1) {D.x = width - 2; D.vx = 0;}
        if (D.y >= height - 1) {D.y = height - 2; D.vy = 0;}

        //printf("x=%d, y=%d\n", D.x, D.y);
        //printf("Fx=%f, Fy=%f\n", Fx_TOT, Fy_TOT);
    
        if (!(D.x == x && D.y == y)){
            snprintf(out_msg.data, MSG_SIZE, "%d,%d", D.x, D.y);
            if (write(fd_out, &out_msg, sizeof(out_msg)) < 0) {
                perror("write to router");
            }
        }
        usleep(params.T * 1e6);
    }

    close(fd_in);
    close(fd_out);
    return 0;
}
