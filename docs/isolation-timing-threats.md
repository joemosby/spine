# Phase 0 isolation/timing threats

Status: draft threats. Not a measurement. Not a guarantee. Not isolation-proved.

Isolate owns these four named isolation/timing threats: FIFO miss, IRQ leak,
95% cap, mailbox map. Runtime owns contract failures (torn, stale,
isolation-fault, hold) in a separate draft. Those sections are not here.

Knobs stay `docs/linux-isolation-knobs.md`. Stock cgroup v2, disjoint
cpuset, staging-only map, stock 95% `sched_rt_runtime_us`, no `-1`,
IRQ-on-RT-CPU leak in the open. No `isolcpus`. No PREEMPT_RT.

The #9 run log (`docs/phase0-proof-run.md`) is a measurement, not a stamp.
cgroups applied (rt=3, be=0-2). `sched_rt_runtime_us` read 950000, not
written. `SCHED_FIFO` was not set (errno 1). Kill used `cgroup.kill`.
Architect: FIFO miss means this run cannot prove period-met; will not
stamp a deadline.

Isolation is Linux-only and not a dep of `@spine//runtime`. Spine is a
layer on existing Linux isolation, not a runtime.

## 1. FIFO miss

`SCHED_FIFO` setup was not applied on the measured run (errno 1). RT ran
without FIFO. Cpuset may still be disjoint; that is not FIFO preemption.
This run cannot prove period-met. Do not treat #9 as a deadline record.

Residual: any `SCHED_OTHER` work that still lands on the RT CPU (including
kernel threads) competes fairly.

## 2. IRQ leak

IRQs can land on the RT CPU on stock Linux. That can delay the job or the
wait. Named, not fixed. `irqaffinity` / `isolcpus` / `nohz_full` are out
(boot theater). Stock kernel cannot bound IRQ/wakeup latency.

## 3. 95% cap

Stock `sched_rt_runtime_us` (950000 / 1000000) throttles `SCHED_FIFO` to
95% of each second. Phase 0 does not set it and does not set `-1`. If FIFO
is later applied, a job that lands in the throttle window can be delayed.
The #9 run did not set FIFO, so this path was not exercised; still a named
timing threat.

## 4. Mailbox map

BE must map staging only (payload + commit). Age/seq/valid and the
observation/isolation-fault record are RT-private. If BE maps those pages
(including by opening a POSIX shm by name), freshness and hold are
untrustworthy. Process isolation is only as good as what BE maps. Not a
second channel: do not add a pipe/socket to "protect" this.
