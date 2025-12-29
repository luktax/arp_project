#ifndef COMMON_H
#define COMMON_H

#define NUM_PROCESSES 7
#define MSG_SIZE 64

enum {
    IDX_B = 0,
    IDX_D,
    IDX_I,
    IDX_M,
    IDX_O,
    IDX_T,
    IDX_W
};

struct msg {
    int src;
    char data[MSG_SIZE];
};

#define LOG(msg) process_log(PROCESS_NAME, msg)

#endif