#include "common.h"
#include "log.h"
#include "jobs.h"
#include "scheduler.h"
#include "logger.h"
#include "banker.h"
#include "ipc.h"
#include "worker.h"
#include "limits.h"

static int cmd_help(int argc, char **argv);
static int cmd_demo(int argc, char **argv);

struct command {
    const char *name;
    int min_args;
    int max_args;
    int (*fn)(int argc, char **argv);
};

static struct command commands[] = {
    {"help",     0, 0, cmd_help},
    {"jobs",     1, 1, cmd_jobs},
    {"fcfs",     1, 1, cmd_fcfs},
    {"rr",       2, 2, cmd_rr},
    {"priority", 1, 1, cmd_priority},
    {"compare",  2, 2, cmd_compare},
    {"logger",   0, 3, cmd_logger},
    {"banker",   1, 1, cmd_banker},
    {"ipc",      0, 1, cmd_ipc},
    {"worker",   1, 1, cmd_worker},
    {"limit",    0, 0, cmd_limit},
    {"cleanup",  0, 0, cmd_cleanup},
    {"demo",     0, 0, cmd_demo},
};

static int num_commands = sizeof(commands) / sizeof(commands[0]);

static void print_usage(FILE *out)
{
    fprintf(out, "usage: ./campus <command> [args]\n");
    fprintf(out, "commands:\n");
    fprintf(out, "  help\n");
    fprintf(out, "  jobs <file>\n");
    fprintf(out, "  fcfs <file>\n");
    fprintf(out, "  rr <file> <quantum>\n");
    fprintf(out, "  priority <file>\n");
    fprintf(out, "  compare <file> <quantum>\n");
    fprintf(out, "  logger [producers] [consumers] [events]\n");
    fprintf(out, "  banker <file>\n");
    fprintf(out, "  ipc [messages]\n");
    fprintf(out, "  worker ok|fail|crash\n");
    fprintf(out, "  limit\n");
    fprintf(out, "  cleanup\n");
    fprintf(out, "  demo\n");
}

static int cmd_help(int argc, char **argv)
{
    (void)argc;
    (void)argv;
    print_usage(stdout);
    return 0;
}

static struct command *find_command(const char *name)
{
    for (int i = 0; i < num_commands; i++) {
        if (strcmp(commands[i].name, name) == 0)
            return &commands[i];
    }
    return NULL;
}

static int run_command(const char *name, int argc, char **argv)
{
    struct command *cmd = find_command(name);
    char line[256];
    int rc;

    if (cmd == NULL) {
        fprintf(stderr, "error: unknown command '%s'\n", name);
        print_usage(stderr);
        return 1;
    }
    if (argc < cmd->min_args || argc > cmd->max_args) {
        fprintf(stderr, "error: wrong number of arguments for '%s'\n", name);
        print_usage(stderr);
        return 1;
    }
    if (strcmp(name, "worker") == 0 && strcmp(argv[0], "ok") != 0 &&
        strcmp(argv[0], "fail") != 0 && strcmp(argv[0], "crash") != 0) {
        fprintf(stderr, "error: worker mode must be ok, fail or crash\n");
        print_usage(stderr);
        return 1;
    }

    snprintf(line, sizeof(line), "%s", name);
    for (int i = 0; i < argc; i++) {
        size_t len = strlen(line);
        snprintf(line + len, sizeof(line) - len, " %s", argv[i]);
    }

    log_event("run: %s", line);
    rc = cmd->fn(argc, argv);
    log_event("done: %s (rc=%d)", name, rc);
    return rc;
}

struct demo_step {
    const char *name;
    int argc;
    char *argv[3];
};

static int cmd_demo(int argc, char **argv)
{
    struct demo_step steps[] = {
        {"fcfs",     1, {"tests/jobs1.txt"}},
        {"rr",       2, {"tests/jobs1.txt", "2"}},
        {"priority", 1, {"tests/jobs1.txt"}},
        {"compare",  2, {"tests/jobs1.txt", "2"}},
        {"logger",   3, {"2", "2", "10"}},
        {"banker",   1, {"tests/banker_safe.txt"}},
        {"banker",   1, {"tests/banker_unsafe.txt"}},
        {"ipc",      1, {"5"}},
        {"worker",   1, {"ok"}},
        {"worker",   1, {"fail"}},
        {"limit",    0, {NULL}},
        {"cleanup",  0, {NULL}},
    };
    int n = sizeof(steps) / sizeof(steps[0]);
    int results[sizeof(steps) / sizeof(steps[0])];
    int failed = 0;

    (void)argc;
    (void)argv;

    for (int i = 0; i < n; i++) {
        printf("\n== step %d: %s", i + 1, steps[i].name);
        for (int j = 0; j < steps[i].argc; j++)
            printf(" %s", steps[i].argv[j]);
        printf("\n");
        fflush(stdout);

        results[i] = run_command(steps[i].name, steps[i].argc, steps[i].argv);
        if (results[i] != 0)
            failed++;
    }

    printf("\ndemo summary\n");
    printf("%-4s %-36s %s\n", "#", "step", "result");
    for (int i = 0; i < n; i++) {
        char label[128];
        snprintf(label, sizeof(label), "%s", steps[i].name);
        for (int j = 0; j < steps[i].argc; j++) {
            size_t len = strlen(label);
            snprintf(label + len, sizeof(label) - len, " %s", steps[i].argv[j]);
        }
        printf("%-4d %-36s %s\n", i + 1, label, results[i] == 0 ? "PASS" : "FAIL");
    }
    printf("%d/%d steps passed\n", n - failed, n);

    return failed == 0 ? 0 : 1;
}

int main(int argc, char **argv)
{
    if (argc < 2) {
        fprintf(stderr, "error: no command given\n");
        print_usage(stderr);
        return 1;
    }
    return run_command(argv[1], argc - 2, argv + 2) == 0 ? 0 : 1;
}
