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
