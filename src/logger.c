#include <pthread.h>
#include "common.h"
#include "log.h"
#include "jobs.h"
#include "logger.h"

#define BUF_SIZE 5
#define EVENT_LEN 64
#define MAX_THREADS 16
#define MAX_EVENTS 1000

static char buffer[BUF_SIZE][EVENT_LEN];
static int in_pos;
static int out_pos;
static int count;
static int done;

static pthread_mutex_t lock;
static pthread_cond_t not_full;
static pthread_cond_t not_empty;

struct producer_arg {
    int id;
    int first;  /* first event number this producer makes */
    int howmany;
};

struct consumer_arg {
    int id;
    int consumed;
};

static void *producer(void *p)
{
    struct producer_arg *a = p;

    for (int k = 0; k < a->howmany; k++) {
        char ev[EVENT_LEN];

        snprintf(ev, sizeof(ev), "producer %d -> job event %d", a->id, a->first + k);

        pthread_mutex_lock(&lock);
        while (count == BUF_SIZE)
            pthread_cond_wait(&not_full, &lock);

        snprintf(buffer[in_pos], EVENT_LEN, "%s", ev);
        in_pos = (in_pos + 1) % BUF_SIZE;
        count++;
        printf("producer %d added  \"%s\"  (buffer %d/%d)\n", a->id, ev, count, BUF_SIZE);

        pthread_cond_signal(&not_empty);
        pthread_mutex_unlock(&lock);
    }
    return NULL;
}

static void *consumer(void *p)
{
    struct consumer_arg *a = p;

    for (;;) {
        char ev[EVENT_LEN];

        pthread_mutex_lock(&lock);
        while (count == 0 && !done)
            pthread_cond_wait(&not_empty, &lock);

        if (count == 0 && done) {
            pthread_mutex_unlock(&lock);
            break;
        }

        snprintf(ev, sizeof(ev), "%s", buffer[out_pos]);
        out_pos = (out_pos + 1) % BUF_SIZE;
        count--;
        printf("consumer %d removed \"%s\"  (buffer %d/%d)\n", a->id, ev, count, BUF_SIZE);

        pthread_cond_signal(&not_full);
        pthread_mutex_unlock(&lock);

        log_event("consumer %d: %s", a->id, ev);
        a->consumed++;
    }
    return NULL;
}

static int get_arg(int argc, char **argv, int idx, int def, int max, const char *what, int *out)
{
    if (idx >= argc) {
        *out = def;
        return 0;
    }
    if (parse_positive(argv[idx], max, out) != 0) {
        fprintf(stderr, "error: %s must be an integer from 1 to %d, got '%s'\n",
                what, max, argv[idx]);
        return -1;
    }
    return 0;
}

int cmd_logger(int argc, char **argv)
{
    pthread_t ptid[MAX_THREADS], ctid[MAX_THREADS];
    struct producer_arg pargs[MAX_THREADS];
    struct consumer_arg cargs[MAX_THREADS];
    int np, nc, ne;
    int started_p = 0, started_c = 0;
    int next = 1, total = 0, rc = 0;

    if (get_arg(argc, argv, 0, 2, MAX_THREADS, "producers", &np) != 0 ||
        get_arg(argc, argv, 1, 2, MAX_THREADS, "consumers", &nc) != 0 ||
        get_arg(argc, argv, 2, 10, MAX_EVENTS, "events", &ne) != 0)
        return 1;

    in_pos = 0;
    out_pos = 0;
    count = 0;
    done = 0;
    pthread_mutex_init(&lock, NULL);
    pthread_cond_init(&not_full, NULL);
    pthread_cond_init(&not_empty, NULL);

    printf("producer-consumer logger: %d producers, %d consumers, %d events, buffer size %d\n\n",
           np, nc, ne, BUF_SIZE);
    log_event("logger: start %d producers, %d consumers, %d events", np, nc, ne);

    for (int i = 0; i < nc; i++) {
        cargs[i].id = i + 1;
        cargs[i].consumed = 0;
        if (pthread_create(&ctid[i], NULL, consumer, &cargs[i]) != 0) {
            fprintf(stderr, "error: could not create consumer thread %d\n", i + 1);
            rc = 1;
            break;
        }
        started_c++;
    }

    /* split the events as evenly as possible, first producers get the extra ones */
    for (int i = 0; i < np && rc == 0; i++) {
        pargs[i].id = i + 1;
        pargs[i].first = next;
        pargs[i].howmany = ne / np + (i < ne % np ? 1 : 0);
        next += pargs[i].howmany;
        if (pthread_create(&ptid[i], NULL, producer, &pargs[i]) != 0) {
            fprintf(stderr, "error: could not create producer thread %d\n", i + 1);
            rc = 1;
            break;
        }
        started_p++;
    }

    for (int i = 0; i < started_p; i++)
        pthread_join(ptid[i], NULL);

    pthread_mutex_lock(&lock);
    done = 1;
    pthread_cond_broadcast(&not_empty);
    pthread_mutex_unlock(&lock);

    for (int i = 0; i < started_c; i++) {
        pthread_join(ctid[i], NULL);
        total += cargs[i].consumed;
    }

    printf("\n");
    for (int i = 0; i < started_c; i++)
        printf("consumer %d handled %d events\n", cargs[i].id, cargs[i].consumed);
    printf("total consumed: %d of %d\n", total, next - 1);
    printf("all %d threads joined\n", started_p + started_c);
    log_event("logger: %d events consumed, all %d threads joined", total, started_p + started_c);

    pthread_mutex_destroy(&lock);
    pthread_cond_destroy(&not_full);
    pthread_cond_destroy(&not_empty);
    return rc;
}
