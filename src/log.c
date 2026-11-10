#include <stdarg.h>
#include <time.h>
#include <pthread.h>
#include <sys/stat.h>
#include "common.h"
#include "log.h"

static pthread_mutex_t log_lock = PTHREAD_MUTEX_INITIALIZER;

void log_event(const char *fmt, ...)
{
    char stamp[32];
    time_t now = time(NULL);
    struct tm tm_now;
    FILE *fp;
    va_list ap;

    localtime_r(&now, &tm_now);
    strftime(stamp, sizeof(stamp), "%Y-%m-%d %H:%M:%S", &tm_now);

    pthread_mutex_lock(&log_lock);

    if (mkdir(LOG_DIR, 0755) != 0 && errno != EEXIST) {
        perror("log: mkdir " LOG_DIR);
        pthread_mutex_unlock(&log_lock);
        return;
    }

    fp = fopen(LOG_FILE, "a");
    if (fp == NULL) {
        perror("log: fopen " LOG_FILE);
        pthread_mutex_unlock(&log_lock);
        return;
    }

    fprintf(fp, "[%s] ", stamp);
    va_start(ap, fmt);
    vfprintf(fp, fmt, ap);
    va_end(ap);
    fprintf(fp, "\n");
    fclose(fp);

    pthread_mutex_unlock(&log_lock);
}
