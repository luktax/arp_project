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
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>

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

int mode = 100;
int server_fd, client_fd, network_fd = -1; // Network file descriptors
int win_w = 155, win_h = 30; // Default window dimensions

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

    // Network Setup
    //Socket variables
    int sockfd, newsockfd, clilen, n;
    struct sockaddr_in serv_addr, cli_addr;
    struct hostent *server;
    char msg_sock[100];

    if(mode == SERVER)       //---------Socket part Server-------
    {
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
        
        // Accept the connection
        LOG("[SERVER] Waiting for the client to connect");
        newsockfd = accept(sockfd, (struct sockaddr *) &cli_addr, &clilen);
        if (newsockfd < 0) 
        {
            LOG("ERROR connecting");
            exit(0);
        }
        LOG("Connected to the client");
        
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
    else if (mode == CLIENT)       //--------------Socket part Client-------------
    {
        sockfd = socket(AF_INET, SOCK_STREAM, 0);
        if (sockfd < 0) LOG("ERROR opening socket");
    
        server = gethostbyname(HOST_NAME);
        if (server == NULL) 
        {
            LOG("ERROR, no such host\n");
            exit(0);
        }
    
        memset(&serv_addr, 0, sizeof(serv_addr));
        serv_addr.sin_family = AF_INET;
        memcpy(&serv_addr.sin_addr.s_addr, 
                server->h_addr_list[0], 
                server->h_length);
        serv_addr.sin_port = htons(DEFAULT_PORT);
    
        if (connect(sockfd,(struct sockaddr *)&serv_addr,sizeof(serv_addr)) < 0) 
        {
            LOG("ERROR connecting");
            exit(0);
        }
        LOG("Connected to the server");
    
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
    LOG("Pipe created");

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
            //
            execl("./build/Watchdog", "./build/Watchdog", fd_pc[IDX_W], fd_cp[IDX_W], NULL);
            perror("execl watchdog");
            exit(EXIT_FAILURE);
        }
        LOG("Watchdog fork");
    } else {
        pid_W = 0; // Ensure it's 0 if not forked
    }

    char watchdog_pid[16];
    sprintf(watchdog_pid, "%d", pid_W);

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

            // Send acknowledgment
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
    
    if (mode == CLIENT) {
        // Forward the initial size received from server to Blackboard
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
            // 
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
            // 
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
    else if (mode == SERVER || mode == CLIENT){
        if (mode == SERVER)
            LOG("[SERVER] Creating route table");
        else
            LOG("[CLIENT] Creating route table");

        //int IDX_R = IDX_O; //remote
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

        route_t route_table[NUM_PROCESSES];
        for (int i = 0; i < NUM_PROCESSES; i++) route_table[i].num = 0;

        route_table[IDX_I].dest[route_table[IDX_I].num++] = IDX_B;  // I->BB
        route_table[IDX_B].dest[route_table[IDX_B].num++] = IDX_D;  // BB->D
        route_table[IDX_B].dest[route_table[IDX_B].num++] = IDX_M;  // BB->M (Locale)
        route_table[IDX_D].dest[route_table[IDX_D].num++] = IDX_B;  // D->BB
        route_table[IDX_M].dest[route_table[IDX_M].num++] = IDX_B;  // M->BB

        route_table[IDX_B].dest[route_table[IDX_B].num++] = IDX_O; //BB->R
        route_table[IDX_O].dest[route_table[IDX_O].num++] = IDX_B; //R->BB
        
        static int size_sent = 0;
        static int local_drone_x = 0, local_drone_y = 0; // Track client drone for 'obst' requests
        static int server_drone_x = 0, server_drone_y = 0; // Track server drone for network updates
        static int server_drone_dirty = 0;

        while(1){
            fd_set rfds;
            FD_ZERO(&rfds);
            int maxfd = -1;
            
            //local processes
            for (int i = 0; i<NUM_PROCESSES; i++){
                int fd = pipe_child_to_parent[i][0];
                if(fd != -1){
                    FD_SET(fd, &rfds);
                    if (fd > maxfd) maxfd = fd;
                }
            }
            
            //from remote
            if (network_fd > 0){
                FD_SET(network_fd, &rfds);
                if (network_fd > maxfd) maxfd = network_fd;
            }

            //select
            int ret = select(maxfd +1, &rfds, NULL, NULL, NULL);
            if (ret < 0) {
                perror("select server");
                break;
            }

            // --- CLIENT Network Protocol ---
            if (mode == CLIENT && network_fd > 0 && FD_ISSET(network_fd, &rfds)) {
                char sbuf[128];
                memset(sbuf, 0, sizeof(sbuf));
                if (read_line(network_fd, sbuf, sizeof(sbuf)) > 0) {
                    if (strncmp(sbuf, "q", 1) == 0) {
                        LOG("CLIENT: Received exit command 'q'");
                        snprintf(sbuf, sizeof(sbuf), "qok");
                        write(network_fd, sbuf, strlen(sbuf) + 1);
                        break; // Exit loop
                    }
                    else if (strncmp(sbuf, "drone", 5) == 0) {
                        // Server drone position update
                        memset(sbuf, 0, sizeof(sbuf));
                        if (read_line(network_fd, sbuf, sizeof(sbuf)) > 0) {
                            int dx, dy;
                            if (sscanf(sbuf, "%d, %d", &dx, &dy) == 2) {
                                // Send dok
                                snprintf(sbuf, sizeof(sbuf), "dok");
                                write(network_fd, sbuf, strlen(sbuf) + 1);

                                // Forward to local Blackboard as REMOTE
                                struct msg m_remote;
                                m_remote.src = IDX_O; // Act as remote source
                                snprintf(m_remote.data, MSG_SIZE, "REMOTE %d, %d", dx, dy);
                                write(pipe_parent_to_child[IDX_B][1], &m_remote, sizeof(m_remote));
                            }
                        }
                    }
                    else if (strncmp(sbuf, "obst", 4) == 0) {
                        // Server asking for Client's drone position (obst)
                        snprintf(sbuf, sizeof(sbuf), "%d, %d", local_drone_x, local_drone_y);
                        write(network_fd, sbuf, strlen(sbuf) + 1);
                        
                        // Wait for pok
                        memset(sbuf, 0, sizeof(sbuf));
                        if (read_line(network_fd, sbuf, sizeof(sbuf)) > 0) {
                            if (strncmp(sbuf, "pok", 3) == 0) {
                                // OK
                            }
                        }
                    }
                }
            }
            
            // SERVER protocol network
            static int obst_count = 0;
            if(mode == SERVER && size_sent && (obst_count++ % 10 == 0)){
                // REMOTE
                char remote_msg[100] = "obst";
                write(network_fd, &remote_msg, strlen(remote_msg)+1);
                LOG("SERVER: sent obst");
                // Wait for obst position
                char sbuf[128];
                memset(sbuf, 0, sizeof(sbuf));
                if (read_line(network_fd, sbuf, sizeof(sbuf)) > 0) {
                    int ox, oy;
                    if (sscanf(sbuf, "%d, %d", &ox, &oy) == 2) {
                        LOG("SERVER: Received obstacle from client");
                        
                        // Forward to Blackboard
                        struct msg m_obst;
                        m_obst.src = IDX_O;
                        snprintf(m_obst.data, MSG_SIZE, "O=%d,%d", ox, oy);
                        write(pipe_parent_to_child[IDX_B][1], &m_obst, sizeof(m_obst));
                        
                        // Send pok
                        snprintf(remote_msg, sizeof(remote_msg), "pok");
                        write(network_fd, &remote_msg, strlen(remote_msg)+1);
                        LOG("SERVER: sent pok");
                    }
                }
            }

            // SERVER protocol network: Drone position
            static int drone_count = 0;
            if(mode == SERVER && size_sent && server_drone_dirty && (drone_count++ % 5 == 0)){
                if(network_fd > 0){
                    char remote_msg[100] = "drone";
                    write(network_fd, &remote_msg, strlen(remote_msg)+1);
                    
                    snprintf(remote_msg, sizeof(remote_msg), "%d, %d", server_drone_x, server_drone_y);
                    write(network_fd, &remote_msg, strlen(remote_msg)+1);
                    
                    // Wait for dok
                    char sbuf[128];
                    memset(sbuf, 0, sizeof(sbuf));
                    if (read_line(network_fd, sbuf, sizeof(sbuf)) > 0) {
                        if (strncmp(sbuf, "dok", 3) == 0) {
                            // LOG("SERVER: Received 'dok' from client");
                        }
                    }
                    server_drone_dirty = 0;
                }
            }

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

                    // Handshake: size -> sok (Server side)
                    if (mode == SERVER && !size_sent && m.src == IDX_M && strncmp(m.data, "RESIZE", 6) == 0) {
                        int w, h;
                        sscanf(m.data, "RESIZE %d %d", &w, &h);
                        char sbuf[128];
                        memset(sbuf, 0, sizeof(sbuf));
                        snprintf(sbuf, sizeof(sbuf), "size %d,%d", w, h);
                        LOG("SERVER: Sending window size to client...");
                        write(network_fd, sbuf, strlen(sbuf)+1);

                        // Wait for sok
                        memset(sbuf, 0, sizeof(sbuf));
                        if (read_line(network_fd, sbuf, sizeof(sbuf)) > 0) {
                            if (strncmp(sbuf, "sok", 3) == 0) {
                                LOG("SERVER: Received 'sok' from client");
                                size_sent = 1;
                            }
                        }
                    }

                    // Intercept ESC key for server shutdown
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
                        break; // Break the main while(1) loop
                    }

                    // Targeted Routing for Blackboard (IDX_B)
                    for (int d = 0; d < route_table[src].num; d++) {
                        int dst = route_table[src].dest[d];

                        // Efficiency: Skip unnecessary processes
                        if (src == IDX_B) {
                            if (strncmp(m.data, "D=", 2) == 0 && dst == IDX_D) continue; // Drone doesn't need its own pos
                            if (strncmp(m.data, "STATS", 5) == 0 && dst == IDX_D) continue; // Drone doesn't need stats
                        }

                        // Track server drone position to be sent via network at throttled frequency
                        if (mode == SERVER && src == IDX_B && strncmp(m.data, "D=", 2) == 0) {
                            if (sscanf(m.data, "D=%d,%d", &server_drone_x, &server_drone_y) == 2) {
                                server_drone_dirty = 1;
                            }
                        }
                        
                        // Track local drone position on Client for 'obst' requests
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