#pragma once

#include <atomic>
#include <cstdint>

// Mailbox + loop + observation library for @spine//runtime.
// Not a scheduler. Not a cgroup call. Not a measured deadline.
// Isolation is not a dep. Never place Runtime in a BE map.

namespace spine {

enum class Mode : std::uint8_t {
  kNormal = 0,
  kBeStale = 1,
  kBeDead = 2,
  kRtOverrun = 3,
};

struct Command {
  std::uint64_t value;
};

// BE-writable staging only. Age/seq/valid MUST NOT live here.
struct StagingSlot {
  Command payload;
  std::atomic<std::uint32_t> commit;  // 0 at init; BE stores payload, then release-stores commit+1
};

struct Observation {
  std::uint64_t period_start_ns;  // CLOCK_MONOTONIC_RAW
  std::uint64_t job_duration_ns;
  bool rt_overrun;
  std::uint32_t mailbox_sequence;
  std::uint32_t mailbox_age;
  bool mailbox_valid;
  std::uint32_t be_overrun_count;
  std::uint32_t drop_count;
  bool isolation_fault;
  bool held;  // true = hold, false = consume accepted/stale-valid message
  Mode mode;
};

struct Config {
  std::uint64_t period_ns;          // T, required > 0, not a claim
  std::uint32_t stale_age_periods;  // N
  Command init_hold;
};

class Runtime {
 public:
  Runtime() = default;
  Runtime(const Runtime&) = delete;
  Runtime& operator=(const Runtime&) = delete;

  // All pointers caller-owned, valid for the lifetime. No allocation.
  // staging: BE map. isolation_fault: supervisor/Isolate writes 0/1. observation: RT writes, Harness reads.
  // Committed mailbox (seq/age/valid + last message) is PRIVATE to this object. Never place Runtime in a BE map.
  bool init(const Config& cfg, StagingSlot* staging, std::atomic<std::uint32_t>* isolation_fault,
            Observation* observation);

  // One RT job. Period wait is NOT here (caller waits).
  // Job path: clock_gettime(CLOCK_MONOTONIC_RAW), wait-free consume, validate,
  // select command (accepted or hold), optional actuator out, write observation.
  // Control compute in Phase 0 is identity: selected command is the message or hold.
  void step(Command* actuator_out /* nullable */);

  const Observation& observation() const;
  Command last_command() const;

 private:
  bool inited_ = false;
  Config cfg_{};
  StagingSlot* staging_ = nullptr;
  std::atomic<std::uint32_t>* isolation_fault_ = nullptr;
  Observation* observation_ = nullptr;

  Command committed_{};
  std::uint32_t sequence_ = 0;
  std::uint32_t age_ = 0;
  bool valid_ = false;
  std::uint32_t last_seen_commit_ = 0;
  std::uint32_t be_overrun_count_ = 0;
  std::uint32_t drop_count_ = 0;
  Command last_command_{};
};

// BE publish: write payload then release-store commit. Wait-free. No lock.
// If a prior unconsumed candidate is overwritten, the next step() increments drop_count (last-wins).
void publish(StagingSlot* staging, Command cmd);

}  // namespace spine
