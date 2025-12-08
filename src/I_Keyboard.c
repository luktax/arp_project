#include <ncurses.h>
#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <ctype.h>
#include <string.h>

#define MSG_SIZE 64
enum { IDX_B = 0, IDX_D, IDX_I, IDX_M, IDX_O, IDX_T};

struct msg {
    int src;             
    char data[MSG_SIZE]; 
};

#define ROWS 3
#define COLS 3

void draw_keypad(int start_y, int start_x, int cell_h, int cell_w, char *keys[ROWS][COLS], int pressed) {
    
    for (int row = 0; row < ROWS; row++) {
        for (int col = 0; col < COLS; col++) {
            int y = start_y + row * cell_h;
            int x = start_x + col * cell_w;

            int ky = y + cell_h/2;
            int kx = x + (cell_w/2);

            // color if pressed
            if (toupper(pressed) == keys[row][col][0])
                attron(COLOR_PAIR(2) | A_BOLD);

            // cell border
            for (int i = 0; i < cell_w; i++) {
                mvaddch(y, x + i, '-');                
                mvaddch(y + cell_h - 1, x + i, '-');   
            }
            for (int i = 0; i < cell_h; i++) {
                mvaddch(y + i, x, '|');                
                mvaddch(y + i, x + cell_w - 1, '|');   
            }
            mvaddch(y, x, '+');                        
            mvaddch(y, x + cell_w - 1, '+');
            mvaddch(y + cell_h - 1, x, '+');
            mvaddch(y + cell_h - 1, x + cell_w - 1, '+');

            //KEY
            mvprintw(ky, kx, "%s", keys[row][col]);

            attroff(COLOR_PAIR(1) | COLOR_PAIR(2) | A_BOLD);
        }
    }
    refresh();
}

int main(int argc, char *argv[]) {
    if (argc < 3) {
        fprintf(stderr, "Usage: %s <fd>\n", argv[0]);
        return 1;
    }

    int fd_out = atoi(argv[2]);

    initscr();
    cbreak();        // 
    noecho();        // 
    keypad(stdscr, TRUE); // 
    nodelay(stdscr, TRUE);
    curs_set(FALSE);
    start_color();

    init_pair(1, COLOR_WHITE, -1); // default
    init_pair(2, COLOR_GREEN, -1);

    mvprintw(0, 0, "I_Keyboard: press a button to move");
    mvprintw(1, 0, "Press ESC to exit");
    mvprintw(2, 0, "Press R to reset");
    refresh();

    int height, width;
    getmaxyx(stdscr, height, width);

    int start_y = height/2-7, start_x = width/2-7;
    int cell_h = 3, cell_w = 5;  // cells dimensions
    char *keys[3][3] = {
        {"Q", "W", "E"},
        {"A", "S", "D"},
        {"Z", "X", "C"}
    };

    draw_keypad(start_y, start_x, cell_h, cell_w, keys, 0);

    

    int ch;
    struct msg m;

    while (1) {
        ch = getch(); // inout

        if (ch != ERR) {
            m.src = IDX_I;
            snprintf(m.data, MSG_SIZE, "%c", ch);

            // write to father
            if (write(fd_out, &m, sizeof(m)) < 0) {
                perror("write");
            }

            // ESC
            if (ch == 27){ printf("[I_KEYBOARD] EXIT\n"); break;}

            draw_keypad(start_y, start_x, cell_h, cell_w, keys, ch);
            refresh();
        }

        usleep(50000); // 50 ms delay 
    }

    endwin();
    close(fd_out);
    return 0;
}