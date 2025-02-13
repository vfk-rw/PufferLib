#include "log.h"
#include <stdlib.h>

LogBuffer *allocate_logbuffer(int size)
{
    LogBuffer *logs = (LogBuffer *)calloc(1, sizeof(LogBuffer));
    logs->logs = (Log *)calloc(size, sizeof(Log));
    logs->length = size;
    logs->idx = 0;
    return logs;
}

void free_logbuffer(LogBuffer *buffer)
{
    if (buffer)
    {
        if (buffer->logs)
            free(buffer->logs);
        free(buffer);
    }
}

void add_log(LogBuffer *logs, Log *log)
{
    if (logs->idx < logs->length)
    {
        logs->logs[logs->idx] = *log;
        logs->idx++;
    }
}

Log aggregate_and_clear(LogBuffer *logs)
{
    Log aggregate = {0, 0};
    if (logs->idx == 0)
        return aggregate;
    for (int i = 0; i < logs->idx; i++)
    {
        aggregate.episode_return += logs->logs[i].episode_return;
        aggregate.episode_length += logs->logs[i].episode_length;
    }
    aggregate.episode_return /= logs->idx;
    aggregate.episode_length /= logs->idx;
    logs->idx = 0;
    return aggregate;
}
