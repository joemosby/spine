# Phase 0 Linux isolation knobs

Status: draft. Not a measurement. Not a guarantee.
This document lists Linux knobs Isolate will use in Phase 0. It does not
claim isolation, bounded latency, or a deadline. Those require a Harness
measurement and Architect's stamp.

This is Isolate's list against the inter-domain contract (Runtime, PR #1).
It is not that contract. Two domains only. One RT-owned mailbox is the only
cross-domain data path. These knobs are designed against that mailbox, not
a second channel.

## What Phase 0 uses

cgroup v2 only. Two leaves: `spine/rt` and `spine/be`.
Controllers we actually use: `cpuset`, `cpu`, `memory`, `pids`.

No PREEMPT_RT install in this document. No containers. No extra channel.

## Temporal wall

Disjoint `cpuset.cpus`. The RT leaf gets one exclusive CPU. The BE leaf gets
the rest. No overlap. If a CPU is in both sets, the wall is not there.

Never set `cpu.max` on RT. Do not quota the RT domain.

BE may have `cpu.max` so Harness can name a load. That is a test fixture,
not an isolation claim.

`cpu.weight` is sharing, not isolation. Do not use it as the story.

## Pressure and fork

`memory.max` and `pids.max` on BE only. RT is not memory-capped.

`memory.max` is pressure isolation, not MMU isolation. A BE that hits the
cap is starved or killed. That does not keep BE from writing RT-owned bytes.
Process address spaces do that, except for the one mailbox mapping.

## Sched

The RT thread is `SCHED_FIFO` at a fixed priority set at setup. That setup
needs privilege. The job path does not.

BE is `SCHED_OTHER`.

Do not use `SCHED_DEADLINE` in Phase 0. It invites deadline claims.

Stock host knob that affects timing: `/proc/sys/kernel/sched_rt_runtime_us`
(and `sched_rt_period_us`). Default throttles FIFO to 95% of each second.
Phase 0 either leaves the default and does not pretend RT owns 100% of its
CPU, or sets the knob in the open. Do not ignore it.

## Spatial on stock

Two processes. Separate address spaces.

The only shared mapping is the mailbox: memfd or POSIX shm, created and
owned by RT, mapped into both. Age, sequence, and valid live in that
mailbox. BE publishes a complete message or nothing. No pointers. No
shared heap.

No pipe. No socket. No watchdog. No second IPC channel.

`cgroup.kill` on `spine/be` must leave RT private mappings and the last
complete mailbox intact. RT continues. BE death is not an RT miss.

## Stock kernel: what we can and cannot demonstrate

A stock kernel can demonstrate:

- BE cannot schedule on the RT CPU
- BE memory cap and pid cap
- process isolation except the one mailbox mapping
- killing BE leaves RT and the last complete mailbox message

A stock kernel cannot demonstrate:

- bounded IRQ latency
- bounded kernel non-preemptible sections
- any deadline number

## Known stock leak (not a Phase 0 fix)

IRQs can still land on the RT CPU. That can affect timing.

Do not add `irqaffinity`, `isolcpus`, `nohz_full`, or `rcu_nocbs` as
product knobs. Those are boot theater. They are not Phase 0.

## Out this week

- PREEMPT_RT kernel build or install
- containers, Docker, Kubernetes
- `io.max`
- device controllers
- a second channel (pipe, socket, watchdog, extra mapping)

PREEMPT_RT (or equivalent) is what would be needed before anyone talks
about bounded wakeup or IRQ latency. Even then, Harness measures and
Architect stamps. This document does not.

## Decisions for Architect stamp

1. cgroup v2 only. Leaves `spine/rt` and `spine/be`. Controllers: `cpuset`, `cpu`, `memory`, `pids`.
2. Disjoint `cpuset.cpus`. No `cpu.max` on RT. `cpu.weight` is sharing, not isolation.
3. `memory.max` and `pids.max` on BE only. `memory.max` is pressure isolation, not MMU isolation.
4. RT is `SCHED_FIFO` at a fixed setup priority. BE is `SCHED_OTHER`. No `SCHED_DEADLINE`. Name `sched_rt_runtime_us`: leave the 95% default or set it in the open.
5. Two processes. One shared mailbox mapping (memfd or POSIX shm, RT-owned). No second channel. `cgroup.kill` on `spine/be` leaves RT and the last complete message.
6. Stock can show the CPU wall, BE caps, process isolation except the mailbox, and BE kill. Stock cannot show bounded IRQ/wakeup latency or a deadline. IRQ-on-RT-CPU is a known leak, not a Phase 0 fix. No boot-param product knobs. PREEMPT_RT is out this week.
