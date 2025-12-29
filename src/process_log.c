#define _POSIX_C_SOURCE 200809L

#include "../include/process_log.h"

#include <stdio.h>
#include <fcntl.h>
#include <unistd.h>
#include <string.h>
#include <time.h>
#include <stdarg.h>
#include <sys/types.h>

#define SYSTEM_LOG_FILE "log/system.log"

void process_log(const char *process_name, const char *message)
{
    int fd = open(SYSTEM_LOG_FILE, O_WRONLY | O_CREAT | O_APPEND, 0644);
    if (fd == -1)
        return;

    struct flock lock;
    memset(&lock, 0, sizeof(lock));
    lock.l_type = F_WRLCK;
    lock.l_whence = SEEK_SET;

    if (fcntl(fd, F_SETLKW, &lock) == -1) {
        close(fd);
        return;
    }

    time_t now = time(NULL);
    struct tm tm;
    localtime_r(&now, &tm);

    char time_str[16];
    strftime(time_str, sizeof(time_str), "%H:%M:%S", &tm);

    dprintf(fd, "%s %s %s\n", time_str, process_name, message);

    fsync(fd);

    lock.l_type = F_UNLCK;
    fcntl(fd, F_SETLK, &lock);
    close(fd);
}

void register_process(const char *name){
    int fd = open("log/processes_pid.log", O_WRONLY | O_CREAT | O_APPEND, 0644);
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

    // Flush output
    fsync(fd);

    // Unlock the file
    lock.l_type = F_UNLCK;
    if (fcntl(fd, F_SETLK, &lock) == -1){
        perror("fcntl unlock");
    }

    close(fd);
}
