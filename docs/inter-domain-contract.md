# Phase 0 inter-domain contract

Status: draft. Not stamped. This document states rules, not measurements.
No deadline value, WCET, miss rate, or isolation guarantee is claimed here.
Those require a Harness measurement and Architect's stamp.

## Purpose

Contract between the hard real-time (RT) domain and the best-effort (BE) domain.
It is not an implementation, a scheduler, or a middleware.

## Domains

Two domains only in Phase 0.

**RT.** Periodic control. Must make progress every period. Owns its code, stack,
state, and every byte that can affect an actuator command. BE cannot kill RT.

**BE.** Anything without a deadline (intelligence, planning, extras). May be slow,
crash, allocate, or be killed. Must not write RT-owned bytes. Must not run on
the RT job path.

## Ownership of each byte

Every process-visible byte has exactly one owner. If ownership is unclear, the
byte is RT-owned until this contract is amended and stamped.

| Class | Owner | BE | RT |
| --- | --- | --- | --- |
| RT state (controller state, last command, period config) | RT | no read, no write, except via the mailbox | read/write |
| RT stack, statics, and any RT storage | RT | never | exclusive |
| BE state | BE | exclusive | never; RT does not touch BE memory |
| Mailbox BE → RT | RT-owned fixed-size buffer | publish one complete message via the publish primitive; never a partial write | read a complete snapshot, or see that there is no new message |

The mailbox is the only legal cross-domain data path. Capacity is fixed at init.
No pointers across the boundary. No shared heap. No shared allocator.
RT never dereferences a BE pointer. BE never dereferences an RT pointer.

Phase 0 requires no RT → BE mailbox.

## Timing (semantics, not numbers)

- RT is periodic. Period `T` is set at init and does not change at runtime in Phase 0.
- One RT job per period. That job is bounded: no unbounded loops, no syscalls on
  the job path, no allocation on the job path unless the bound is fixed at init
  and justified in the same change.
- BE has no period and no deadline.
- A BE overrun means: BE did not publish a complete mailbox message before the
  RT job that would consume it started. That is not an RT timing fault.
- `T` and stale-age `N` (periods) are init configuration. They are not guarantees.

## Failure

### BE dies (crash, kill, exit)

RT continues the next period. It does not wait, join, block, or allocate because
BE died. The last complete mailbox message remains. Age and sequence make it
observable as stale. A non-RT supervisor may restart BE; that restart does not
reset RT state.

### BE overruns (no fresh publish this period)

RT runs on time. If mailbox age ≤ `N` periods, RT may consume the last complete
message. If age > `N` or the mailbox has never been valid, RT uses the last RT
command (hold). Overrun is counted and visible. It is not an RT deadline miss.

### RT job overruns (still running when the next period starts)

This is an RT failure. Record an overrun. Hold the last command. Do not publish
a late actuator update. Start the next period clean. Do not pile skipped work.

### Spatial

A BE fault (wild write, use-after-free, abort) must not change RT-owned bytes
except through a successful complete publish. Mechanism is Isolate's. After BE
is killed or faults, RT state and the last complete mailbox remain intact.

## Observability (minimum)

Every RT period exposes at least:

- period start (clock named by Harness, not this document)
- job duration
- RT overrun flag
- mailbox sequence, age, valid
- mode: `normal` | `be-stale` | `be-dead` | `rt-overrun`

## Out of scope

Not a middleware (no DDS, ROS, agent framework). Not a schedulability proof.
Not an isolation implementation. Not a deadline claim. Not C++.

## Decisions for Architect stamp

1. Two domains only. One BE → RT mailbox, RT-owned, fixed size, complete message or nothing.
2. Last complete message persists across BE death; age and sequence mark stale.
3. BE stale/dead: hold last RT command when age > `N` or mailbox never valid.
4. RT overrun: hold last command, skip, do not pile.
5. `T` and `N` are init config, not claims. No numbers in this document.
