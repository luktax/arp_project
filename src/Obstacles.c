#include <unistd.h> 
#include <fcntl.h> 
#include <stdio.h> 
#include <stdlib.h> 
#include <time.h> 
#include <string.h> 
#include <math.h> 
#include <fcntl.h>

#define MSG_SIZE 64 
enum { IDX_B = 0, IDX_D, IDX_I, IDX_M, IDX_O, IDX_T}; 

struct msg { int src; char data[MSG_SIZE]; }; 

int main(int argc, char *argv[]) {
    if (argc < 3) { 
        fprintf(stderr, "Usage: %s <fd>\n", argv[0]); 
        return 1; 
    } 

    int fd_in = atoi(argv[1]); 
    int fd_out = atoi(argv[2]);
    fcntl(fd_in, F_SETFL, O_NONBLOCK);

    int W = 190, H = 30; 

    srand(time(NULL)^ getpid()); 

    struct msg bb_msg; 
    bb_msg.src = IDX_O;

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
                    printf("[BB->O] RESIZE ricevuto %d, %d\n", W, H); 
                    if (newW != W || newH != H){ 
                        W = newW; 
                        H = newH; 
                        window_changed = 1; 
                    }
                }
                if (strncmp(m.data, "STOP_O", 6) == 0){
                    window_changed = 0;
                    //printf("[O] STOP\n");
                }     
            }
            if (strncmp(m.data, "ESC", 3)== 0){
                printf("[OBSTACLES] EXIT\n");
                break;
            }
        }

        bb_msg.data[0] = '\0';

        if(window_changed){ 
            if (!reset_sent){
                snprintf(bb_msg.data, MSG_SIZE, "RESET"); 
                write(fd_out, &bb_msg, sizeof(bb_msg)); 
                reset_sent = 1;
                printf("[O] RESET INVIATO\n");
            }
            
            int x = (rand() % (W-2)) + 1;
            int y = (rand() % (H-2)) + 1;

            snprintf(bb_msg.data, MSG_SIZE, "%d,%d", x, y);
            write(fd_out, &bb_msg, sizeof(bb_msg));
        } else {
            reset_sent = 0;
        } 
        usleep(50000); 
    } 
}