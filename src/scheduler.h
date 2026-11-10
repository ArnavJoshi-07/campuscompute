#ifndef SCHEDULER_H
#define SCHEDULER_H

#include "common.h"
#include "jobs.h"

/* only the first MAX_SEGS slices are kept for printing the timeline */
#define MAX_SEGS 200

struct segment {
    int id;     /* 0 means the CPU was idle */
    int start;
    int end;
};

struct schedule {
    char name[32];
    struct job jobs[MAX_JOBS];
    int n;
    struct segment segs[MAX_SEGS];
    int nseg;   /* total slices, can be more than MAX_SEGS */
    int total_tat;
    int total_wt;
    double avg_tat;
    double avg_wt;
};

int cmd_fcfs(int argc, char **argv);
int cmd_rr(int argc, char **argv);
int cmd_priority(int argc, char **argv);
int cmd_compare(int argc, char **argv);

void run_fcfs(const struct job *jobs, int n, struct schedule *s, int verbose);
void run_rr(const struct job *jobs, int n, int quantum, struct schedule *s, int verbose);
void run_priority(const struct job *jobs, int n, struct schedule *s, int verbose);

#endif
