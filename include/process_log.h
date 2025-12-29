#ifndef PROCESS_LOG_H
#define PROCESS_LOG_H

/**
 * Log a message with the process name and current timestamp.
 */
void process_log(const char *process_name, const char *message);

/**
 * Register a process startup in the system logs.
 */
void register_process(const char *name);

#endif
