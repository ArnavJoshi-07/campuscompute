#include <dirent.h>
#include <semaphore.h>
#include <signal.h>
#include <time.h>
#include <sys/mman.h>
#include <sys/resource.h>
#include <sys/time.h>
#include <sys/wait.h>
#include "common.h"
#include "log.h"
#include "ipc.h"
#include "worker.h"
#include "limits.h"

#define CPU_SOFT 1
#define CPU_HARD 2
#define WATCHDOG_SEC 15

static double elapsed(struct timespec *a, struct timespec *b)
{
    return (b->tv_sec - a->tv_sec) + (b->tv_nsec - a->tv_nsec) / 1e9;
}

static double tv_sec(struct timeval *tv)
{
    return tv->tv_sec + tv->tv_usec / 1e6;
}

/* runs in the child, never returns normally */
static void run_limited(void)
{
    struct rlimit rl;
    volatile unsigned long n = 0;
    pid_t me = getpid();

    rl.rlim_cur = CPU_SOFT;
    rl.rlim_max = CPU_HARD;
    if (setrlimit(RLIMIT_CPU, &rl) != 0) {
        perror("limit: setrlimit RLIMIT_CPU");
        _exit(1);
    }
    if (getrlimit(RLIMIT_CPU, &rl) != 0) {
        perror("limit: getrlimit RLIMIT_CPU");
        _exit(1);
    }
    printf("child %d: RLIMIT_CPU soft=%llus hard=%llus, now busy looping\n",
           (int)me, (unsigned long long)rl.rlim_cur, (unsigned long long)rl.rlim_max);
    fflush(stdout);

    for (;;)
        n++;
}

int cmd_limit(int argc, char **argv)
{
    struct timespec start, now;
    struct rusage ru;
    pid_t pid, w;
    int status;
    int rc = 1;

    (void)argc;
    (void)argv;

    if (clock_gettime(CLOCK_MONOTONIC, &start) != 0) {
        perror("limit: clock_gettime");
        return 1;
    }

    fflush(stdout);
    pid = fork();
    if (pid < 0) {
        perror("limit: fork");
        return 1;
    }
    if (pid == 0)
        run_limited();

    printf("parent %d: waiting for child %d\n", (int)getpid(), (int)pid);
    fflush(stdout);

    /* poll so we can kill the child ourselves if the limit never fires */
    for (;;) {
        struct timespec pause = {0, 50 * 1000 * 1000};

        w = wait4(pid, &status, WNOHANG, &ru);
        if (w == pid)
            break;
        if (w < 0 && errno != EINTR) {
            perror("limit: wait4");
            kill(pid, SIGKILL);
            waitpid(pid, NULL, 0);
            return 1;
        }
        if (clock_gettime(CLOCK_MONOTONIC, &now) == 0 && elapsed(&start, &now) > WATCHDOG_SEC) {
            fprintf(stderr, "limit: child still running after %ds, killing it\n", WATCHDOG_SEC);
            kill(pid, SIGKILL);
        }
        nanosleep(&pause, NULL);
    }

    if (clock_gettime(CLOCK_MONOTONIC, &now) != 0) {
        perror("limit: clock_gettime");
        now = start;
    }

    printf("child %d ran for %.2fs wall time, %.2fs cpu (user %.2fs, sys %.2fs)\n",
           (int)pid, elapsed(&start, &now),
           tv_sec(&ru.ru_utime) + tv_sec(&ru.ru_stime),
           tv_sec(&ru.ru_utime), tv_sec(&ru.ru_stime));

    if (WIFSIGNALED(status)) {
        int sig = WTERMSIG(status);

        printf("child %d terminated by signal %d %s\n", (int)pid, sig, sig_name(sig));
        if (sig == SIGXCPU) {
            printf("effect: the kernel sent SIGXCPU once the child used its %ds soft cpu limit, stopping the runaway loop\n",
                   CPU_SOFT);
            rc = 0;
        } else if (sig == SIGKILL) {
            printf("effect: the child reached the %ds hard cpu limit (or the watchdog) and was killed with SIGKILL\n",
                   CPU_HARD);
            rc = 0;
        }
        log_event("limit: child %d killed by %s after %.2fs cpu", (int)pid, sig_name(sig),
                  tv_sec(&ru.ru_utime) + tv_sec(&ru.ru_stime));
    } else if (WIFEXITED(status)) {
        printf("child %d exited with code %d, the limit did not stop it\n", (int)pid, WEXITSTATUS(status));
        log_event("limit: child %d exited with code %d", (int)pid, WEXITSTATUS(status));
    }
    return rc;
}

static int unlink_shm(const char *name)
{
    if (shm_unlink(name) == 0) {
        printf("removed shared memory %s\n", name);
        return 0;
    }
    if (errno == ENOENT) {
        printf("shared memory %s not present\n", name);
        return 0;
    }
    fprintf(stderr, "cleanup: shm_unlink %s: %s\n", name, strerror(errno));
    return -1;
}

static int unlink_sem(const char *name)
{
    if (sem_unlink(name) == 0) {
        printf("removed semaphore %s\n", name);
        return 0;
    }
    if (errno == ENOENT) {
        printf("semaphore %s not present\n", name);
        return 0;
    }
    fprintf(stderr, "cleanup: sem_unlink %s: %s\n", name, strerror(errno));
    return -1;
}

int cmd_cleanup(int argc, char **argv)
{
    char shm_name[IPC_NAME_LEN], full_name[IPC_NAME_LEN], empty_name[IPC_NAME_LEN];
    DIR *dir;
    struct dirent *ent;
    int left = 0;
    int problems = 0;
    pid_t w;

    (void)argc;
    (void)argv;

    ipc_names(shm_name, full_name, empty_name, sizeof(shm_name));
    if (unlink_shm(shm_name) != 0)
        problems++;
    if (unlink_sem(full_name) != 0)
        problems++;
    if (unlink_sem(empty_name) != 0)
        problems++;

    printf("checking /dev/shm for campus objects\n");
    dir = opendir("/dev/shm");
    if (dir == NULL) {
        perror("cleanup: opendir /dev/shm");
        problems++;
    } else {
        errno = 0;
        while ((ent = readdir(dir)) != NULL) {
            if (strncmp(ent->d_name, "campus", 6) == 0 ||
                strncmp(ent->d_name, "sem.campus", 10) == 0) {
                printf("  still there: /dev/shm/%s\n", ent->d_name);
                left++;
            }
        }
        if (errno != 0) {
            perror("cleanup: readdir /dev/shm");
            problems++;
        }
        closedir(dir);
        if (left == 0)
            printf("  none found\n");
    }

    printf("checking for child processes\n");
    w = waitpid(-1, NULL, WNOHANG);
    if (w < 0 && errno == ECHILD) {
        printf("  no child processes left\n");
    } else if (w < 0) {
        perror("cleanup: waitpid");
        problems++;
    } else if (w == 0) {
        printf("  a child process is still running\n");
        problems++;
    } else {
        printf("  reaped leftover child %d\n", (int)w);
        problems++;
    }

    if (left == 0 && problems == 0) {
        printf("cleanup OK\n");
        log_event("cleanup: OK");
        return 0;
    }
    printf("cleanup found problems: %d object(s) left, %d other issue(s)\n", left, problems);
    log_event("cleanup: %d objects left, %d issues", left, problems);
    return 1;
}
