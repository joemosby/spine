# Phase 0 proof spec

Status: spec only. Not measured.
No deadline, WCET, miss rate, or isolation guarantee.
Architect stamps any claim after a run.
This document names the load and the clock. It does not contain results.

## Purpose

How Harness will run the Phase 0 proof that the RT job still completes each
period under a named best-effort load.

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
- Does not add a mailbox, pipe, socket, or shared page to measure.
- Does not run on the RT job path.
- Isolation knobs, if present, are Isolate's stamped knobs
  (`docs/linux-isolation-knobs.md`). This spec does not prove isolation.
  Isolate owns cgroup knobs / IRQ leak / 95% FIFO cap. Do not invent knobs.
- Harness is not a public Bazel dep.
- Simple deterministic plant later; no physics engine in Phase 0. Out of this
  spec.

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
Any sentence that a deadline was met needs a measurement and Architect's stamp.
This spec is the method, not the result.

## Decisions for Architect stamp

1. Clock is `CLOCK_MONOTONIC_RAW`.
2. Phase 0 proof load is `be-saturate`; control is `be-idle`.
3. Measurement uses the existing RT observability record only. No second
   channel.
