#include <signal.h>
#include <sys/wait.h>
#include "common.h"
#include "log.h"
#include "worker.h"

#define MODE_OK 0
#define MODE_FAIL 1
#define MODE_CRASH 2

#define FAIL_CODE 3
#define WORK_LOOPS 200000000UL

const char *sig_name(int sig)
{
    switch (sig) {
    case SIGABRT: return "SIGABRT";
    case SIGSEGV: return "SIGSEGV";
    case SIGKILL: return "SIGKILL";
    case SIGTERM: return "SIGTERM";
    case SIGINT:  return "SIGINT";
    case SIGXCPU: return "SIGXCPU";
    case SIGFPE:  return "SIGFPE";
    case SIGBUS:  return "SIGBUS";
    default:      return "unknown";
    }
}

static void run_worker(int mode)
{
    pid_t me = getpid();
    unsigned long sum = 0;

    printf("worker %d started\n", (int)me);
    fflush(stdout);

    if (mode == MODE_FAIL) {
        printf("worker %d: SIMULATED FAILURE, exiting with code %d\n", (int)me, FAIL_CODE);
        fflush(stdout);
        _exit(FAIL_CODE);
    }
    if (mode == MODE_CRASH) {
        printf("worker %d: SIMULATED CRASH, calling abort()\n", (int)me);
        fflush(stdout);
        abort();
    }

    for (unsigned long i = 0; i < WORK_LOOPS; i++)
        sum += i % 7;
    printf("worker %d: summed %lu loop steps, result %lu\n", (int)me, WORK_LOOPS, sum);
    fflush(stdout);
    _exit(0);
}

int cmd_worker(int argc, char **argv)
{
    int mode;
    pid_t pid, w;
    int status;
    int ok = 0;

    if (argc != 1) {
        fprintf(stderr, "usage: ./campus worker ok|fail|crash\n");
        return 1;
    }
    if (strcmp(argv[0], "ok") == 0) {
        mode = MODE_OK;
    } else if (strcmp(argv[0], "fail") == 0) {
        mode = MODE_FAIL;
    } else if (strcmp(argv[0], "crash") == 0) {
        mode = MODE_CRASH;
    } else {
        fprintf(stderr, "error: unknown worker mode '%s'\n", argv[0]);
        fprintf(stderr, "usage: ./campus worker ok|fail|crash\n");
        return 1;
    }

    fflush(stdout);
    pid = fork();
    if (pid < 0) {
        perror("worker: fork");
        log_event("worker: fork failed: %s", strerror(errno));
        return 1;
    }
    if (pid == 0)
        run_worker(mode);

    printf("parent %d: started worker %d in mode '%s', waiting\n", (int)getpid(), (int)pid, argv[0]);
    fflush(stdout);

    do {
        w = waitpid(pid, &status, 0);
    } while (w < 0 && errno == EINTR);
    if (w < 0) {
        perror("worker: waitpid");
        log_event("worker: waitpid failed: %s", strerror(errno));
        return 1;
    }

    if (WIFEXITED(status)) {
        int code = WEXITSTATUS(status);

        if (code == 0) {
            printf("worker %d finished normally (exit 0)\n", (int)pid);
            log_event("worker %d finished normally (exit 0)", (int)pid);
            ok = (mode == MODE_OK);
        } else {
            printf("worker %d FAILED with exit code %d%s\n", (int)pid, code,
                   mode == MODE_FAIL ? " (simulated)" : "");
            log_event("worker %d FAILED with exit code %d", (int)pid, code);
            ok = (mode == MODE_FAIL && code == FAIL_CODE);
        }
    } else if (WIFSIGNALED(status)) {
        int sig = WTERMSIG(status);

        printf("worker %d killed by signal %d %s%s\n", (int)pid, sig, sig_name(sig),
               mode == MODE_CRASH ? " (simulated)" : "");
        log_event("worker %d killed by signal %d %s", (int)pid, sig, sig_name(sig));
        ok = (mode == MODE_CRASH && sig == SIGABRT);
    } else {
        printf("worker %d ended with unknown status 0x%x\n", (int)pid, status);
        log_event("worker %d unknown status 0x%x", (int)pid, status);
    }

    if (ok) {
        printf("detection: result matches the '%s' mode\n", argv[0]);
        return 0;
    }
    printf("detection: unexpected result for '%s' mode\n", argv[0]);
    return 1;
}
