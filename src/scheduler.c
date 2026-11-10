#include "common.h"
#include "log.h"
#include "jobs.h"
#include "scheduler.h"

#define TIE_RULE "tie rule: earlier arrival first, then lower job id"
#define SEGS_PER_LINE 6

/* ready queue for round robin, a job is never in it twice */
struct ready_queue {
    int items[MAX_JOBS];
    int head;
    int count;
};

static void rq_push(struct ready_queue *q, int x)
{
    q->items[(q->head + q->count) % MAX_JOBS] = x;
    q->count++;
}

static int rq_pop(struct ready_queue *q)
{
    int x = q->items[q->head];

    q->head = (q->head + 1) % MAX_JOBS;
    q->count--;
    return x;
}

static int cmp_arrival(const void *a, const void *b)
{
    const struct job *x = a;
    const struct job *y = b;

    if (x->arrival != y->arrival)
        return x->arrival - y->arrival;
    return x->id - y->id;
}

static int cmp_id(const void *a, const void *b)
{
    const struct job *x = a;
    const struct job *y = b;

    return x->id - y->id;
}

static void init_schedule(struct schedule *s, const char *name, const struct job *jobs, int n)
{
    memset(s, 0, sizeof(*s));
    snprintf(s->name, sizeof(s->name), "%s", name);
    s->n = n;
    for (int i = 0; i < n; i++) {
        s->jobs[i] = jobs[i];
        s->jobs[i].status = JOB_NEW;
        s->jobs[i].start = -1;
        s->jobs[i].completion = 0;
        s->jobs[i].turnaround = 0;
        s->jobs[i].waiting = 0;
    }
    /* keep jobs in arrival order so every scan follows the tie rule */
    qsort(s->jobs, n, sizeof(struct job), cmp_arrival);
}

static void set_status(struct schedule *s, struct job *j, enum job_status st, int t, int verbose)
{
    if (j->status == st)
        return;
    if (verbose) {
        printf("  t=%-4d J%-3d %-7s -> %s\n", t, j->id, status_name(j->status), status_name(st));
        log_event("%s: t=%d J%d %s -> %s", s->name, t, j->id,
                  status_name(j->status), status_name(st));
    }
    j->status = st;
}

static void add_segment(struct schedule *s, int id, int start, int end)
{
    /* same job continuing right away, just extend the last slice */
    if (s->nseg > 0 && s->nseg <= MAX_SEGS && s->segs[s->nseg - 1].id == id &&
        s->segs[s->nseg - 1].end == start) {
        s->segs[s->nseg - 1].end = end;
        return;
    }
    if (s->nseg < MAX_SEGS) {
        s->segs[s->nseg].id = id;
        s->segs[s->nseg].start = start;
        s->segs[s->nseg].end = end;
    }
    s->nseg++;
}

static void go_idle(struct schedule *s, int from, int to, int verbose)
{
    if (verbose)
        printf("  t=%-4d CPU idle until %d\n", from, to);
    add_segment(s, 0, from, to);
}

/* every NEW job that has arrived by time t becomes READY */
static void admit(struct schedule *s, int t, struct ready_queue *q, int verbose)
{
    for (int i = 0; i < s->n; i++) {
        struct job *j = &s->jobs[i];

        if (j->status == JOB_NEW && j->arrival <= t) {
            set_status(s, j, JOB_READY, j->arrival, verbose);
            if (q != NULL)
                rq_push(q, i);
        }
    }
}

static int next_arrival(const struct schedule *s)
{
    for (int i = 0; i < s->n; i++) {
        if (s->jobs[i].status == JOB_NEW)
            return s->jobs[i].arrival;
    }
    return -1;
}

/* runs job j from time t to completion, returns the new time */
static int run_to_end(struct schedule *s, struct job *j, int t, int verbose)
{
    j->start = t;
    set_status(s, j, JOB_RUNNING, t, verbose);
    t += j->burst;
    add_segment(s, j->id, j->start, t);
    admit(s, t, NULL, verbose);
    j->completion = t;
    set_status(s, j, JOB_DONE, t, verbose);
    return t;
}

static void finish(struct schedule *s)
{
    s->total_tat = 0;
    s->total_wt = 0;
    for (int i = 0; i < s->n; i++) {
        struct job *j = &s->jobs[i];

        j->turnaround = j->completion - j->arrival;
        j->waiting = j->turnaround - j->burst;
        s->total_tat += j->turnaround;
        s->total_wt += j->waiting;
    }
    s->avg_tat = (double)s->total_tat / s->n;
    s->avg_wt = (double)s->total_wt / s->n;
    qsort(s->jobs, s->n, sizeof(struct job), cmp_id);
}

void run_fcfs(const struct job *jobs, int n, struct schedule *s, int verbose)
{
    int t = 0;

    init_schedule(s, "FCFS", jobs, n);
    for (int i = 0; i < s->n; i++) {
        struct job *j = &s->jobs[i];

        if (t < j->arrival) {
            go_idle(s, t, j->arrival, verbose);
            t = j->arrival;
        }
        admit(s, t, NULL, verbose);
        t = run_to_end(s, j, t, verbose);
    }
    finish(s);
}

void run_priority(const struct job *jobs, int n, struct schedule *s, int verbose)
{
    int t = 0;
    int done = 0;

    init_schedule(s, "Priority", jobs, n);
    while (done < s->n) {
        int best = -1;

        admit(s, t, NULL, verbose);
        /* strict < keeps the earlier job (arrival order) when priorities tie */
        for (int i = 0; i < s->n; i++) {
            if (s->jobs[i].status != JOB_READY)
                continue;
            if (best < 0 || s->jobs[i].priority < s->jobs[best].priority)
                best = i;
        }

        if (best < 0) {
            int na = next_arrival(s);
            go_idle(s, t, na, verbose);
            t = na;
            continue;
        }

        t = run_to_end(s, &s->jobs[best], t, verbose);
        done++;
    }
    finish(s);
}

void run_rr(const struct job *jobs, int n, int quantum, struct schedule *s, int verbose)
{
    struct ready_queue q;
    int remaining[MAX_JOBS];
    int t = 0;
    int done = 0;

    init_schedule(s, "RR", jobs, n);
    q.head = 0;
    q.count = 0;
    for (int i = 0; i < s->n; i++)
        remaining[i] = s->jobs[i].burst;

    admit(s, t, &q, verbose);
    while (done < s->n) {
        struct job *j;
        int i, slice, start;

        if (q.count == 0) {
            int na = next_arrival(s);
            go_idle(s, t, na, verbose);
            t = na;
            admit(s, t, &q, verbose);
            continue;
        }

        i = rq_pop(&q);
        j = &s->jobs[i];
        if (j->start < 0)
            j->start = t;
        set_status(s, j, JOB_RUNNING, t, verbose);

        slice = remaining[i] < quantum ? remaining[i] : quantum;
        start = t;
        t += slice;
        remaining[i] -= slice;
        add_segment(s, j->id, start, t);

        /* jobs that arrived during this slice go in the queue before j */
        admit(s, t, &q, verbose);

        if (remaining[i] == 0) {
            j->completion = t;
            set_status(s, j, JOB_DONE, t, verbose);
            done++;
        } else {
            /* if nobody else is waiting, j just keeps the CPU */
            if (q.count > 0)
                set_status(s, j, JOB_READY, t, verbose);
            rq_push(&q, i);
        }
    }
    finish(s);
}

static void print_timeline(const struct schedule *s)
{
    int shown = s->nseg < MAX_SEGS ? s->nseg : MAX_SEGS;

    printf("timeline:\n");
    for (int i = 0; i < shown; i++) {
        const struct segment *g = &s->segs[i];

        if (i % SEGS_PER_LINE == 0)
            printf("|");
        if (g->id == 0)
            printf(" idle %d-%d |", g->start, g->end);
        else
            printf(" J%d %d-%d |", g->id, g->start, g->end);
        if (i % SEGS_PER_LINE == SEGS_PER_LINE - 1 || i == shown - 1)
            printf("\n");
    }
    if (s->nseg > MAX_SEGS)
        printf("... %d more slices not shown\n", s->nseg - MAX_SEGS);
}

static void print_table(const struct schedule *s)
{
    printf("%-5s %-8s %-6s %-9s %-6s %-6s %-6s %s\n",
           "job", "arrival", "burst", "priority", "start", "CT", "TAT", "WT");
    for (int i = 0; i < s->n; i++) {
        const struct job *j = &s->jobs[i];

        printf("J%-4d %-8d %-6d %-9d %-6d %-6d %-6d %d\n", j->id, j->arrival, j->burst,
               j->priority, j->start, j->completion, j->turnaround, j->waiting);
    }
    printf("average TAT = %.2f\n", s->avg_tat);
    printf("average WT  = %.2f\n", s->avg_wt);
}

static void print_result(const struct schedule *s)
{
    printf("\n");
    print_timeline(s);
    printf("\n");
    print_table(s);
    log_event("%s: avg TAT %.2f, avg WT %.2f", s->name, s->avg_tat, s->avg_wt);
}

static int parse_quantum(const char *arg, int *q)
{
    if (parse_positive(arg, MAX_TIME, q) != 0) {
        fprintf(stderr, "error: time quantum must be a positive integer (1-%d), got '%s'\n",
                MAX_TIME, arg);
        return -1;
    }
    return 0;
}

int cmd_fcfs(int argc, char **argv)
{
    struct job jobs[MAX_JOBS];
    struct schedule s;
    int n;

    (void)argc;
    if (load_jobs(argv[0], jobs, &n) != 0)
        return 1;

    printf("FCFS scheduling (%d jobs)\n", n);
    printf("%s\n\n", TIE_RULE);
    printf("status changes:\n");
    run_fcfs(jobs, n, &s, 1);
    print_result(&s);
    return 0;
}

int cmd_rr(int argc, char **argv)
{
    struct job jobs[MAX_JOBS];
    struct schedule s;
    int n, quantum;

    (void)argc;
    if (parse_quantum(argv[1], &quantum) != 0)
        return 1;
    if (load_jobs(argv[0], jobs, &n) != 0)
        return 1;

    printf("Round Robin scheduling (%d jobs)\n", n);
    printf("time quantum = %d\n", quantum);
    printf("%s\n", TIE_RULE);
    printf("jobs that arrive during a slice join the queue before the preempted job\n\n");
    printf("status changes:\n");
    run_rr(jobs, n, quantum, &s, 1);
    print_result(&s);
    return 0;
}

int cmd_priority(int argc, char **argv)
{
    struct job jobs[MAX_JOBS];
    struct schedule s;
    int n;

    (void)argc;
    if (load_jobs(argv[0], jobs, &n) != 0)
        return 1;

    printf("Priority scheduling, non-preemptive (%d jobs)\n", n);
    printf("lower priority number runs first\n");
    printf("%s\n\n", TIE_RULE);
    printf("status changes:\n");
    run_priority(jobs, n, &s, 1);
    print_result(&s);
    return 0;
}

int cmd_compare(int argc, char **argv)
{
    struct job jobs[MAX_JOBS];
    struct schedule res[3];
    char label[32];
    int n, quantum, best = 0;

    (void)argc;
    if (parse_quantum(argv[1], &quantum) != 0)
        return 1;
    if (load_jobs(argv[0], jobs, &n) != 0)
        return 1;

    run_fcfs(jobs, n, &res[0], 0);
    run_rr(jobs, n, quantum, &res[1], 0);
    run_priority(jobs, n, &res[2], 0);

    /* best = lowest total waiting time, then lowest total turnaround */
    for (int i = 1; i < 3; i++) {
        if (res[i].total_wt < res[best].total_wt ||
            (res[i].total_wt == res[best].total_wt && res[i].total_tat < res[best].total_tat))
            best = i;
    }

    printf("comparing scheduling algorithms on %s (%d jobs, RR quantum = %d)\n",
           argv[0], n, quantum);
    printf("%s\n\n", TIE_RULE);
    printf("%-12s %-9s %s\n", "algorithm", "avg TAT", "avg WT");
    for (int i = 0; i < 3; i++) {
        if (i == 1)
            snprintf(label, sizeof(label), "RR (q=%d)", quantum);
        else
            snprintf(label, sizeof(label), "%s", res[i].name);
        printf("%-12s %-9.2f %.2f\n", label, res[i].avg_tat, res[i].avg_wt);
    }
    printf("\nbest: %s (lowest average waiting time, ties broken by average TAT)\n",
           res[best].name);

    log_event("compare: FCFS %.2f/%.2f, RR %.2f/%.2f, Priority %.2f/%.2f (avg TAT/WT), best %s",
              res[0].avg_tat, res[0].avg_wt, res[1].avg_tat, res[1].avg_wt,
              res[2].avg_tat, res[2].avg_wt, res[best].name);
    return 0;
}
