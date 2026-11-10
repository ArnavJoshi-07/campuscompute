#ifndef JOBS_H
#define JOBS_H

#include <stdio.h>

/* upper bound for arrival and burst so the time math can't overflow */
#define MAX_TIME 100000

enum job_status {
    JOB_NEW,
    JOB_READY,
    JOB_RUNNING,
    JOB_DONE
};

struct job {
    int id;
    int arrival;
    int burst;
    int priority;
    enum job_status status;
    int start;
    int completion;
    int turnaround;
    int waiting;
};

int cmd_jobs(int argc, char **argv);

int load_jobs(const char *path, struct job *jobs, int *count);
void print_jobs(const struct job *jobs, int count);
const char *status_name(enum job_status s);

/* small parsing helpers, also used by banker.c and logger.c */
int read_data_line(FILE *fp, char *buf, int size, int *lineno);
int parse_int_line(const char *line, int *vals, int max);
int parse_positive(const char *s, int max, int *out);

#endif
