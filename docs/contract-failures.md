# Phase 0 contract failures

Status: draft. Not a deadline stamp. Not isolation-proved.
These are contract failure modes. Isolation/timing threats (FIFO miss, IRQ leak,
95% cap, mailbox map) are Isolate's. Guard reviews isolation sentences; there
are none in this file.

The run log at `docs/phase0-proof-run.md` is a measurement, not a stamp.

## Torn publish

BE writes the staging slot. A publish is complete only when the payload is
written and the commit generation is even (seqlock: odd = in progress, even =
complete). Mid-publish, including last-wins in progress, is **no new message**.

RT consume is wait-free: if commit is odd, or the two loads disagree, RT does
not accept. Torn / wrong-size / layout-fail is not a complete publish. BE cannot
reset freshness. Age and sequence stay RT-only.

## Stale

Silence is stale, not kill. If there is no accepted publish this period: age += 1
and a BE overrun is counted. If the mailbox is valid and age ≤ N, RT may consume
the last complete message. If age > N or the mailbox was never valid, RT holds.

Mode `be-stale` unless a higher mode applies. Observation events: stale, mode.
Injection `inj-stale-mailbox` must show on the existing record. No second channel.

## Isolation-fault

Isolate or supervisor sets the RT-owned isolation-fault word. RT does not infer
death from silence. Mode `be-dead`, hold. RT does not wait, join, block, or
allocate. RT does not clear the word. BE restart does not clear it.

A finished commit generation still advances `last_seen` even when the fault
rejects the payload. Completeness is a finished generation, not an accept.
Injection `inj-be-kill` must show isolation_fault + `be-dead` on the existing
record. This is not an isolation proof.

## Hold

Hold is the safe actuator choice: init-hold until the first accept, then last RT
command. Hold when: mailbox never valid; age > N; isolation-fault set; or
rt-overrun (compute `held` after the overrun check — rt-overrun holds and skips
the actuator).

A late actuator update is not published. Skipped work is not piled. Dual faults
record both. Mode precedence: `rt-overrun` > `be-dead` > `be-stale` > `normal`.

## Out of scope

Not C++. Not a scheduler. Not a telemetry stack. Not a deadline claim. Not an
isolation claim. FIFO miss and IRQ leak stay Isolate's.
