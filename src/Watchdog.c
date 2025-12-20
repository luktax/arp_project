#include <stdio.h>
#include <unistd.h>
#include <fcntl.h>
#include <string.h>
#include <errno.h>

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
    register_process("Watchdog");
}

