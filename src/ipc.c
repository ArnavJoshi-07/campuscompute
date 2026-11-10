#include <fcntl.h>
#include <semaphore.h>
#include <signal.h>
#include <time.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include "common.h"
#include "log.h"
#include "ipc.h"

#define IPC_DEFAULT_MSGS 5
#define IPC_MAX_MSGS 50
#define SEM_TIMEOUT 5

void ipc_names(char *shm_name, char *full_name, char *empty_name, size_t len)
{
    unsigned int uid = (unsigned int)getuid();

    snprintf(shm_name, len, "/campus_shm_%u", uid);
    snprintf(full_name, len, "/campus_full_%u", uid);
    snprintf(empty_name, len, "/campus_empty_%u", uid);
}

static int parse_count(const char *s, int *out)
{
    char *end;
    long v;

    errno = 0;
    v = strtol(s, &end, 10);
    if (errno != 0 || end == s || *end != '\0')
        return -1;
    if (v < 1 || v > IPC_MAX_MSGS)
        return -1;
    *out = (int)v;
    return 0;
}

/* like sem_wait but gives up after some seconds so we never hang
   forever if the other process dies */
static int wait_sem(sem_t *s, int seconds)
{
    struct timespec ts;

    if (clock_gettime(CLOCK_REALTIME, &ts) != 0)
        return -1;
    ts.tv_sec += seconds;

    while (sem_timedwait(s, &ts) != 0) {
        if (errno != EINTR)
            return -1;
    }
    return 0;
}

static void print_wait_error(const char *who, const char *sem)
{
    if (errno == ETIMEDOUT)
        fprintf(stderr, "ipc: %s timed out after %ds waiting on '%s'\n",
                who, SEM_TIMEOUT, sem);
    else
        fprintf(stderr, "ipc: %s sem_timedwait '%s': %s\n",
                who, sem, strerror(errno));
}

/* leftovers from an earlier run that was killed would make O_EXCL fail */
static void remove_stale(const char *shm_name, const char *full_name,
                         const char *empty_name)
{
    if (shm_unlink(shm_name) == 0)
        printf("ipc: removed old shared memory %s\n", shm_name);
    else if (errno != ENOENT)
        fprintf(stderr, "ipc: shm_unlink %s: %s\n", shm_name, strerror(errno));

    if (sem_unlink(full_name) == 0)
        printf("ipc: removed old semaphore %s\n", full_name);
    else if (errno != ENOENT)
        fprintf(stderr, "ipc: sem_unlink %s: %s\n", full_name, strerror(errno));

    if (sem_unlink(empty_name) == 0)
        printf("ipc: removed old semaphore %s\n", empty_name);
    else if (errno != ENOENT)
        fprintf(stderr, "ipc: sem_unlink %s: %s\n", empty_name, strerror(errno));
}

/* runs in the child, never returns */
static void run_producer(struct ipc_slot *slot, sem_t *full, sem_t *empty,
                         int count)
{
    pid_t me = getpid();

    printf("producer pid=%d started\n", (int)me);
    fflush(stdout);

    for (int i = 1; i <= count; i++) {
        if (wait_sem(empty, SEM_TIMEOUT) != 0) {
            print_wait_error("producer", "empty");
            _exit(1);
        }

        slot->seq = i;
        slot->sender = me;
        snprintf(slot->text, sizeof(slot->text), "hello #%d from the producer", i);

        printf("producer pid=%d wrote message %d\n", (int)me, i);
        fflush(stdout);

        if (sem_post(full) != 0) {
            perror("ipc: producer sem_post full");
            _exit(1);
        }
    }
    _exit(0);
}

int cmd_ipc(int argc, char **argv)
{
    char shm_name[IPC_NAME_LEN], full_name[IPC_NAME_LEN], empty_name[IPC_NAME_LEN];
    int count = IPC_DEFAULT_MSGS;
    int fd = -1;
    int shm_made = 0;
    struct ipc_slot *slot = MAP_FAILED;
    sem_t *full = SEM_FAILED;
    sem_t *empty = SEM_FAILED;
    pid_t child = -1;
    pid_t me = getpid();
    int received = 0;
    int rc = 0;

    if (argc == 1 && parse_count(argv[0], &count) != 0) {
        fprintf(stderr, "error: messages must be a number from 1 to %d\n", IPC_MAX_MSGS);
        return 1;
    }

    ipc_names(shm_name, full_name, empty_name, sizeof(shm_name));
    remove_stale(shm_name, full_name, empty_name);

    printf("ipc: %d messages, shm=%s, sems=%s %s\n", count, shm_name, full_name, empty_name);

    fd = shm_open(shm_name, O_CREAT | O_EXCL | O_RDWR, 0600);
    if (fd < 0) {
        fprintf(stderr, "ipc: shm_open %s: %s\n", shm_name, strerror(errno));
        rc = 1;
        goto cleanup;
    }
    shm_made = 1;
    printf("ipc: created shared memory %s (%zu bytes)\n", shm_name, sizeof(struct ipc_slot));

    if (ftruncate(fd, sizeof(struct ipc_slot)) != 0) {
        perror("ipc: ftruncate");
        rc = 1;
        goto cleanup;
    }

    slot = mmap(NULL, sizeof(struct ipc_slot), PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (slot == MAP_FAILED) {
        perror("ipc: mmap");
        rc = 1;
        goto cleanup;
    }
    memset(slot, 0, sizeof(*slot));

    /* empty = 1: slot is free, full = 0: nothing to read yet */
    empty = sem_open(empty_name, O_CREAT | O_EXCL, 0600, 1);
    if (empty == SEM_FAILED) {
        fprintf(stderr, "ipc: sem_open %s: %s\n", empty_name, strerror(errno));
        rc = 1;
        goto cleanup;
    }
    full = sem_open(full_name, O_CREAT | O_EXCL, 0600, 0);
    if (full == SEM_FAILED) {
        fprintf(stderr, "ipc: sem_open %s: %s\n", full_name, strerror(errno));
        rc = 1;
        goto cleanup;
    }
    printf("ipc: created semaphores %s (init 1) and %s (init 0)\n", empty_name, full_name);

    fflush(stdout);
    child = fork();
    if (child < 0) {
        perror("ipc: fork");
        rc = 1;
        goto cleanup;
    }
    if (child == 0)
        run_producer(slot, full, empty, count);

    printf("consumer pid=%d waiting for messages from producer pid=%d\n", (int)me, (int)child);
    fflush(stdout);

    for (int i = 1; i <= count; i++) {
        struct ipc_slot msg;

        if (wait_sem(full, SEM_TIMEOUT) != 0) {
            print_wait_error("consumer", "full");
            kill(child, SIGKILL);
            rc = 1;
            break;
        }

        msg = *slot;
        printf("consumer pid=%d read message %d from pid=%d: \"%s\"\n",
               (int)me, msg.seq, (int)msg.sender, msg.text);
        fflush(stdout);

        if (msg.seq != i || msg.sender != child) {
            fprintf(stderr, "ipc: unexpected message (seq %d, expected %d)\n", msg.seq, i);
            rc = 1;
        }
        received++;

        /* only now let the producer overwrite the slot */
        if (sem_post(empty) != 0) {
            perror("ipc: consumer sem_post empty");
            kill(child, SIGKILL);
            rc = 1;
            break;
        }
    }

cleanup:
    if (child > 0) {
        int status;
        pid_t w;

        do {
            w = waitpid(child, &status, 0);
        } while (w < 0 && errno == EINTR);

        if (w < 0) {
            perror("ipc: waitpid");
            rc = 1;
        } else if (WIFEXITED(status)) {
            printf("cleanup: producer pid=%d exited with code %d\n", (int)child, WEXITSTATUS(status));
            if (WEXITSTATUS(status) != 0)
                rc = 1;
        } else if (WIFSIGNALED(status)) {
            printf("cleanup: producer pid=%d killed by signal %d\n", (int)child, WTERMSIG(status));
            rc = 1;
        }
    }

    if (slot != MAP_FAILED) {
        if (munmap(slot, sizeof(struct ipc_slot)) != 0) {
            perror("cleanup: munmap");
            rc = 1;
        } else {
            printf("cleanup: munmap done\n");
        }
    }
    if (fd >= 0) {
        if (close(fd) != 0) {
            perror("cleanup: close shm fd");
            rc = 1;
        } else {
            printf("cleanup: closed shm fd\n");
        }
    }
    if (shm_made) {
        if (shm_unlink(shm_name) != 0) {
            fprintf(stderr, "cleanup: shm_unlink %s: %s\n", shm_name, strerror(errno));
            rc = 1;
        } else {
            printf("cleanup: shm_unlink %s done\n", shm_name);
        }
    }
    if (empty != SEM_FAILED) {
        if (sem_close(empty) != 0) {
            perror("cleanup: sem_close empty");
            rc = 1;
        } else {
            printf("cleanup: sem_close %s done\n", empty_name);
        }
        if (sem_unlink(empty_name) != 0) {
            fprintf(stderr, "cleanup: sem_unlink %s: %s\n", empty_name, strerror(errno));
            rc = 1;
        } else {
            printf("cleanup: sem_unlink %s done\n", empty_name);
        }
    }
    if (full != SEM_FAILED) {
        if (sem_close(full) != 0) {
            perror("cleanup: sem_close full");
            rc = 1;
        } else {
            printf("cleanup: sem_close %s done\n", full_name);
        }
        if (sem_unlink(full_name) != 0) {
            fprintf(stderr, "cleanup: sem_unlink %s: %s\n", full_name, strerror(errno));
            rc = 1;
        } else {
            printf("cleanup: sem_unlink %s done\n", full_name);
        }
    }

    if (rc == 0) {
        printf("ipc: %d/%d messages received, all resources removed\n", received, count);
        log_event("ipc: %d messages from producer %d to consumer %d via %s",
                  received, (int)child, (int)me, shm_name);
    } else {
        printf("ipc: finished with errors (%d/%d messages received)\n", received, count);
        log_event("ipc: error, %d/%d messages received", received, count);
    }
    return rc;
}
