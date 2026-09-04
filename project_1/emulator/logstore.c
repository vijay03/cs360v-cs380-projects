/* logstore.c: thread-safe log store core (PROVIDED). See logstore.h. */
#include "logstore.h"

#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>

struct logstore {
    FILE           *f;
    pthread_mutex_t lock;
};

static const char *level_name(uint32_t level)
{
    switch (level) {
    case 0:  return "DEBUG";
    case 1:  return "INFO";
    case 2:  return "WARN";
    case 3:  return "ERROR";
    default: return "LVL?";
    }
}

logstore *logstore_open(const char *path)
{
    logstore *ls = calloc(1, sizeof *ls);
    if (!ls)
        return NULL;
    ls->f = fopen(path, "w");
    if (!ls->f) {
        free(ls);
        return NULL;
    }
    pthread_mutex_init(&ls->lock, NULL);
    return ls;
}

void logstore_close(logstore *ls)
{
    if (!ls)
        return;
    if (ls->f)
        fclose(ls->f);
    pthread_mutex_destroy(&ls->lock);
    free(ls);
}

void logstore_append(logstore *ls, uint32_t seq, uint32_t level,
                     const void *bytes, uint32_t len)
{
    pthread_mutex_lock(&ls->lock);
    fprintf(ls->f, "[%u] %s ", seq, level_name(level));
    if (len)
        fwrite(bytes, 1, len, ls->f);
    fputc('\n', ls->f);
    fflush(ls->f);
    pthread_mutex_unlock(&ls->lock);
}

void logstore_flush(logstore *ls)
{
    pthread_mutex_lock(&ls->lock);
    fflush(ls->f);
    pthread_mutex_unlock(&ls->lock);
}
