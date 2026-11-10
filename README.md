# CampusCompute

## Prerequisites

## Build

## Commands

## Cleanup

## Part A notes

## Part B notes

Part B covers processes, shared memory IPC, failure detection and resource
limits. Code is in `src/ipc.c`, `src/worker.c` and `src/limits.c`.

### Commands

```
./campus ipc [messages]       messages is 1..50, default 5
./campus worker ok|fail|crash
./campus limit
./campus cleanup
```

### ipc (FR4)

The parent creates a POSIX shared memory object (`shm_open`, `ftruncate`,
`mmap`) that holds one message slot: sequence number, sender pid and a short
text. Two named semaphores control access to the slot:

- `empty`, starts at 1, means the slot is free to write
- `full`, starts at 0, means a message is waiting to be read

After `fork()` the child is the producer and the parent is the consumer.
The producer waits on `empty`, writes a message and posts `full`. The
consumer waits on `full`, prints the message with both pids and posts
`empty`. Waits time out after 5 seconds so the program does not hang if the
other side dies.

At the end the parent reaps the child with `waitpid` and then runs
`munmap`, `close`, `shm_unlink`, `sem_close` and `sem_unlink`, printing each
step. If something fails half way, only the objects that were actually
created get removed.

IPC names include the user id so they never clash with other users:

```
/campus_shm_<uid>      shared memory   (/dev/shm/campus_shm_<uid>)
/campus_full_<uid>     semaphore       (/dev/shm/sem.campus_full_<uid>)
/campus_empty_<uid>    semaphore       (/dev/shm/sem.campus_empty_<uid>)
```

If an old object with the same name is left over from a killed run, it is
removed before starting.

### worker (FR6)

The parent forks one worker and checks how it ended with `waitpid` and the
`WIFEXITED`/`WEXITSTATUS`/`WIFSIGNALED`/`WTERMSIG` macros.

| mode  | what the child does          | parent reports                                   |
|-------|------------------------------|--------------------------------------------------|
| ok    | sums a 200 million step loop | `worker <pid> finished normally (exit 0)`        |
| fail  | exits with code 3 on purpose | `worker <pid> FAILED with exit code 3 (simulated)` |
| crash | calls `abort()` on purpose   | `worker <pid> killed by signal 6 SIGABRT (simulated)` |

The command returns 0 when the detected result matches the mode, so all
three modes count as PASS. Any other mode is a usage error (return 1).

### limit (FR7)

The child sets `RLIMIT_CPU` to soft 1s / hard 2s with `setrlimit`, prints
the values back with `getrlimit`, then runs an endless busy loop. Once the
child has used 1 second of CPU time the kernel sends it `SIGXCPU`, whose
default action is to terminate the process. If a program ignored `SIGXCPU`
it would get `SIGKILL` at the 2s hard limit. The parent uses `wait4` (same
as `waitpid` but also returns the child's CPU usage) and prints the signal,
the wall time and the CPU time. A 15 second watchdog kills the child if the
limit never fires.

### cleanup (FR7)

Removes only this program's uid based objects (`shm_unlink`/`sem_unlink`,
"not present" is fine), then checks:

- no `/dev/shm` entries starting with `campus` or `sem.campus`
- `waitpid(-1, NULL, WNOHANG)` fails with `ECHILD`, so no child is left

Prints `cleanup OK` or lists what is left and returns 1.

### Testing

```
make clean && make
./campus ipc 5
ls /dev/shm | grep campus     # prints nothing
./campus worker ok
./campus worker fail
./campus worker crash
./campus worker xyz           # error
./campus limit
./campus cleanup
./campus ipc 0                # error
```
