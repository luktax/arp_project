#define _POSIX_C_SOURCE 200809L
#define _DEFAULT_SOURCE
#include <sys/select.h>
#include <unistd.h>
#include <sys/wait.h>
#include <stdlib.h>
#include <stdio.h>
#include <fcntl.h>
#include <string.h>
#include <errno.h>
#include <signal.h>
#include <time.h>

/* ========================================================================
 * NETWORK MODE: Additional includes for socket communication
 * ======================================================================== */
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <sys/time.h>

/* ========================================================================
 * NETWORK MODE: Synchronization timing constants
 * - DRONE_SYNC_MS: Frequency for sending drone position updates (10Hz)
 * - OBST_SYNC_MS: Frequency for requesting obstacle position (20Hz)
 * ======================================================================== */
#define DRONE_SYNC_MS 100
#define OBST_SYNC_MS 50

// Header files
#include "../include/process_log.h"
#define PROCESS_NAME "MAIN"
#include "../include/common.h"

static const char *process_names[] = {
    [IDX_B] = "Blackboard",
    [IDX_D] = "Drone",
    [IDX_I] = "Keyboard",
    [IDX_M] = "Map",
    [IDX_O] = "Obstacles",
    [IDX_T] = "Targets",
    [IDX_W] = "Watchdog"
};

pid_t pid_B, pid_D, pid_I, pid_M, pid_O, pid_T, pid_W;

void clean_children() {
    if (pid_W > 0) kill(pid_W, SIGTERM);
    if (pid_B > 0) kill(pid_B, SIGTERM);
    if (pid_D > 0) kill(pid_D, SIGTERM);
    if (pid_I > 0) kill(pid_I, SIGTERM);
    if (pid_M > 0) kill(pid_M, SIGTERM);
    if (pid_O > 0) kill(pid_O, SIGTERM);
    if (pid_T > 0) kill(pid_T, SIGTERM);
}

void handle_signal(int sig) {
    printf("\n[MAIN] Caught signal %d, terminating children...\n", sig);
    clean_children();
    
    // Wait for children to exit to avoid zombies
    while(wait(NULL) > 0);
    
    printf("[MAIN] All children terminated. Exiting.\n");
    exit(0);
}

/* ========================================================================
 * NETWORK MODE: Helper function for reading null-terminated strings from socket
 * ======================================================================== */
ssize_t read_line(int fd, char *buf, size_t maxlen) {
    size_t i = 0;
    char c;

    while (i < maxlen - 1) {
        ssize_t n = read(fd, &c, 1);
        if (n == 1) {
            buf[i++] = c;
            if (c == '\0') break;
        } else if (n == 0) {
            return 0;   // connection closed
        } else {
            return -1;  // error
        }
    }
    buf[i] = '\0';
    return i;
}


int pipe_parent_to_child[NUM_PROCESSES][2]; // Child reads from [0], parent writes to [1]
int pipe_child_to_parent[NUM_PROCESSES][2]; // Parent reads from [0], child writes to [1]

// Route table structure
typedef struct{
    int num;
    int dest[NUM_PROCESSES];
} route_t;

/* ========================================================================
 * NETWORK MODE: Global variables for network operation
 * - mode: Operating mode (STANDALONE=0, SERVER=1, CLIENT=2)
 * - network_fd: Socket file descriptor for network communication
 * - win_w, win_h: Window dimensions (fixed in network mode, dynamic in standalone)
 * ======================================================================== */
int mode = 100;
int server_fd, client_fd, network_fd = -1;
int win_w = 155, win_h = 30;

int main(){
    unlink("log/watchdog.log");
    unlink("log/processes_pid.log");
    unlink("log/system.log");
    while (1) {
        printf("Choose the mode: 0->STANDALONE, 1->SERVER, 2->CLIENT\n");
        fflush(stdout);
        
        int result = scanf("%d", &mode);
        if (result == 1) {
            if (mode == STANDALONE || mode == SERVER || mode == CLIENT) {
                break;
            }
            printf("Invalid mode selected.\n");
        } else {
             printf("Invalid input. Please enter a number.\n");
        }
        
        // Clear input buffer
        int c;
        while ((c = getchar()) != '\n' && c != EOF);
    }
    
    LOG("Initialization, mode");

    /* ========================================================================
     * NETWORK MODE: Socket Setup and Connection Establishment
     * ========================================================================
     * STANDALONE mode: Skip all network setup
     * ======================================================================== */
    int sockfd, newsockfd, clilen, n;
    struct sockaddr_in serv_addr, cli_addr;
    struct hostent *server;
    char msg_sock[100];

    /* ========================================================================
     * SERVER MODE
     * ======================================================================== */
    if(mode == SERVER) {
        /* SERVER socket initialization */
        sockfd = socket(AF_INET, SOCK_STREAM, 0);
        if (sockfd < 0) 
            LOG("ERROR opening socket");
        memset(&serv_addr, 0, sizeof(serv_addr));
        serv_addr.sin_family = AF_INET;
        serv_addr.sin_addr.s_addr = INADDR_ANY;
        serv_addr.sin_port = htons(DEFAULT_PORT);
        if (bind(sockfd, (struct sockaddr *) &serv_addr, sizeof(serv_addr)) < 0) 
            LOG("ERROR on binding");
        listen(sockfd,5);
        clilen = sizeof(cli_addr);
        
        /* SERVER: Wait for client connection */
        LOG("[SERVER] Waiting for the client to connect");
        newsockfd = accept(sockfd, (struct sockaddr *) &cli_addr, &clilen);
        if (newsockfd < 0) {
            LOG("ERROR connecting");
            exit(0);
        }
        LOG("Connected to the client");
        
        /* SERVER: Initial handshake - send "ok", wait for "ook" */
        char buf[128];
        snprintf(buf, sizeof(buf), "ok");
        if (write(newsockfd, buf, strlen(buf)+1) < 0) LOG("ERROR writing 'ok' to socket");
        
        memset(buf, 0, sizeof(buf));
        if (read_line(newsockfd, buf, sizeof(buf)) < 0) LOG("ERROR reading 'ook' from socket");
        if (strcmp(buf, "ook") == 0) {
            LOG("Handshake 'ok/ook' successful");
        } else {
            char elog[128];
            snprintf(elog, sizeof(elog), "Handshake failed: expected 'ook', got '%s'", buf);
            LOG(elog);
        }
        network_fd = newsockfd;
    }
    /* ========================================================================
     * CLIENT MODE
     * ======================================================================== */
    else if (mode == CLIENT) {
        /* CLIENT socket initialization */
        sockfd = socket(AF_INET, SOCK_STREAM, 0);
        if (sockfd < 0) LOG("ERROR opening socket");
    
        /* CLIENT: Resolve server hostname */
        server = gethostbyname(HOST_NAME);
        if (server == NULL) {
            LOG("ERROR, no such host\n");
            exit(0);
        }
    
        /* CLIENT: Connect to server */
        memset(&serv_addr, 0, sizeof(serv_addr));
        serv_addr.sin_family = AF_INET;
        memcpy(&serv_addr.sin_addr.s_addr, 
                server->h_addr_list[0], 
                server->h_length);
        serv_addr.sin_port = htons(DEFAULT_PORT);
    
        if (connect(sockfd,(struct sockaddr *)&serv_addr,sizeof(serv_addr)) < 0) {
            LOG("ERROR connecting");
            exit(0);
        }
        LOG("Connected to the server");
    
        /* CLIENT: Initial handshake - wait for "ok", send "ook" */
        char buf[128];
        memset(buf, 0, sizeof(buf));
        if (read_line(sockfd, buf, sizeof(buf)) < 0) LOG("ERROR reading 'ok' from socket");
        if (strcmp(buf, "ok") == 0) {
            snprintf(buf, sizeof(buf), "ook");
            if (write(sockfd, buf, strlen(buf)+1) < 0) LOG("ERROR writing 'ook' to socket");
            LOG("Handshake 'ok/ook' successful");
        } else {
            char elog[128];
            snprintf(elog, sizeof(elog), "Handshake failed: expected 'ok', got '%s'", buf);
            LOG(elog);
        }
        network_fd = sockfd;
    }
    
    signal(SIGPIPE, SIG_IGN);
    
    struct sigaction sa;
    sa.sa_handler = handle_signal;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;
    sigaction(SIGINT, &sa, NULL);
    sigaction(SIGTERM, &sa, NULL);

    // Pipe creation
    for (int i = 0; i < NUM_PROCESSES; i++) {
        if (pipe(pipe_parent_to_child[i]) == -1 ||
            pipe(pipe_child_to_parent[i]) == -1) {
            perror("pipe");
            exit(EXIT_FAILURE);
        }
    }
    LOG("Pipes created");

    for (int i = 0; i < NUM_PROCESSES; i++) {
        int flags = fcntl(pipe_child_to_parent[i][0], F_GETFL, 0);
        fcntl(pipe_child_to_parent[i][0], F_SETFL, flags | O_NONBLOCK);
    }

    char fd_pc[NUM_PROCESSES][16];
    char fd_cp[NUM_PROCESSES][16];
    char mode_str[16];
    sprintf(mode_str, "%d", mode);

    // String conversion for file descriptors
    for (int i = 0; i < NUM_PROCESSES; i++) {
        sprintf(fd_pc[i], "%d", pipe_parent_to_child[i][0]); // Reading end
        sprintf(fd_cp[i], "%d", pipe_child_to_parent[i][1]); // Writing end
    }

    //WATCHDOG
    if (mode == STANDALONE) {
        pid_W = fork();
        if (pid_W == 0) {
            for (int i = 0; i < NUM_PROCESSES; i++) {
                if (i != IDX_W) {
                    close(pipe_parent_to_child[i][0]);
                    close(pipe_parent_to_child[i][1]);
                    close(pipe_child_to_parent[i][0]);
                    close(pipe_child_to_parent[i][1]);
                }
            }
            execl("./build/Watchdog", "./build/Watchdog", fd_pc[IDX_W], fd_cp[IDX_W], NULL);
            perror("execl watchdog");
            exit(EXIT_FAILURE);
        }
        LOG("Watchdog fork");
    } else {
        /* NETWORK: Watchdog not used in network mode */
        pid_W = 0;
    }

    char watchdog_pid[16];
    sprintf(watchdog_pid, "%d", pid_W);

    /* ========================================================================
     * CLIENT MODE: Window Size Synchronization
     * ======================================================================== */
    if (mode == CLIENT) {
        LOG("CLIENT: Waiting for window size from server...");
        char buf[128];
        memset(buf, 0, sizeof(buf));
        if (read_line(network_fd, buf, sizeof(buf)) < 0) {
            perror("read size from server");
        }
        if (strncmp(buf, "size", 4) == 0) {
            sscanf(buf, "size %d,%d", &win_w, &win_h);
            char slog[128];
            snprintf(slog, sizeof(slog), "CLIENT: Received window size: %dx%d", win_w, win_h);
            LOG(slog);

            /* CLIENT: Send acknowledgment */
            snprintf(buf, sizeof(buf), "sok");
            write(network_fd, buf, strlen(buf)+1);
            LOG("CLIENT: Sent 'sok' acknowledgment");
        } else {
            char elog[128];
            snprintf(elog, sizeof(elog), "CLIENT: Handshake failed: expected 'size', got '%s'", buf);
            LOG(elog);
        }
    }

    //BLACKBOARD
    pid_B = fork();
    if (pid_B == 0) {
        // 
        for (int i = 0; i < NUM_PROCESSES; i++) {
            if (i != IDX_B) {
                close(pipe_parent_to_child[i][0]);
                close(pipe_parent_to_child[i][1]);
                close(pipe_child_to_parent[i][0]);
                close(pipe_child_to_parent[i][1]);
            }
        }
        close(pipe_parent_to_child[IDX_B][1]);
        close(pipe_child_to_parent[IDX_B][0]);

        // argv[1]=read_fd, argv[2]=write_fd
        execl("./build/Blackboard", "./build/Blackboard", fd_pc[IDX_B], fd_cp[IDX_B], watchdog_pid, mode_str, NULL);
        perror("execl blackboard");
        _exit(EXIT_FAILURE);
    }
    LOG("Blackboard fork");

    
    /* ========================================================================
     * CLIENT MODE: Forward window size to Blackboard
     * ======================================================================== */
    if (mode == CLIENT) {
        struct msg mb_size;
        mb_size.src = IDX_M;
        snprintf(mb_size.data, MSG_SIZE, "RESIZE %d %d", win_w, win_h);
        write(pipe_parent_to_child[IDX_B][1], &mb_size, sizeof(mb_size));
        LOG("CLIENT: Forwarded received window size to Blackboard");
    }

    //DRONE
    pid_D = fork();
    if (pid_D == 0){
        // 
        for (int i = 0; i < NUM_PROCESSES; i++) {
            if (i != IDX_D) {
                close(pipe_parent_to_child[i][0]);
                close(pipe_parent_to_child[i][1]);
                close(pipe_child_to_parent[i][0]);
                close(pipe_child_to_parent[i][1]);
            }
        }

        close(pipe_parent_to_child[IDX_D][1]);
        close(pipe_child_to_parent[IDX_D][0]);
        // 
        // 
        execl("./build/Drone", "./build/Drone", fd_pc[IDX_D], fd_cp[IDX_D], watchdog_pid, mode_str, NULL);
        perror("execl drone");
        exit(EXIT_FAILURE);
    }
    LOG("Drone fork");

    //KEYBOARD
    pid_I = fork();
    if (pid_I == 0)  {
        for (int i = 0; i < NUM_PROCESSES; i++) {
            if (i != IDX_I) {
                close(pipe_parent_to_child[i][0]);
                close(pipe_parent_to_child[i][1]);
                close(pipe_child_to_parent[i][0]);
                close(pipe_child_to_parent[i][1]);
            }
        }
        
        close(pipe_parent_to_child[IDX_I][1]); // Keyboard doesn't write to parent->child
        close(pipe_child_to_parent[IDX_I][0]); // Keyboard doesn't read from child->parent

        //
        execlp("konsole", "konsole", "-e", "./build/I_Keyboard",
            fd_pc[IDX_I], fd_cp[IDX_I], watchdog_pid, mode_str, NULL);
        perror("execl keyboard");
        _exit(EXIT_FAILURE);
    }
    LOG("Keyboard fork");

    //MAP
    pid_M = fork();
    if (pid_M == 0) {
        for (int i = 0; i < NUM_PROCESSES; i++) {
            if (i != IDX_M) {
                close(pipe_parent_to_child[i][0]);
                close(pipe_parent_to_child[i][1]);
                close(pipe_child_to_parent[i][0]);
                close(pipe_child_to_parent[i][1]);
            }
        }
        // 
        close(pipe_parent_to_child[IDX_M][1]); // Child doesn't write to parent->child
        close(pipe_child_to_parent[IDX_M][0]); // Child doesn't read from child->parent

        char win_w_str[16], win_h_str[16];
        sprintf(win_w_str, "%d", win_w);
        sprintf(win_h_str, "%d", win_h);

        execlp("konsole", "konsole", "-e", "./build/map", fd_pc[IDX_M], fd_cp[IDX_M], watchdog_pid, mode_str, win_w_str, win_h_str, NULL);
        perror("execl konsole map");
        _exit(EXIT_FAILURE);
    }
    LOG("Map fork");

    //OBSTACLES
    if (mode == STANDALONE) {
        pid_O = fork();
        if (pid_O == 0) {
            for (int i = 0; i < NUM_PROCESSES; i++) {
                if (i != IDX_O) {
                    close(pipe_parent_to_child[i][0]);
                    close(pipe_parent_to_child[i][1]);
                    close(pipe_child_to_parent[i][0]);
                    close(pipe_child_to_parent[i][1]);
                }
            }

            close(pipe_parent_to_child[IDX_O][1]);
            close(pipe_child_to_parent[IDX_O][0]);
            execl("./build/Obstacles", "./build/Obstacles", fd_pc[IDX_O], fd_cp[IDX_O], watchdog_pid, NULL);
            perror("execl obstacle");
            exit(EXIT_FAILURE);
        }
        LOG("Obstacles fork");
    }
    
    //TARGETS
    if (mode == STANDALONE) {
        pid_T = fork();
        if (pid_T == 0) {
            for (int i = 0; i < NUM_PROCESSES; i++) {
                if (i != IDX_T) {
                    close(pipe_parent_to_child[i][0]);
                    close(pipe_parent_to_child[i][1]);
                    close(pipe_child_to_parent[i][0]);
                    close(pipe_child_to_parent[i][1]);
                }
            }

            close(pipe_parent_to_child[IDX_T][1]);
            close(pipe_child_to_parent[IDX_T][0]);
            execl("./build/Targets", "./build/Targets", fd_pc[IDX_T], fd_cp[IDX_T], watchdog_pid, NULL);
            perror("execl targets");
            exit(EXIT_FAILURE);
        }
        LOG("Targets fork");
    }

    if (mode == STANDALONE){
        // PARENT PROCESS MAIN LOOP
        for (int i = 0; i < NUM_PROCESSES; i++) {
            close(pipe_parent_to_child[i][0]);
            close(pipe_child_to_parent[i][1]);
        }

        route_t route_table[NUM_PROCESSES];
        for (int i = 0; i < NUM_PROCESSES; i++) route_table[i].num = 0;
        
        route_table[IDX_I].dest[route_table[IDX_I].num++] = IDX_B;  //I->BB
        route_table[IDX_B].dest[route_table[IDX_B].num++] = IDX_D;  //BB->D
        route_table[IDX_B].dest[route_table[IDX_B].num++] = IDX_M;  //BB->M
        route_table[IDX_D].dest[route_table[IDX_D].num++] = IDX_B;  //D->BB
        route_table[IDX_M].dest[route_table[IDX_M].num++] = IDX_B;  //M->BB
        route_table[IDX_B].dest[route_table[IDX_B].num++] = IDX_O;  //BB->O
        route_table[IDX_O].dest[route_table[IDX_O].num++] = IDX_B;  //O->BB
        route_table[IDX_B].dest[route_table[IDX_B].num++] = IDX_T;  //BB->T
        route_table[IDX_T].dest[route_table[IDX_T].num++] = IDX_B;  //T->BB

        LOG("Route table created");

        while (1) {
            LOG("waiting for a message to redirect");
            fd_set rfds;
            FD_ZERO(&rfds);

            int maxfd = -1;

            // The parent must monitor all read ends of child->parent pipes
            for (int i = 0; i < NUM_PROCESSES; i++) {
                int fd = pipe_child_to_parent[i][0];
                if (fd >= 0) {
                    FD_SET(fd, &rfds);
                    if (fd > maxfd) maxfd = fd;
                }
            }

            // Wait for a message from ANY child
            int ret = select(maxfd + 1, &rfds, NULL, NULL, NULL);
            if (ret < 0) {
                perror("select");
                break;
            }

            int esc_received = 0;
            // Check which child sent data
            for (int src = 0; src < NUM_PROCESSES; src++) {
                int read_fd = pipe_child_to_parent[src][0];

                if (FD_ISSET(read_fd, &rfds)) {
                    struct msg m;
                    int n = read(read_fd, &m, sizeof(m));

                    if (n <= 0) {
                        // Child terminated -> close its reading end
                        close(read_fd);
                        pipe_child_to_parent[src][0] = -1;
                        continue;
                    }

                    if (src == IDX_I) {
                        char dbg[64];
                        snprintf(dbg, sizeof(dbg), "DEBUG: Main read from Keyboard: %d bytes, key=%c", n, m.data[0]);
                        LOG(dbg);
                    }

                    if (strncmp(m.data, "ESC", 3) == 0){
                        printf("ESC RECEIVED\n");
                        esc_received = 1;
                    }

                    // Destination from route_table
                    for (int d = 0; d < route_table[src].num; d++) {
                        int dst = route_table[src].dest[d];
                        int write_fd = pipe_parent_to_child[dst][1];

                        ssize_t w = write(write_fd, &m, sizeof(m));
                        if (w == -1) {
                            perror("write to child");
                            fprintf(stderr, "SIGPIPE likely on src=%d -> dst=%d fd=%d\n", src, dst, write_fd);
                        } else if (w != n) {
                            fprintf(stderr, "Partial write src=%d -> dst=%d fd=%d written=%zd expected=%d\n",
                                src, dst, write_fd, w, n);
                        } else {
                            // Confirm write success
                            if (src == IDX_I) {
                                char tlog[128];
                                snprintf(tlog, sizeof(tlog), "DEBUG: Main routed Keyboard msg (src=%d, key=%c) to BB (fd=%d)", m.src, m.data[0], write_fd);
                                LOG(tlog);
                            }
                        }
                        char log_msg[128];
                        snprintf(log_msg, sizeof(log_msg), "Message redirected from %s to %s", process_names[src], process_names[dst]);
                        LOG(log_msg);
                    }
                    
                }
            }
            if (esc_received){
                LOG("ESC received, closing...");
                for (int i = 0; i < NUM_PROCESSES; i++) {
                    int write_fd = pipe_parent_to_child[i][1];
                    struct msg esc_msg;
                    esc_msg.src = IDX_B;
                    strncpy(esc_msg.data, "ESC", MSG_SIZE);
                    write(write_fd, &esc_msg, sizeof(esc_msg));
                }
                break;
            }
        }
    }
    /* ========================================================================
     * NETWORK MODE: Main Loop for SERVER/CLIENT
     * ======================================================================== */
    else if (mode == SERVER || mode == CLIENT){
        if (mode == SERVER)
            LOG("[SERVER] Creating route table");
        else
            LOG("[CLIENT] Creating route table");

        /* NETWORK: Close unused process pipes (Obstacles, Targets, Watchdog) */
        for (int i = 0; i < NUM_PROCESSES; i++) {
            if (i == IDX_O || i == IDX_T || i == IDX_W){
                close(pipe_parent_to_child[i][0]);
                close(pipe_parent_to_child[i][1]);
                close(pipe_child_to_parent[i][0]);
                close(pipe_child_to_parent[i][1]);

                pipe_parent_to_child[i][1] = -1;
                pipe_child_to_parent[i][0] = -1;
            } else {
                close(pipe_parent_to_child[i][0]);
                close(pipe_child_to_parent[i][1]);
            }
        }

        /* NETWORK: Simplified route table (no Obstacles/Targets/Watchdog) */
        route_t route_table[NUM_PROCESSES];
        for (int i = 0; i < NUM_PROCESSES; i++) route_table[i].num = 0;

        route_table[IDX_I].dest[route_table[IDX_I].num++] = IDX_B;  // I->BB
        route_table[IDX_B].dest[route_table[IDX_B].num++] = IDX_D;  // BB->D
        route_table[IDX_B].dest[route_table[IDX_B].num++] = IDX_M;  // BB->M (Local)
        route_table[IDX_D].dest[route_table[IDX_D].num++] = IDX_B;  // D->BB
        route_table[IDX_M].dest[route_table[IDX_M].num++] = IDX_B;  // M->BB

        route_table[IDX_B].dest[route_table[IDX_B].num++] = IDX_O; // BB->Remote (virtual)
        route_table[IDX_O].dest[route_table[IDX_O].num++] = IDX_B; // Remote->BB (virtual)
        
        /* NETWORK: State tracking variables */
        static int size_sent = 0;                    // SERVER: Window size sent to CLIENT
        static int local_drone_x = 0, local_drone_y = 0;  // CLIENT: Local drone position
        static int server_drone_x = 0, server_drone_y = 0; // SERVER: Server drone position
        static int server_drone_dirty = 0;           // SERVER: Drone position changed flag
        
        static unsigned long last_drone_ms = 0;      // Timestamp for drone sync
        static unsigned long last_obst_ms = 0;       // Timestamp for obstacle request

        /* NETWORK: Main event loop */
        int running = 1;
        while(running){
            fd_set rfds;
            FD_ZERO(&rfds);
            int maxfd = -1;
            
            /* NETWORK: Monitor local process pipes */
            for (int i = 0; i<NUM_PROCESSES; i++){
                int fd = pipe_child_to_parent[i][0];
                if(fd != -1){
                    FD_SET(fd, &rfds);
                    if (fd > maxfd) maxfd = fd;
                }
            }
            
            /* NETWORK: Monitor network socket for remote messages */
            if (network_fd > 0){
                FD_SET(network_fd, &rfds);
                if (network_fd > maxfd) maxfd = network_fd;
            }

            /* Wait for activity on pipes or network socket */
            int ret = select(maxfd +1, &rfds, NULL, NULL, NULL);
            if (ret < 0) {
                perror("select server");
                break;
            }

            /* ====================================================================
             * NETWORK PROTOCOL: CLIENT Message Handling
             * ==================================================================== */
            if (mode == CLIENT && network_fd > 0 && FD_ISSET(network_fd, &rfds)) {
                char sbuf[128];
                memset(sbuf, 0, sizeof(sbuf));
                if (read_line(network_fd, sbuf, sizeof(sbuf)) > 0) {
                    /* NETWORK: Handle exit command */
                    if (strncmp(sbuf, "q", 1) == 0) {
                        LOG("CLIENT: Received exit command 'q'");
                        snprintf(sbuf, sizeof(sbuf), "qok");
                        write(network_fd, sbuf, strlen(sbuf) + 1);
                        LOG("MAIN (CLIENT): Cleaning up children...");
                        clean_children();
                        running = 0;
                    }
                    /* NETWORK: Receive SERVER drone position */
                    else if (strncmp(sbuf, "drone", 5) == 0) {
                        memset(sbuf, 0, sizeof(sbuf));
                        if (read_line(network_fd, sbuf, sizeof(sbuf)) > 0) {
                            int dx, dy;
                            if (sscanf(sbuf, "%d, %d", &dx, &dy) == 2) {
                                /* NETWORK: Acknowledge receipt */
                                snprintf(sbuf, sizeof(sbuf), "dok");
                                write(network_fd, sbuf, strlen(sbuf) + 1);
                                LOG("NETWORK (CLIENT): Received 'dok' from server");
                                /* NETWORK: Forward to Blackboard as remote obstacle */
                                struct msg m_remote;
                                m_remote.src = IDX_O;
                                snprintf(m_remote.data, MSG_SIZE, "REMOTE %d, %d", dx, dy);
                                write(pipe_parent_to_child[IDX_B][1], &m_remote, sizeof(m_remote));
                            }
                        }
                    }
                    /* NETWORK: Send CLIENT drone position to SERVER */
                    else if (strncmp(sbuf, "obst", 4) == 0) {
                        LOG("NETWORK (CLIENT): Received 'obst' request from server");
                        // Server asking for Client's drone position (obst)
                        snprintf(sbuf, sizeof(sbuf), "%d, %d", local_drone_x, local_drone_y);
                        write(network_fd, sbuf, strlen(sbuf) + 1);
                        LOG("NETWORK (CLIENT): Sent local drone position to server");
                        
                        /* NETWORK: Wait for acknowledgment */
                        memset(sbuf, 0, sizeof(sbuf));
                        if (read_line(network_fd, sbuf, sizeof(sbuf)) > 0) {
                            if (strncmp(sbuf, "pok", 3) == 0) {
                                LOG("NETWORK (CLIENT): Received 'pok' from server");
                            }
                        }
                    }
                }
            }
            
            /* ====================================================================
             * NETWORK PROTOCOL: SERVER Time-Based Synchronization
             * ==================================================================== */
            struct timeval tv;
            gettimeofday(&tv, NULL);
            unsigned long current_ms = tv.tv_sec * 1000 + tv.tv_usec / 1000;

            if (mode == SERVER && size_sent && network_fd > 0) {
                /* NETWORK: Obstacle request (20Hz) */
                if (current_ms - last_obst_ms >= OBST_SYNC_MS) {
                    char remote_msg[100] = "obst";
                    write(network_fd, &remote_msg, strlen(remote_msg)+1);
                    LOG("NETWORK (SERVER): Sent 'obst' request to client");
                    
                    /* NETWORK: Receive CLIENT drone position */
                    char sbuf[128];
                    memset(sbuf, 0, sizeof(sbuf));
                    if (read_line(network_fd, sbuf, sizeof(sbuf)) > 0) {
                        int ox, oy;
                        if (sscanf(sbuf, "%d, %d", &ox, &oy) == 2) {
                            LOG("NETWORK (SERVER): Valid obstacle position received");
                            
                            /* NETWORK: Forward to Blackboard as obstacle */
                            struct msg m_obst;
                            m_obst.src = IDX_O;
                            snprintf(m_obst.data, MSG_SIZE, "O=%d,%d", ox, oy);
                            write(pipe_parent_to_child[IDX_B][1], &m_obst, sizeof(m_obst));
                            
                            /* NETWORK: Send acknowledgment */
                            snprintf(remote_msg, sizeof(remote_msg), "pok");
                            write(network_fd, &remote_msg, strlen(remote_msg)+1);
                            LOG("NETWORK (SERVER): Sent 'pok' to client");
                        }
                    }
                    last_obst_ms = current_ms;
                }

                /* NETWORK: Drone sync (10Hz) */
                //if (server_drone_dirty && (current_ms - last_drone_ms >= DRONE_SYNC_MS)) {
                if ((current_ms - last_drone_ms >= DRONE_SYNC_MS)) {
                    char remote_msg[100] = "drone";
                    write(network_fd, &remote_msg, strlen(remote_msg)+1);
                    LOG("NETWORK (SERVER): Sent 'drone' command to client");
                    
                    /* NETWORK: Send SERVER drone position */
                    snprintf(remote_msg, sizeof(remote_msg), "%d, %d", server_drone_x, server_drone_y);
                    write(network_fd, &remote_msg, strlen(remote_msg)+1);
                    LOG("NETWORK (SERVER): Sent server drone position to client");
                    
                    /* NETWORK: Wait for acknowledgment */
                    char sbuf[128];
                    memset(sbuf, 0, sizeof(sbuf));
                    if (read_line(network_fd, sbuf, sizeof(sbuf)) > 0) {
                        if (strncmp(sbuf, "dok", 3) == 0) {
                            LOG("SERVER: Received 'dok' from client");
                        }
                    }
                    server_drone_dirty = 0;
                    last_drone_ms = current_ms;
                }
            }

            /* ====================================================================
             * NETWORK MODE: Local Process Message Routing
             * ==================================================================== */
            for (int src = 0; src < NUM_PROCESSES; src++) {
                int read_fd = pipe_child_to_parent[src][0];

                if (read_fd != -1 && FD_ISSET(read_fd, &rfds)) {
                    struct msg m;
                    while (1) {
                        ssize_t n = read(read_fd, &m, sizeof(m));
                        if (n <= 0) {
                            if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) break; 
                            close(read_fd);
                            pipe_child_to_parent[src][0] = -1;
                            break;
                        }

                        /* ================================================================
                         * NETWORK: Window Size Handshake (SERVER only)
                         * ================================================================ */
                        if (mode == SERVER && !size_sent && m.src == IDX_M && strncmp(m.data, "RESIZE", 6) == 0) {
                            int w, h;
                            sscanf(m.data, "RESIZE %d %d", &w, &h);
                            char sbuf[128];
                            memset(sbuf, 0, sizeof(sbuf));
                            snprintf(sbuf, sizeof(sbuf), "size %d,%d", w, h);
                            LOG("SERVER: Sending window size to client...");
                            write(network_fd, sbuf, strlen(sbuf)+1);

                            /* NETWORK: Wait for acknowledgment */
                            memset(sbuf, 0, sizeof(sbuf));
                            if (read_line(network_fd, sbuf, sizeof(sbuf)) > 0) {
                                if (strncmp(sbuf, "sok", 3) == 0) {
                                    LOG("SERVER: Received 'sok' from client");
                                    size_sent = 1;
                                }
                            }
                        }

                        /* ================================================================
                         * NETWORK: ESC Key Shutdown Protocol
                         * ================================================================ */
                        if (src == IDX_I && m.data[0] == 27) {
                            if (mode == SERVER && network_fd > 0) {
                                LOG("SERVER: Initiating shutdown protocol with client...");
                                char qmsg[16] = "q";
                                write(network_fd, qmsg, strlen(qmsg) + 1);
                                
                                char qbuf[128];
                                memset(qbuf, 0, sizeof(qbuf));
                                if (read_line(network_fd, qbuf, sizeof(qbuf)) > 0) {
                                    if (strncmp(qbuf, "qok", 3) == 0) {
                                        LOG("SERVER: Received 'qok', shutting down.");
                                    }
                                }
                            }
                            LOG("MAIN: ESC detected, cleaning up...");
                            clean_children();
                            running = 0;
                            break;
                        }

                        /* ================================================================
                         * NETWORK: Message Routing with Network
                         * ================================================================ */
                        for (int d = 0; d < route_table[src].num; d++) {
                            int dst = route_table[src].dest[d];

                            // Efficiency: Skip unnecessary processes
                            if (src == IDX_B) {
                                if (strncmp(m.data, "D=", 2) == 0 && dst == IDX_D) continue;
                                if (strncmp(m.data, "STATS", 5) == 0 && dst == IDX_D) continue;
                            }

                            /* NETWORK: Track SERVER drone position for synchronization */
                            if (mode == SERVER && src == IDX_B && strncmp(m.data, "D=", 2) == 0) {
                                if (sscanf(m.data, "D=%d,%d", &server_drone_x, &server_drone_y) == 2) {
                                    //server_drone_dirty = 1;
                                }
                            }
                            
                            /* NETWORK: Track CLIENT drone position for obstacle requests */
                            if (mode == CLIENT && src == IDX_B && strncmp(m.data, "D=", 2) == 0) {
                                sscanf(m.data, "D=%d,%d", &local_drone_x, &local_drone_y);
                            }

                            int write_fd = pipe_parent_to_child[dst][1];
                            if (write_fd != -1){
                                write(write_fd, &m, sizeof(m));
                            }
                        }
                    }
                }
            }    
        }
    }

    clean_children();

    waitpid(pid_B, NULL, 0);
    waitpid(pid_D, NULL, 0);
    waitpid(pid_I, NULL, 0);
    waitpid(pid_M, NULL, 0);
    if (pid_O > 0) waitpid(pid_O, NULL, 0);
    if (pid_T > 0) waitpid(pid_T, NULL, 0);
    if (pid_W > 0) waitpid(pid_W, NULL, 0);
    LOG("Execution terminated correctly");
    return 0;
}