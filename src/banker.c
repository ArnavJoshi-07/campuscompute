#include "common.h"
#include "log.h"
#include "jobs.h"
#include "banker.h"

/* keeps Work from overflowing when allocations are added up */
#define MAX_UNITS 1000000

struct bank {
    int n;
    int m;
    int avail[MAX_RES];
    int alloc[MAX_PROC][MAX_RES];
    int max[MAX_PROC][MAX_RES];
    int need[MAX_PROC][MAX_RES];
};

/* reads one line with exactly want non-negative integers */
static int read_row(FILE *fp, const char *path, int *lineno, int *vals, int want,
                    const char *what)
{
    char line[256];
    int rc = read_data_line(fp, line, sizeof(line), lineno);

    if (rc == 0) {
        fprintf(stderr, "error: %s: file ended early, expected %s\n", path, what);
        return -1;
    }
    if (rc < 0) {
        fprintf(stderr, "error: %s line %d: line too long\n", path, *lineno);
        return -1;
    }
    if (parse_int_line(line, vals, want) != want) {
        fprintf(stderr, "error: %s line %d: expected %d integers for %s\n",
                path, *lineno, want, what);
        return -1;
    }
    for (int i = 0; i < want; i++) {
        if (vals[i] < 0) {
            fprintf(stderr, "error: %s line %d: negative value in %s\n", path, *lineno, what);
            return -1;
        }
        if (vals[i] > MAX_UNITS) {
            fprintf(stderr, "error: %s line %d: value too large in %s (max %d)\n",
                    path, *lineno, what, MAX_UNITS);
            return -1;
        }
    }
    return 0;
}

static int load_bank(const char *path, struct bank *b)
{
    FILE *fp;
    char line[256], what[64];
    int lineno = 0;
    int nm[2];

    fp = fopen(path, "r");
    if (fp == NULL) {
        fprintf(stderr, "error: cannot open banker file '%s': %s\n", path, strerror(errno));
        return -1;
    }

    if (read_row(fp, path, &lineno, nm, 2, "n m (processes resources)") != 0)
        goto fail;
    b->n = nm[0];
    b->m = nm[1];
    if (b->n < 1 || b->n > MAX_PROC) {
        fprintf(stderr, "error: %s line %d: n must be 1 to %d\n", path, lineno, MAX_PROC);
        goto fail;
    }
    if (b->m < 1 || b->m > MAX_RES) {
        fprintf(stderr, "error: %s line %d: m must be 1 to %d\n", path, lineno, MAX_RES);
        goto fail;
    }

    if (read_row(fp, path, &lineno, b->avail, b->m, "the Available row") != 0)
        goto fail;
    for (int i = 0; i < b->n; i++) {
        snprintf(what, sizeof(what), "Allocation of P%d", i);
        if (read_row(fp, path, &lineno, b->alloc[i], b->m, what) != 0)
            goto fail;
    }
    for (int i = 0; i < b->n; i++) {
        snprintf(what, sizeof(what), "Max of P%d", i);
        if (read_row(fp, path, &lineno, b->max[i], b->m, what) != 0)
            goto fail;
        for (int j = 0; j < b->m; j++) {
            if (b->alloc[i][j] > b->max[i][j]) {
                fprintf(stderr, "error: %s line %d: P%d has Allocation %d > Max %d for resource %c\n",
                        path, lineno, i, b->alloc[i][j], b->max[i][j], 'A' + j);
                goto fail;
            }
            b->need[i][j] = b->max[i][j] - b->alloc[i][j];
        }
    }

    if (read_data_line(fp, line, sizeof(line), &lineno) != 0) {
        fprintf(stderr, "error: %s line %d: unexpected extra data\n", path, lineno);
        goto fail;
    }

    fclose(fp);
    return 0;

fail:
    fclose(fp);
    return -1;
}

static void print_vec(const int *v, int m)
{
    for (int j = 0; j < m; j++)
        printf("%3d", v[j]);
}

/* one column block of the table, padded so the titles line up */
static void print_block(const int *v, int m, int width)
{
    print_vec(v, m);
    printf("%*s", width - 3 * m + 3, "");
}

static void print_tables(const struct bank *b)
{
    int w = 3 * b->m < 10 ? 10 : 3 * b->m;

    printf("%-8s %-*s   %-*s   %s\n", "", w, "Allocation", w, "Max", "Need");
    printf("%-8s ", "process");
    for (int k = 0; k < 3; k++) {
        for (int j = 0; j < b->m; j++)
            printf("%3c", 'A' + j);
        if (k < 2)
            printf("%*s", w - 3 * b->m + 3, "");
    }
    printf("\n");

    for (int i = 0; i < b->n; i++) {
        printf("P%-7d ", i);
        print_block(b->alloc[i], b->m, w);
        print_block(b->max[i], b->m, w);
        print_vec(b->need[i], b->m);
        printf("\n");
    }

    printf("\n%-8s ", "Available");
    print_vec(b->avail, b->m);
    printf("\n");
}

static int can_run(const struct bank *b, int i, const int *work)
{
    for (int j = 0; j < b->m; j++) {
        if (b->need[i][j] > work[j])
            return 0;
    }
    return 1;
}

/* fills seq with the safe order, returns how many processes could finish */
static int safety(const struct bank *b, int *seq, int *finish)
{
    int work[MAX_RES];
    int found = 0, step = 0;

    for (int j = 0; j < b->m; j++)
        work[j] = b->avail[j];
    for (int i = 0; i < b->n; i++)
        finish[i] = 0;

    printf("\nsafety check, start with Work =");
    print_vec(work, b->m);
    printf("\n");

    /* keep passing over the processes until a full pass finds nobody new */
    do {
        found = 0;
        for (int i = 0; i < b->n; i++) {
            if (finish[i] || !can_run(b, i, work))
                continue;

            step++;
            for (int j = 0; j < b->m; j++)
                work[j] += b->alloc[i][j];
            finish[i] = 1;
            seq[step - 1] = i;
            found = 1;

            printf("step %d: P%d Need", step, i);
            print_vec(b->need[i], b->m);
            printf(" <= Work, runs and releases, Work =");
            print_vec(work, b->m);
            printf("\n");
        }
    } while (found && step < b->n);

    if (step < b->n) {
        printf("no remaining process has Need <= Work =");
        print_vec(work, b->m);
        printf("\n");
    }
    return step;
}

int cmd_banker(int argc, char **argv)
{
    struct bank b;
    int seq[MAX_PROC], finish[MAX_PROC];
    char text[128];
    int done;

    (void)argc;
    if (load_bank(argv[0], &b) != 0)
        return 1;

    printf("Banker's safety algorithm: %d processes, %d resource types\n\n", b.n, b.m);
    print_tables(&b);

    done = safety(&b, seq, finish);

    text[0] = '\0';
    printf("\n");
    if (done == b.n) {
        for (int k = 0; k < b.n; k++) {
            size_t len = strlen(text);
            snprintf(text + len, sizeof(text) - len, "%sP%d", k ? " -> " : "", seq[k]);
        }
        printf("SAFE, sequence: %s\n", text);
        log_event("banker: %s is SAFE, sequence %s", argv[0], text);
    } else {
        for (int i = 0; i < b.n; i++) {
            size_t len = strlen(text);
            if (!finish[i])
                snprintf(text + len, sizeof(text) - len, "%sP%d", len ? ", " : "", i);
        }
        printf("UNSAFE: no process can proceed, remaining: %s\n", text);
        log_event("banker: %s is UNSAFE, remaining %s", argv[0], text);
    }
    return 0;
}
