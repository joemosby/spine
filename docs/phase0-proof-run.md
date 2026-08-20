# Phase 0 proof run log

Status: measurement, not a stamp. Architect stamps any deadline claim.
This file is a record of one run. It does not prove isolation.
It does not claim a deadline was met.

## Host

- uname -r: 6.12.94+
- nproc: 4
- cgroup v2 mounted: yes (/sys/fs/cgroup)
- sched_rt_runtime_us (read, not set): 950000

## Knobs actually applied

cgroups applied: leaves /sys/fs/cgroup/spine/rt and /sys/fs/cgroup/spine/be.
disjoint cpuset.cpus: rt=3 be=0-2 (no overlap).
cpu.max on RT: not written.
sched_rt_runtime_us: not written.
no isolcpus, no irqaffinity, no PREEMPT_RT install.
RT pid moved to spine/rt. BE pid moved to spine/be. Supervisor left in its original cgroup.
sudo -n was used for some cgroup file writes.
SCHED_FIFO was not set (errno 1: Operation not permitted).
BE SCHED_OTHER: requested in the BE process (default class).
Kill method: cgroup.kill on /sys/fs/cgroup/spine/be.

## CONFIG (not claims)

- period_ns T = 10000000
- stale_age_periods N = 3
- periods per load phase K = 200
- init_hold = 1
- first published command = 2

## Clock

- job-path measurement clock: CLOCK_MONOTONIC_RAW via clock_gettime (Runtime::step)
- period wait clock: CLOCK_MONOTONIC via clock_nanosleep (not the measurement clock)

## Build

- supervisor argv0: ./bazel-bin/harness/phase0_run
- rt binary: ./bazel-bin/harness/rt
- be binary: ./bazel-bin/harness/be
- commands that actually ran:

```
bazelisk test //runtime:runtime_test --test_output=errors
bazelisk build //harness:phase0_run //harness:rt //harness:be //runtime:runtime //:docs
./bazel-bin/harness/phase0_run ./bazel-bin/harness/rt ./bazel-bin/harness/be docs/phase0-proof-run.md
```

Counts below are computed from the existing Observation record only.
last_command is Runtime::last_command, written by the RT process into the
RT-owned mapping (Observation + isolation_fault). BE does not map that mapping.

## be-idle

- periods (Observation period_start_ns changes): 200
- rt_overrun count (samples with rt_overrun==true): 0
- min job_duration_ns: 210
- max job_duration_ns: 520
- last job_duration_ns: 292
- last mode: be-stale
- last be_overrun_count: 204
- last drop_count: 0

## be-saturate

- periods (Observation period_start_ns changes): 200
- rt_overrun count (samples with rt_overrun==true): 0
- min job_duration_ns: 114
- max job_duration_ns: 391
- last job_duration_ns: 274
- last mode: normal
- last be_overrun_count: 305
- last drop_count: 73632970

## be-saturate rt_overrun fact

Every sampled be-saturate period had rt_overrun==false (200 of 200 samples from the Observation record). Record fact, not a deadline claim.

## inj-be-overrun

Observation snapshot after BE stopped publishing for at least one RT period.
- period_start_ns: 744524836669
- job_duration_ns: 237
- rt_overrun: false
- mailbox_sequence: 99
- mailbox_age: 1
- mailbox_valid: true
- be_overrun_count: 306
- drop_count: 73632970
- isolation_fault: false
- held: false
- mode: normal
- last_command: 2

## inj-stale-mailbox

Observation snapshot after mailbox_age > N.
- period_start_ns: 744554957306
- job_duration_ns: 407
- rt_overrun: false
- mailbox_sequence: 99
- mailbox_age: 4
- mailbox_valid: true
- be_overrun_count: 309
- drop_count: 73632970
- isolation_fault: false
- held: true
- mode: be-stale
- last_command: 2

## accept after stale (before kill)

Observation snapshot after BE published one complete message and RT accepted it.
- period_start_ns: 744564960752
- job_duration_ns: 234
- rt_overrun: false
- mailbox_sequence: 100
- mailbox_age: 0
- mailbox_valid: true
- be_overrun_count: 309
- drop_count: 73632970
- isolation_fault: false
- held: false
- mode: normal
- last_command: 2

## inj-be-kill

Observation snapshot after kill + isolation_fault set.
- period_start_ns: 744574860951
- job_duration_ns: 318
- rt_overrun: false
- mailbox_sequence: 100
- mailbox_age: 1
- mailbox_valid: true
- be_overrun_count: 310
- drop_count: 73632970
- isolation_fault: true
- held: true
- mode: be-dead
- last_command: 2

A later Observation.period_start_ns after that snapshot: 744584885027 (RT still stepped).

