#ifndef LOG_H
#define LOG_H

#define LOG_BUFFER_SIZE 1024

typedef struct Log
{
    float episode_return;
    float episode_length;
} Log;

typedef struct LogBuffer
{
    Log *logs;
    int length;
    int idx;
} LogBuffer;

LogBuffer *allocate_logbuffer(int size);
void free_logbuffer(LogBuffer *buffer);
void add_log(LogBuffer *logs, Log *log);
Log aggregate_and_clear(LogBuffer *logs);

#endif
