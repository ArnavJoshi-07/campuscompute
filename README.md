# CampusCompute

## Prerequisites

## Build

## Commands

## Cleanup

## Part A notes

Part A covers job loading, CPU scheduling, the producer-consumer logger and
the Banker's safety check.

### Commands

```
./campus jobs tests/jobs1.txt              # load and print the job table
./campus fcfs tests/jobs1.txt              # first come first served
./campus rr tests/jobs1.txt 2              # round robin, quantum 2
./campus priority tests/jobs1.txt          # non-preemptive priority
./campus compare tests/jobs1.txt 2         # all three, one summary table
./campus logger 2 2 10                     # producers consumers events
./campus banker tests/banker_safe.txt      # safe state
./campus banker tests/banker_unsafe.txt    # unsafe state
```

Other test inputs: `tests/jobs2.txt` (CPU idle gaps), `tests/jobs_bad.txt`
and `tests/banker_bad.txt` (should be rejected with a line number).

### Scheduling rules

- Time quantum (rr, compare) must be a positive integer, 1 to 100000.
- Tie rule: earlier arrival first, then lower job id.
- Priority: lower number = higher priority. Non-preemptive, so a job
  runs to the end once it starts. Equal priorities use the tie rule.
- Round robin: jobs that arrive during a time slice join the ready queue
  before the job that was just preempted. If no other job is waiting the
  running job keeps the CPU.
- If no job has arrived yet the CPU is shown as `idle` in the timeline.
- CT = completion time, TAT = CT - arrival, WT = TAT - burst.
- compare picks the algorithm with the lowest average WT (ties broken by
  lower average TAT).
- Status changes (NEW -> READY -> RUNNING -> DONE) are printed and written
  to `logs/campus.log`.

### Logger

`./campus logger [P] [C] [E]`, defaults 2 producers, 2 consumers, 10 events.
P and C must be 1 to 16, E must be 1 to 1000. The buffer holds 5 events.
Events are split between producers as evenly as possible. Every consumed
event is written to `logs/campus.log`.

### Input formats

Job file, one job per line, `#` starts a comment:

```
# id arrival burst priority
1 0 5 3
2 1 3 1
```

Rules: 4 integers per line, id > 0 and unique, arrival >= 0, burst > 0,
priority >= 0, at most 50 jobs (MAX_JOBS). Arrival and burst are capped at
100000.

Banker file:

```
n m                 (processes, resource types)
Available           (1 line, m numbers)
Allocation          (n lines, m numbers each)
Max                 (n lines, m numbers each)
```

Rules: 1 <= n <= 10, 1 <= m <= 10, no negative numbers, Allocation <= Max
for every entry. Processes are numbered from P0. Both SAFE and UNSAFE
results return 0, only bad input is an error.

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
