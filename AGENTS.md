# Agent notes

This workspace is not fully hermetic.

The compiler is a Bazel-fetched LLVM. Native links still use the host
sysroot. Do not write "hermetic" as a claim.

Do not claim isolation, deadlines, or certification.

---

# Phase 0 brief

Start every session by reading this file. Chat is not the contract.
Long form lives in `docs/`.

Mixed-criticality runtime: hard RT control and best-effort intelligence on one
machine. Best-effort must not break RT deadlines or corrupt the RT domain.
Phase 0 only: contracts, Linux isolation, deterministic SIL + simple plant, one
reference example, threat model. Not Phase 0: robotics middleware, heavy physics,
agent frameworks, broad hardware, cert claims, cleverness.

Author never merges. One written review from someone who did not write the PR.
Guard: isolation, timing, alloc. Architect looks only if a PR changes a stamp.

## Live stamps

- **Domains:** RT and BE. Supervisor is in neither. Separate address spaces.
  BE store rights: staging slot only. (Architect, cut 2)
- **Mailbox:** one BE→RT, depth 1, last-wins. Sequence, age, valid are RT-only.
  Consume is wait-free vs BE. (Architect, cut 2)
- **Job path:** wait-free vs BE. No heap. No shared lock/futex/join with BE.
  Allowed syscalls: clock read; non-blocking actuator write. (Architect, cut 2)
- **T / N:** init config, not claims. No deadline number without a Harness
  measurement and Architect stamp.
- **Bazel:** not fully hermetic. Fetched LLVM + host sysroot. Do not write
  hermetic. (Ship / Architect)
- **Bzlmod consumer:** portable as a third-party Bzlmod module. Name `spine`.
  Public `@spine//runtime`. Consumer brings the toolchain. Isolation, when
  present, is Linux-only and not a runtime dep. Harness is not a public dep.
  No WORKSPACE. (Architect)
- **Cgroup knobs / IRQ leak / 95% FIFO cap:** Isolate owns these. Shared map
  is staging only (payload + commit). Age/seq/valid are RT-private, not in
  the BE map. Stock 95% `sched_rt_runtime_us`. Do not set it. No `-1`.
  IRQ-on-RT-CPU leak stays in the open. Long form:
  `docs/linux-isolation-knobs.md`.
- **Observation:** event types timing, overrun, stale, mode, kill. They ride the
  existing RT-owned observability record. No second channel. Not a telemetry
  stack. (Architect)
- **Proof spec:** clock `CLOCK_MONOTONIC_RAW`; named load `be-saturate`
  (control `be-idle`); named injections `inj-be-kill`, `inj-be-overrun`,
  `inj-stale-mailbox`; no numbers; long form `docs/phase0-proof-spec.md`;
  Harness is not a public dep.

## Runtime contract (cut 2)

Long form: `docs/inter-domain-contract.md`.

- Two domains + supervisor. Separate address spaces.
- BE writes only the staging slot (payload + commit word). RT copies+validates
  into committed RT-only storage. Torn / wrong-size / layout-fail = no new message.
- RT increments sequence on accept. Age is 0 on accept, else +1 each period.
  BE cannot reset freshness.
- Job path: clock, wait-free consume, validate, compute, select, non-blocking
  actuator, observability write. Period wait is not on the job path.
- Silence is be-stale. be-dead only via Isolate/supervisor isolation-fault word.
- Isolation-fault word is cleared only by Isolate/supervisor; BE restart does
  not clear it; while set, be-dead + hold.
- No unbounded loop on the job path.
- Init-hold command set at init. Restart does not clear RT state.
- Observability is a fixed RT-owned record, readable if BE is dead, not via BE.
- Observation events: timing, overrun, stale, mode, kill. Same record. No second channel.

## Consumer module (Bzlmod)

Architect stamped: portable as a third-party Bzlmod module. Requirements,
not suggestions.

- Module name is `spine`. A consumer does `bazel_dep(name = "spine")` and
  depends on the public `cc_library` at `@spine//runtime`. No WORKSPACE
  consumers.
- Public surface is small and stable: `@spine//runtime` only. That target is
  a placeholder until Runtime owns the C++. It is not a loop and not a
  cgroup call.
- Isolation, when a package exists, is Linux-only
  (`target_compatible_with`). It is not required to use the mailbox/loop
  and must not be a required dep of `@spine//runtime`. Do not claim
  isolation.
- Harness is not a public dep.
- Do not force this workspace's fetched LLVM or host sysroot on the
  consumer. They bring the toolchain. This repo's own builds still use
  the fetched LLVM and must not silently use host gcc/clang. That is a
  root-module `dev_dependency`, not a consumer requirement.
- No repo-root scripts or absolute paths in the library graph. Labels stay
  package-relative.
- This workspace is still not fully hermetic. Do not write "hermetic" as a
  claim.

## Harness

Long form: `docs/phase0-proof-spec.md`. Spec only. Not measured.

- Clock: `CLOCK_MONOTONIC_RAW`. Named load: `be-saturate`. Control: `be-idle`.
- Named injections: `inj-be-kill`, `inj-be-overrun`, `inj-stale-mailbox`.
- Measurement uses the existing RT observability record only. No second channel.
- Harness is not a public dep.
