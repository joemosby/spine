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
- **Cgroup knobs / IRQ leak / 95% FIFO cap:** Isolate owns these. Do not invent
  them here. Long form: `docs/linux-isolation-knobs.md` when stamped.

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
- Init-hold command set at init. Restart does not clear RT state.
- Observability is a fixed RT-owned record, readable if BE is dead, not via BE.
