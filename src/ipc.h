#ifndef IPC_H
#define IPC_H

#include <stddef.h>
#include <sys/types.h>

#define IPC_NAME_LEN 64
#define IPC_TEXT_LEN 64

/* one message slot in shared memory */
struct ipc_slot {
    int seq;
    pid_t sender;
    char text[IPC_TEXT_LEN];
};

void ipc_names(char *shm_name, char *full_name, char *empty_name, size_t len);
int cmd_ipc(int argc, char **argv);

#endif
