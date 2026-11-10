#include <ctype.h>
#include <limits.h>
#include "common.h"
#include "log.h"
#include "jobs.h"

const char *status_name(enum job_status s)
{
    switch (s) {
    case JOB_NEW:     return "NEW";
    case JOB_READY:   return "READY";
    case JOB_RUNNING: return "RUNNING";
    case JOB_DONE:    return "DONE";
    }
    return "?";
}

/*
 * Reads the next line that has data on it. Anything after '#' is a comment.
 * Returns 1 for a line, 0 at end of file, -1 if the line is too long.
 */
int read_data_line(FILE *fp, char *buf, int size, int *lineno)
{
    while (fgets(buf, size, fp) != NULL) {
        char *p;
        size_t len = strlen(buf);

        (*lineno)++;
        if (len == (size_t)size - 1 && buf[len - 1] != '\n' && !feof(fp))
            return -1;

        p = strchr(buf, '#');
        if (p != NULL)
            *p = '\0';

        for (p = buf; *p != '\0'; p++) {
            if (!isspace((unsigned char)*p))
                return 1;
        }
    }
    return 0;
}

/*
 * Parses whitespace separated integers. Returns how many were on the line
 * (only the first max are stored), or -1 if something is not an integer.
 */
int parse_int_line(const char *line, int *vals, int max)
{
    const char *p = line;
    char *end;
    int count = 0;

    for (;;) {
        long v;

        while (isspace((unsigned char)*p))
            p++;
        if (*p == '\0')
            break;

        errno = 0;
        v = strtol(p, &end, 10);
        if (end == p || errno == ERANGE || v > INT_MAX || v < INT_MIN)
            return -1;
        if (*end != '\0' && !isspace((unsigned char)*end))
            return -1;

        if (count < max)
            vals[count] = (int)v;
        count++;
        p = end;
    }
    return count;
}

int parse_positive(const char *s, int max, int *out)
{
    int v;

    if (parse_int_line(s, &v, 1) != 1 || v <= 0 || v > max)
        return -1;
    *out = v;
    return 0;
}

int load_jobs(const char *path, struct job *jobs, int *count)
{
    FILE *fp;
    char line[256];
    int lineno = 0;
    int n = 0;
    int rc;

    fp = fopen(path, "r");
    if (fp == NULL) {
        fprintf(stderr, "error: cannot open job file '%s': %s\n", path, strerror(errno));
        return -1;
    }

    while ((rc = read_data_line(fp, line, sizeof(line), &lineno)) == 1) {
        int v[4];
        struct job *j;

        if (parse_int_line(line, v, 4) != 4) {
            fprintf(stderr, "error: %s line %d: expected 4 integers (id arrival burst priority)\n",
                    path, lineno);
            goto fail;
        }
        if (v[0] <= 0) {
            fprintf(stderr, "error: %s line %d: job id must be positive\n", path, lineno);
            goto fail;
        }
        if (v[1] < 0 || v[1] > MAX_TIME) {
            fprintf(stderr, "error: %s line %d: arrival must be between 0 and %d\n",
                    path, lineno, MAX_TIME);
            goto fail;
        }
        if (v[2] <= 0 || v[2] > MAX_TIME) {
            fprintf(stderr, "error: %s line %d: burst must be between 1 and %d\n",
                    path, lineno, MAX_TIME);
            goto fail;
        }
        if (v[3] < 0) {
            fprintf(stderr, "error: %s line %d: priority must not be negative\n", path, lineno);
            goto fail;
        }
        for (int i = 0; i < n; i++) {
            if (jobs[i].id == v[0]) {
                fprintf(stderr, "error: %s line %d: duplicate job id %d\n", path, lineno, v[0]);
                goto fail;
            }
        }
        if (n == MAX_JOBS) {
            fprintf(stderr, "error: %s line %d: too many jobs (max %d)\n", path, lineno, MAX_JOBS);
            goto fail;
        }

        j = &jobs[n++];
        memset(j, 0, sizeof(*j));
        j->id = v[0];
        j->arrival = v[1];
        j->burst = v[2];
        j->priority = v[3];
        j->status = JOB_NEW;
        j->start = -1;
    }

    if (rc < 0) {
        fprintf(stderr, "error: %s line %d: line too long\n", path, lineno);
        goto fail;
    }
    if (n == 0) {
        fprintf(stderr, "error: %s: no jobs found\n", path);
        goto fail;
    }

    fclose(fp);
    *count = n;
    return 0;

fail:
    fclose(fp);
    return -1;
}

void print_jobs(const struct job *jobs, int count)
{
    printf("%-6s %-8s %-6s %-9s %s\n", "job", "arrival", "burst", "priority", "status");
    for (int i = 0; i < count; i++) {
        printf("J%-5d %-8d %-6d %-9d %s\n", jobs[i].id, jobs[i].arrival,
               jobs[i].burst, jobs[i].priority, status_name(jobs[i].status));
    }
}

int cmd_jobs(int argc, char **argv)
{
    struct job jobs[MAX_JOBS];
    int n;

    (void)argc;

    if (load_jobs(argv[0], jobs, &n) != 0)
        return 1;

    printf("loaded %d jobs from %s\n", n, argv[0]);
    print_jobs(jobs, n);
    log_event("jobs: loaded %d jobs from %s", n, argv[0]);
    return 0;
}
