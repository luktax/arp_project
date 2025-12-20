#include <ncurses.h>
#include <unistd.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <locale.h>
#include <string.h>
#include <math.h>

#define MSG_SIZE 64
#define MAX_OBS 100

enum { IDX_B = 0, IDX_D, IDX_I, IDX_M, IDX_O, IDX_T};



struct msg {
    int src;
    char data[MSG_SIZE];
};

void draw_window(WINDOW *win){
    int H, W;
    getmaxyx(stdscr, H, W);

    int margin = 3;

    int wh = H - 2 * margin;
    int ww = W - 2 * margin;
    if (wh < 3) wh = 3;
    if (ww < 3) ww = 3;
    
    wresize(win, wh, ww);
    mvwin(win, margin, margin);

    werase(stdscr);
    werase(win);

    wattron(win, COLOR_PAIR(2));
    box(win, 0, 0);
    wattroff(win, COLOR_PAIR(2));

    //mvwprintw(win, 0, 2, "BORDO");
    //mvwprintw(win, 0, 2, "%d,%d", H, W);
    refresh();
    wrefresh(win);
}

void draw_all(WINDOW *win, int obs_x[MAX_OBS], int obs_y[MAX_OBS], int num_obs, int tgs_x[MAX_OBS], int tgs_y[MAX_OBS], int num_tgs, int x, int y){
    werase(win);
    //draw_window(win);

    wattron(win, COLOR_PAIR(2));
    box(win, 0, 0);
    wattroff(win, COLOR_PAIR(2));

    for (int i = 0; i < num_obs; i++){
        wattron(win, COLOR_PAIR(2));
        mvwaddch(win, obs_y[i], obs_x[i], 'O');
        //mvwprintw(win, obs_y[i], obs_x[i], "%d,%d", obs_x[i], obs_y[i]);
        wattroff(win, COLOR_PAIR(2));
    }
    for (int i = 0; i < num_tgs; i++){
        wattron(win, COLOR_PAIR(4));
        mvwaddch(win, tgs_y[i], tgs_x[i], 'T');
        //mvwprintw(win, tgs_y[i], tgs_x[i], "%d,%d", tgs_x[i], tgs_y[i]);
        wattroff(win, COLOR_PAIR(4));

    }

    //drone
    wattron(win, COLOR_PAIR(1) | A_BOLD);
    mvwaddch(win, y, x, '+');
    wattroff(win, COLOR_PAIR(1) | A_BOLD);
    //refresh();
    wrefresh(win);

}

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

int main(int argc, char *argv[]) {

    //write on the processes.log
    register_process("Map");

    if (argc < 2) {
        fprintf(stderr, "Usage: %s <fd>\n", argv[0]);
        return 1;
    }
    int fd_in = atoi(argv[1]);
    int fd_out = atoi(argv[2]);

    setlocale(LC_ALL, "");
    initscr();
    noecho();
    cbreak();
    curs_set(FALSE);
    start_color();
    use_default_colors();
    keypad(stdscr, TRUE);
    nodelay(stdscr, TRUE);

    // color definition
    init_color(COLOR_YELLOW, 1000, 500, 0);   // orange
    init_pair(1, COLOR_BLUE,  -1);            
    init_pair(2, COLOR_YELLOW, -1);           
    init_pair(3, COLOR_WHITE, -1);
    init_pair(4, COLOR_GREEN, -1);
    
    int height, width;
    getmaxyx(stdscr, height, width);

    WINDOW *win_main = newwin(1, 1, 0, 0);

    int x = 5;
    int y = 5;

    draw_window(win_main);

    //obstacle
    int obs_x[MAX_OBS];
    int obs_y[MAX_OBS];
    int num_obs = 0;
    //targets
    int tgs_x[MAX_OBS];
    int tgs_y[MAX_OBS];
    int num_tgs = 0;
    //int expected_obs = (int)roundf(height*width/1000);
    //flag for the redraw
    int ready_o = 0;
    int ready_t = 0;

    while(1){
        
        //resize
        int ch = getch();
        if (ch == KEY_RESIZE){

            clear();
            erase();
            refresh();

            int still_resizing = 1;
            napms(50);
            int c2 = getch();
            if (c2 == ERR) {
                still_resizing = 0;
            }
            else if (c2 != KEY_RESIZE){
                ungetch(c2);
            }
            resize_term(0,0);
            getmaxyx(stdscr, height, width);
            
            //expected_obs = (int)(width * height) / 1000;

            num_obs = 0;
            num_tgs = 0;  

            //msg to notice the bb of the resize
            struct msg mb;
            mb.src = IDX_M;
            snprintf(mb.data, MSG_SIZE, "RESIZE %d %d", width-6, height-6);
            write(fd_out, &mb, sizeof(mb));

            ready_o = 0;
            ready_t = 0;

            draw_window(win_main);
        }

        //read the message
        struct msg m;
        ssize_t n = read(fd_in, &m, sizeof(m));
        
        if (n > 0) {
            //if it is an obstacle
            if (m.src == IDX_B && strncmp(m.data, "O=", 2) == 0){
                int o_x, o_y;
                sscanf(m.data, "O=%d,%d", &o_x, &o_y);
                obs_x[num_obs] = o_x;
                obs_y[num_obs] = o_y;
                num_obs++;
                //printf("[M] posizione ostacolo: %d,%d\n", o_x, o_y);
            }
            //if it is a target
            else if (m.src == IDX_B && strncmp(m.data, "T=", 2) == 0){
                int t_x, t_y;
                sscanf(m.data, "T=%d,%d", &t_x, &t_y);
                tgs_x[num_tgs] = t_x;
                tgs_y[num_tgs] = t_y;
                num_tgs++;
                //printf("[M] posizione target: %d,%d\n", t_x, t_y);
            }
            else if (m.src == IDX_B && strncmp(m.data, "REDRAW_O", 8) == 0){
                ready_o = 1;
            }
            else if (m.src == IDX_B && strncmp(m.data, "REDRAW_T", 8) == 0){
                ready_t = 1;
            }
            //if it is the drone
            else if(m.src == IDX_B && strncmp(m.data, "D=", 2) == 0){
                sscanf(m.data, "D=%d,%d", &x, &y);
            }
            else if (strncmp(m.data, "ESC", 3)== 0){
                printf("[MAP] EXIT\n");
                break;
            }
            
        }
        if (ready_o && ready_t){
            draw_all(win_main, obs_x, obs_y, num_obs, tgs_x ,tgs_y, num_tgs, x, y);
        }

        usleep(50000);

    }

    delwin(win_main);
    endwin();
    close(fd_in);
    return 0;
}