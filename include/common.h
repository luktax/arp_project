#ifndef COMMON_H
#define COMMON_H

// Number of processes in the system
#define NUM_PROCESSES 7

// Default message size for IPC
#define MSG_SIZE 64

// Default port for network communication
#define DEFAULT_PORT 8080
//#define HOST_NAME "localhost"
#define HOST_NAME "130.251.107.95"

// Process indices
enum {
    IDX_B = 0, // Blackboard
    IDX_D,     // Drone
    IDX_I,     // Keyboard Input
    IDX_M,     // Map
    IDX_O,     // Obstacles
    IDX_T,     // Targets
    IDX_W      // Watchdog
};

// Message structure for pipe communication
struct msg {
    int src;            // Source process index
    char data[MSG_SIZE]; // Message payload
};

// Logging macro
#define LOG(msg) process_log(PROCESS_NAME, msg)

enum {STANDALONE = 0, SERVER, CLIENT};
#endif