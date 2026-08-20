#pragma once

#include "runtime/runtime.h"

#include <cstdint>

// Private harness layout. Not a public dep. Not a second mailbox.
// BE maps StagingSlot only. Observation + isolation_fault are RT-owned.
// Seq/age/valid stay inside Runtime (RT process). Never place Runtime in a BE map.

namespace spine {
namespace harness {

// CONFIG, not claims.
constexpr std::uint64_t kPeriodNs = 10000000ull;  // T = 10 ms
constexpr std::uint32_t kStaleAgePeriods = 3u;    // N
constexpr std::uint32_t kPeriodsPerLoad = 200u;   // K
constexpr std::uint64_t kInitHold = 1ull;
constexpr std::uint64_t kPublishCmd = 2ull;
constexpr int kFifoPriority = 10;

enum BePhase : std::uint32_t {
  kBeIdle = 0,
  kBeSaturate = 1,
  kBeSilent = 2,
  kBePublishOne = 3,
};

struct StagingMap {
  StagingSlot slot;
};

// RT-owned mapping. Harness/supervisor may map this. BE must not.
struct RtVisible {
  Observation observation;
  std::atomic<std::uint32_t> isolation_fault;
  std::atomic<std::uint64_t> last_command;  // Runtime::last_command after each step
};

}  // namespace harness
}  // namespace spine
