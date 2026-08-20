# Phase 0 proof spec

Status: spec only. Not measured.
No deadline, WCET, miss rate, or isolation guarantee.
Architect stamps any claim after a run.
This document names the load, the clock, and the SIL injections.
It does not contain results.

## Purpose

How Harness will run the Phase 0 proof that the RT job still completes each
period under a named best-effort load.

Phase 0 proof stays: period still met under `be-saturate` vs `be-idle`.
Not "well-behaved." Not measured until a run. Architect stamps any claim.

Phase 0 win is one run (method, not a result): period still met under
`be-saturate`; then inject the cases below; last complete mailbox message
holds across BE kill; the existing RT-owned observability record logs
kill / overrun / stale. We are a usability layer on existing isolation,
not a hypervisor. Ordinary Linux first.

## Clock

- Name: `CLOCK_MONOTONIC_RAW` via `clock_gettime`.
- The RT job-path clock read uses this clock.
- Observability fields "period start" and "job duration" are in this clock.
- Do not use `CLOCK_REALTIME`. Do not use `CLOCK_MONOTONIC` (NTP slew).
- No other measurement clock.

## Named loads

Exactly these two.

**be-idle** (control). BE process is alive. It does not burn CPU. It does not
publish to the staging slot.

**be-saturate** (Phase 0 proof load). BE busy-loops on every CPU it is allowed
to schedule on, and commits the staging slot as fast as it can (payload then
commit word only). It does not map or write sequence, age, or valid.

One mailbox only: BE → RT, depth 1, last-wins. BE writes staging payload +
commit word only. Sequence, age, and valid stay RT-private.

## Harness

- Reads the existing RT-owned observability record, read-only, not via BE.
- Observation events (timing, overrun, stale, mode, kill) ride that record.
  Runtime owns that paper. No second channel.
- Observation is not a telemetry stack. No GUI.
- Does not add a mailbox, pipe, socket, or shared page to measure.
- Does not run on the RT job path.
- Isolation knobs, if present, are Isolate's stamped knobs
  (`docs/linux-isolation-knobs.md`). This spec does not prove isolation.
  Isolate owns cgroup knobs / IRQ leak / 95% FIFO cap. Do not invent knobs.
- Harness is not a public Bazel dep.
- Simple deterministic plant later; no physics engine in Phase 0. Out of this
  spec.

## Named injections

Exactly these three. SIL only. No new mailbox, pipe, socket, or shared page.
Injection does not prove isolation. Knobs stay Isolate's (stock 95%, no `-1`,
staging-only map).

**inj-be-kill.** Isolate/supervisor kills BE with `cgroup.kill` on `spine/be`
and sets the isolation-fault word. Not a new channel. Expect: mode `be-dead`;
last complete mailbox and last RT command hold; RT still starts the next
period. BE restart does not clear the isolation-fault word.

**inj-be-overrun.** BE is alive and does not complete a publish this period.
BE overrun count increments. That is not an RT timing fault. Expect:
observation logs overrun; period still met.

**inj-stale-mailbox.** BE does not publish until age > `N`. `N` is init
config, no number in this document. Mode `be-stale`. Hold last RT command
per the contract (init-hold until first accept). Expect: observation logs
stale.

## Record

Already in the inter-domain contract. List only. Do not invent fields.

- period start
- job duration
- RT overrun flag
- mailbox sequence, age, valid
- BE overrun count, drop count
- isolation-fault flag
- hold vs consume
- mode: `normal` | `be-stale` | `be-dead` | `rt-overrun`

## What this spec does not contain

`T`, `N`, run length, CPU counts as claims, deadline, WCET, miss rate,
isolation-proved language, a second channel, C++.

Run length and `T` / `N` are init config, recorded in the run, not claims.

## Pass / fail

A run produces the observability record under both named loads.
The Phase 0 win is one run as named above: method, not a result.
Any sentence that a deadline was met needs a measurement and Architect's stamp.
This spec is the method, not the result.

## Decisions for Architect stamp

1. Clock is `CLOCK_MONOTONIC_RAW`.
2. Phase 0 proof load is `be-saturate`; control is `be-idle`.
3. Measurement uses the existing RT observability record only. No second
   channel.
4. Named injections are `inj-be-kill`, `inj-be-overrun`, `inj-stale-mailbox`.
5. Phase 0 win is one run: period-met under `be-saturate`, then those
   injections, last mailbox holds, record logs kill/overrun/stale.
