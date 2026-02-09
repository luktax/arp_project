#define _POSIX_C_SOURCE 200809L
#include <signal.h>
#include <ncurses.h>
#include <unistd.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <locale.h>
#include <string.h>
#include <math.h>
#include <errno.h>
#include <time.h>

#include "../include/process_log.h"
#define PROCESS_NAME "MAP"
#include "../include/common.h"

#define MAX_OBS 100

#define STATS_WIDTH 35

int grabbed = 0;
int height, width;

/**
 * Redraw the main ncurses window and its borders.
 */
void draw_window(WINDOW *win){
    int H, W;
    getmaxyx(stdscr, H, W);

    height = H;
    width = W;

    int margin = 3;

    int wh = H - 2 * margin;
    int ww = W - 2 * margin - STATS_WIDTH; // Reserve space for sidebar
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

/**
 * Draw all game elements (obstacles, targets, and drone) in the main window.
 */
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
        mvwprintw(win, tgs_y[i], tgs_x[i], "%d", i+1+grabbed);
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

int main(int argc, char *argv[]) {

    // Register process for logging
    register_process("Map");
    LOG("Map process started");

    if (argc < 2) {
        fprintf(stderr, "Usage: %s <fd>\n", argv[0]);
        return 1;
    }
    int fd_in = atoi(argv[1]);
    int fd_out = atoi(argv[2]);

    int flags = fcntl(fd_in, F_GETFL, 0);
    fcntl(fd_in, F_SETFL, flags | O_NONBLOCK);
    // Watchdog PID to send alive signals
    pid_t watchdog_pid = atoi(argv[3]);
    
    // Mode (0=STANDALONE, 1=SERVER, 2=CLIENT)
    int mode = (argc >= 5) ? atoi(argv[4]) : 0;

    setlocale(LC_ALL, "");
    initscr();
    noecho();
    cbreak();
    curs_set(FALSE);
    start_color();
    use_default_colors();
    keypad(stdscr, TRUE);
    nodelay(stdscr, TRUE);

    // Color pair definitions
    init_color(COLOR_YELLOW, 1000, 500, 0);   // Orange
    init_pair(1, COLOR_BLUE,  -1);            
    init_pair(2, COLOR_YELLOW, -1);           
    init_pair(3, COLOR_WHITE, -1);
    init_pair(4, COLOR_GREEN, -1);

    // If initial dimensions are passed as arguments (Client mode)
    if (argc >= 7) {
        width = atoi(argv[5]) + STATS_WIDTH + 6;
        height = atoi(argv[6]) + 6;
        LOG("Initial dimensions from command line used");
    }

    WINDOW *win_main = newwin(1, 1, 0, 0);

    int x = 5;
    int y = 5;

    draw_window(win_main);
    
    // Initial message to notify Blackboard of the terminal dimension
    struct msg mb_init;
    mb_init.src = IDX_M;
    snprintf(mb_init.data, MSG_SIZE, "RESIZE %d %d", width - STATS_WIDTH - 6, height - 6);
    write(fd_out, &mb_init, sizeof(mb_init));
    
    // Stats window placement: Right side
    int W_scr, H_scr;
    getmaxyx(stdscr, H_scr, W_scr);
    WINDOW *win_stats = newwin(H_scr - 6, STATS_WIDTH - 2, 3, W_scr - STATS_WIDTH); 
    box(win_stats, 0, 0);
    wrefresh(win_stats);

    LOG("Map initialized");

    // Obstacles state
    int obs_x[MAX_OBS];
    int obs_y[MAX_OBS];
    int num_obs = 0;

    // Targets state
    int tgs_x[MAX_OBS];
    int tgs_y[MAX_OBS];
    int num_tgs = 0;

    // Redraw flags
    int ready_o = 1;
    int ready_t = 1;

    int running = 1;

    while(running){
        
        // Resizing logic
        int ch = getch();
        if (ch == KEY_RESIZE){

            int c2;
            do {
                napms(50);
                c2 = getch();
            } while (c2 == KEY_RESIZE);

            if (c2 != ERR) {
                ungetch(c2);
            }
            
            clear();
            erase();
            refresh();
            resize_term(0,0);
            LOG("Map resized");

            getmaxyx(stdscr, height, width);
            
            //expected_obs = (int)(width * height) / 1000;

            num_obs = 0;
            num_tgs = 0;  

            // Message to notify Blackboard of the terminal resize
            struct msg mb;
            mb.src = IDX_M;
            // Send game area dimensions (Width - Sidebar - Margins)
            snprintf(mb.data, MSG_SIZE, "RESIZE %d %d", width - STATS_WIDTH - 6, height - 6);
            write(fd_out, &mb, sizeof(mb));

            ready_o = 0;
            ready_t = 0;

            draw_window(win_main);
            
            // Redraw stats window at correct position
            wresize(win_stats, height - 6, STATS_WIDTH - 2);
            mvwin(win_stats, 3, width - STATS_WIDTH);
            box(win_stats, 0, 0);
            wrefresh(win_stats);
        }

        // Read incoming messages
        while(1){
            struct msg m;
            ssize_t n = read(fd_in, &m, sizeof(m));
            
            if (n < 0) {
                if (errno == EAGAIN || errno == EWOULDBLOCK) {
                    break; 
                }
                perror("read map");
                break;
            }
            if (n == 0) break;

            // STATS forwarded by Blackboard
            if (m.src == IDX_B && strncmp(m.data, "STATS", 5) == 0){
                float fx, fy, vx, vy, x, y;
                sscanf(m.data, "STATS %f %f %f %f %f %f", &fx, &fy, &vx, &vy, &x, &y);
                
                werase(win_stats);
                box(win_stats, 0, 0);
                mvwprintw(win_stats, 1, 1, "DYNAMICS");
                mvwprintw(win_stats, 2, 1, "Pos:   %.2f, %.2f", x, y);
                mvwprintw(win_stats, 3, 1, "Vel:   %.2f, %.2f", vx, vy);
                mvwprintw(win_stats, 4, 1, "Force: %.2f, %.2f", fx, fy);
                wrefresh(win_stats);
            }
            // Obstacle update
            else if (m.src == IDX_B && strncmp(m.data, "O=", 2) == 0){
                int o_x, o_y;
                // LOG("MAP received obs position");
                if (mode == SERVER) num_obs = 0; // Only keep one obstacle in server mode
                sscanf(m.data, "O=%d,%d", &o_x, &o_y);
                obs_x[num_obs] = o_x;
                obs_y[num_obs] = o_y;
                num_obs++;
            }
            // Target update
            else if (m.src == IDX_B && strncmp(m.data, "T[", 2) == 0){
                int t_i, t_x, t_y;
                sscanf(m.data, "T[%d]=%d,%d", &t_i, &t_x, &t_y);
                if (t_i >= 0 && t_i < MAX_OBS) {
                    tgs_x[t_i] = t_x;
                    tgs_y[t_i] = t_y;
                    if (t_i >= num_tgs) num_tgs = t_i + 1;
                }
            }
            else if (m.src == IDX_B && strncmp(m.data, "GOAL=", 5) == 0){
                int t_x, t_y;
                sscanf(m.data, "GOAL=%d,%d", &t_x, &t_y);
                
                for (int i = 0; i < num_tgs - 1; i++) {
                        tgs_x[i] = tgs_x[i + 1];
                        tgs_y[i] = tgs_y[i + 1];
                }
                if (num_tgs > 0) {
                    tgs_x[num_tgs - 1] = t_x;
                    tgs_y[num_tgs - 1] = t_y;
                }
                
                grabbed++;
            }
            else if (m.src == IDX_B && strncmp(m.data, "RESET_O", 7) == 0){
                num_obs = 0;
            }
            else if (m.src == IDX_B && strncmp(m.data, "O_SHIFT=", 8) == 0){
                int x, y;
                sscanf(m.data, "O_SHIFT=%d,%d", &x, &y);
                if (num_obs > 0) {
                    for (int i = 0; i < num_obs - 1; i++) {
                        obs_x[i] = obs_x[i + 1];
                        obs_y[i] = obs_y[i + 1];
                    }
                    obs_x[num_obs - 1] = x;
                    obs_y[num_obs - 1] = y;
                }
            }
            else if (m.src == IDX_B && strncmp(m.data, "RESET_T", 7) == 0){
                num_tgs = 0;
            }
            else if (m.src == IDX_B && strncmp(m.data, "REDRAW_O", 8) == 0){
                ready_o = 1;
            }
            else if (m.src == IDX_B && strncmp(m.data, "REDRAW_T", 8) == 0){
                ready_t = 1;
            }
            // Drone position update
            else if(m.src == IDX_B && strncmp(m.data, "D=", 2) == 0){
                sscanf(m.data, "D=%d,%d", &x, &y);
                // LOG(m.data);
            }
            else if (strncmp(m.data, "ESC", 3)== 0){
                printf("[MAP] EXIT\n");
                LOG("Map received ESC, exiting");
                running = 0;
                break;
            }
        }
        
        if (!running) break;
        if (ready_o && ready_t){
            draw_all(win_main, obs_x, obs_y, num_obs, tgs_x ,tgs_y, num_tgs, x, y);
            // LOG("Map redrawn"); // Too frequent in debug mode
        }

        // Send alive signal to watchdog
        union sigval val;
        val.sival_int = time(NULL);
        
        //printf("[M] signals: IM ALIVE\n");
        //printf("[M] signals: IM ALIVE\n");
        if (watchdog_pid > 0) {
            sigqueue(watchdog_pid, SIGUSR1, val);
        }

        usleep(50000);

    }

    delwin(win_main);
    delwin(win_stats);
    endwin();
    close(fd_in);
    LOG("Map terminated");
    return 0;
}