# Phase 0 inter-domain contract

Status: cut 2. Architect stamped: separate address spaces; seq/age/valid RT-only;
job path wait-free vs BE; observation events: timing, overrun, stale, mode, kill (existing record, no second channel). Not measured.
This document states rules, not measurements.
No deadline value, WCET, miss rate, or isolation guarantee is claimed here.
Those require a Harness measurement and Architect's stamp.
Lines that sound like a guarantee are **unstamped / unmeasured**.

## Purpose

Contract between the hard real-time (RT) domain and the best-effort (BE) domain.
It is not an implementation, a scheduler, or a middleware.

## Domains

Two domains only in Phase 0, plus a supervisor that is in neither domain.

**RT.** Periodic control. Must make progress every period. Owns its code, stack,
state, and every byte that can affect an actuator command. BE cannot kill RT.

**BE.** Anything without a deadline (intelligence, planning, extras). May be slow,
crash, allocate, or be killed. Must not write RT-owned bytes. Must not run on
the RT job path.

**Supervisor.** Non-RT. May start, kill, or restart BE. May write only the
RT-owned isolation-fault word. Must not run on the RT job path. Restart does
not reset RT state.

## Address spaces (spatial)

Domains do not share an address space.

BE has no store rights to RT pages except the staging slot named below.
"Must not write RT-owned bytes" is not a same-process convention.

A BE fault (wild write, use-after-free, abort) must not change RT-owned bytes
except by a store into the staging slot. Mechanism is Isolate's. **Unstamped /
unmeasured** until Isolate demonstrates it.

After BE is killed or faults, last-complete mailbox, last RT command, age,
sequence, valid, and the observability record remain intact in RT-only memory.

## Ownership of each object

Every process-visible byte has exactly one owner. If ownership is unclear, the
object is RT-owned until this contract is amended and stamped.

| Object | Owner | BE | RT | Supervisor |
| --- | --- | --- | --- | --- |
| RT thread, RT stack, RT statics, RT state (controller, last command, period config, init-hold) | RT | never | exclusive | never |
| BE thread(s), BE stack, BE state | BE | exclusive | never | may kill the process |
| Supervisor thread | supervisor | never | never | exclusive |
| Staging slot (BE → RT candidate) | RT-owned mapping; BE may store only here | write candidate payload + commit word; never partial-as-committed | read wait-free | never |
| Committed mailbox (last complete message) | RT | never | exclusive | never |
| Sequence, age, valid | RT | never | exclusive | never |
| Isolation-fault word | RT | never | read | write |
| Actuator fd | RT | never | exclusive | never |
| Clock handle | RT | never | exclusive | never |
| Observability record | RT | never (Harness/human may map read-only, not via BE) | exclusive write | read-only |

No pointers across the boundary. No shared heap. No shared allocator.

Phase 0 requires no RT → BE mailbox.

Mailbox depth is 1. Last-wins. Depth > 1 is forbidden (unbounded RT drain).

## Publish and consume

BE writes only the staging slot. A publish is **complete** when the fixed-size
payload is written and then a commit word is stored (commit after payload).
Wrong size, torn snapshot, or layout fail is not complete.

RT consume is wait-free with respect to BE:
- snapshot a committed candidate, or
- see "no new message"
including when BE is mid-publish or dead.

RT copies a complete candidate into RT-only committed storage, then validates
size and layout. Reject → treat as "no new message." BE cannot reset freshness.

Sequence, age, and valid are RT-only:
- Sequence: RT increments when it **accepts** a validated publish.
- Age: RT sets to 0 on accept; RT increments by 1 each period with no accept.
- Valid: RT sets true on first accept; false at init.

Failed publish (torn, wrong-size, layout-fail, or incomplete commit) = no new
message. Drop of a prior unconsumed candidate (last-wins) increments a drop
count. BE cannot write those counts.

## Timing (semantics, not numbers)

- RT is periodic. Period `T` is set at init and does not change at runtime in Phase 0.
- One RT job per period.
- BE has no period and no deadline.
- A BE overrun means: no accepted publish before this job's consume. That is not
  an RT timing fault.
- `T` and stale-age `N` (periods) are init configuration. They are not guarantees.

This document does not claim any deadline was met. **Unstamped / unmeasured.**

### Job path

The **job path** is the work from period start to the end of that job's
actuator publish and observability write. Period wait is not on the job path.

On the job path:
1. clock read
2. wait-free consume (or "no new message")
3. validate / accept or reject
4. control compute
5. select command (accepted message or hold)
6. actuator publish
7. write observability record

Not on the job path: period wait, BE publish into staging, BE run, supervisor,
Isolate, Harness/human read of the record, BE restart.

### Job-path bans

- No unbounded loop on the job path. Depth-1 bounds drain; it does not bound
  control compute.
- No heap. No allocation. No `std::vector` / `std::string` growth. No throw.
  No formatting logs. All buffers reserved at init.
- No lock, mutex, futex, or join shared with BE. The job path is wait-free
  w.r.t. BE.
- No blocking or allocating syscall.
- Allowed syscalls on the job path: clock read; non-blocking actuator write.
  Nothing else.

BE must not delay the RT job (CPU, preemption, priority, IRQ). Mechanism is
Isolate's. The prohibition is this contract's. **Unstamped / unmeasured**
until Isolate demonstrates it and Harness measures it.

## Failure

### BE overruns (no accepted publish this period)

RT still starts the next job. This document does not claim the deadline was met.
If mailbox valid and age ≤ `N`, RT may consume the last complete message.
If age > `N` or never valid, RT holds the last RT command (init-hold until the
first accepted publish). Count a BE overrun. Not an RT deadline miss.

### Drop

A newer complete candidate overwrites an unconsumed one: last-wins + drop count.

### Isolation fault / BE dies

Isolate or supervisor sets the RT-owned isolation-fault word. RT does not infer
death from silence (silence is stale). Mode `be-dead`, hold last RT command.
RT does not wait, join, block, or allocate because BE died.

Restart does not clear last-complete, last command, age, sequence, valid,
the observability record, or the isolation-fault word.
Isolate or supervisor is the only writer and the only clearer of the
isolation-fault word. BE restart does not clear it. While the word is set,
mode is `be-dead` and RT holds (init-hold or last command). RT does not
clear it.

### RT job overruns (still running when the next period starts)

This is an RT failure. Record an overrun. Hold the last command. Do not publish
a late actuator update. Start the next period clean. Do not pile skipped work.
This document does not claim the deadline was met. **Unstamped / unmeasured.**

### Dual faults

Record both. Mode is one enum. Precedence:
`rt-overrun` > `be-dead` > `be-stale` > `normal`.
Flags still show the other fault.

## Observability (minimum)

Every RT period writes a fixed-size, RT-owned record. No alloc, no block.
Writable only by RT. Readable if BE is dead: Harness or a human reads it via a
read-only path that does not go through BE.

Fields at least:

- period start (clock named by Harness, not this document)
- job duration
- RT overrun flag
- mailbox sequence, age, valid
- BE overrun count, drop count
- isolation-fault flag
- hold vs consume
- mode: `normal` | `be-stale` | `be-dead` | `rt-overrun`

## Observation events

Phase 0 observation is the existing RT-owned record. It is not a second mailbox,
not a BE map, and not a telemetry stack.

Every period, RT writes these event types into that record. No extra channel.

- **timing** — period start and job duration. Clock is the one named by Harness
  (`CLOCK_MONOTONIC_RAW` in the proof spec). This document does not claim a
  deadline was met. **Unstamped / unmeasured.**
- **overrun** — RT overrun flag (job still running at next period start) and BE
  overrun count (no accepted publish this period). Both stay visible. BE overrun
  is not an RT deadline miss.
- **stale** — mailbox valid and age > `N`, or never valid. RT holds. Silence is
  stale, not kill.
- **mode** — one enum: `normal` | `be-stale` | `be-dead` | `rt-overrun`.
  Precedence: `rt-overrun` > `be-dead` > `be-stale` > `normal`. Flags still show
  the other fault.
- **kill** — isolation-fault word is set (BE killed or isolation fault). Mode
  `be-dead`, hold. Isolate/supervisor is the only writer/clearer. Restart does
  not clear it.

Harness reads the same record (read-only, not via BE), including after kill.
SIL injection cases (BE kill, overrun, stale mailbox) must show on these events.
No numbers in this document.

## Out of scope

Not a middleware (no DDS, ROS, agent framework). Not a schedulability proof.
Not an isolation implementation. Not a deadline claim. Not C++.

## Decisions for Architect stamp

1. Two domains plus a supervisor. Separate address spaces. BE store rights:
   staging slot only.
2. One BE → RT mailbox, depth 1, last-wins. Staging vs committed.
   Sequence, age, valid are RT-only. Consume is wait-free.
3. Job path is wait-free w.r.t. BE. No shared lock with BE. No heap on the job path.
4. be-stale = age > N without isolation-fault. be-dead = isolation-fault word
   set by Isolate/supervisor. Silence is stale, not dead.
5. Init-hold command is set at init. Restart does not clear RT state.
6. `T` and `N` are init config, not claims. No numbers in this document.
7. Observation events timing, overrun, stale, mode, kill ride the existing RT-owned record. No second channel.
