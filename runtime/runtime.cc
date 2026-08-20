#include "runtime/runtime.h"

#include <time.h>

namespace spine {
namespace {

std::uint64_t monotonic_raw_ns() {
  timespec ts;
  ts.tv_sec = 0;
  ts.tv_nsec = 0;
  if (clock_gettime(CLOCK_MONOTONIC_RAW, &ts) != 0) {
    return 0;
  }
  return static_cast<std::uint64_t>(ts.tv_sec) * 1000000000ull +
         static_cast<std::uint64_t>(ts.tv_nsec);
}

void write_observation(Observation* observation, std::uint64_t period_start_ns, std::uint64_t job_duration_ns,
                       bool rt_overrun, std::uint32_t sequence, std::uint32_t age, bool valid,
                       std::uint32_t be_overrun_count, std::uint32_t drop_count, bool isolation_fault, bool held,
                       Mode mode) {
  observation->period_start_ns = period_start_ns;
  observation->job_duration_ns = job_duration_ns;
  observation->rt_overrun = rt_overrun;
  observation->mailbox_sequence = sequence;
  observation->mailbox_age = age;
  observation->mailbox_valid = valid;
  observation->be_overrun_count = be_overrun_count;
  observation->drop_count = drop_count;
  observation->isolation_fault = isolation_fault;
  observation->held = held;
  observation->mode = mode;
}

Mode select_mode(bool rt_overrun, bool isolation_fault, bool stale) {
  if (rt_overrun) {
    return Mode::kRtOverrun;
  }
  if (isolation_fault) {
    return Mode::kBeDead;
  }
  if (stale) {
    return Mode::kBeStale;
  }
  return Mode::kNormal;
}

}  // namespace

void publish(StagingSlot* staging, Command cmd) {
  if (staging == nullptr) {
    return;
  }
  staging->payload = cmd;
  staging->commit.fetch_add(1u, std::memory_order_release);
}

bool Runtime::init(const Config& cfg, StagingSlot* staging, std::atomic<std::uint32_t>* isolation_fault,
                   Observation* observation) {
  if (cfg.period_ns == 0 || staging == nullptr || isolation_fault == nullptr || observation == nullptr) {
    return false;
  }

  cfg_ = cfg;
  staging_ = staging;
  isolation_fault_ = isolation_fault;
  observation_ = observation;

  committed_ = Command{0};
  sequence_ = 0;
  age_ = 0;
  valid_ = false;
  last_seen_commit_ = 0;
  be_overrun_count_ = 0;
  drop_count_ = 0;
  last_command_ = cfg.init_hold;

  const bool fault = isolation_fault_->load(std::memory_order_acquire) != 0;
  write_observation(observation_, 0, 0, false, 0, 0, false, 0, 0, fault, true,
                    select_mode(false, fault, true));

  inited_ = true;
  return true;
}

void Runtime::step(Command* actuator_out) {
  if (!inited_) {
    return;
  }

  const std::uint64_t period_start_ns = monotonic_raw_ns();

  const std::uint32_t c1 = staging_->commit.load(std::memory_order_acquire);
  bool have_candidate = false;
  Command candidate{0};
  if (c1 != last_seen_commit_) {
    candidate = staging_->payload;
    const std::uint32_t c2 = staging_->commit.load(std::memory_order_acquire);
    if (c1 == c2) {
      have_candidate = true;
      const std::uint32_t unseen = c1 - last_seen_commit_;
      last_seen_commit_ = c1;
      if (unseen > 1u) {
        drop_count_ += unseen - 1u;
      }
    }
  }

  const bool isolation_fault = isolation_fault_->load(std::memory_order_acquire) != 0;

  bool accepted = false;
  if (have_candidate && !isolation_fault) {
    accepted = true;
    sequence_ += 1u;
    age_ = 0;
    valid_ = true;
    committed_ = candidate;
    last_command_ = candidate;
  }

  if (!accepted) {
    age_ += 1u;
    be_overrun_count_ += 1u;
  }

  bool held = true;
  if (isolation_fault) {
    held = true;
  } else if (accepted) {
    held = false;
  } else if (valid_ && age_ <= cfg_.stale_age_periods) {
    held = false;
    last_command_ = committed_;
  } else {
    held = true;
  }

  const std::uint64_t job_end_ns = monotonic_raw_ns();
  const std::uint64_t job_duration_ns = job_end_ns - period_start_ns;
  const bool rt_overrun = job_duration_ns >= cfg_.period_ns;

  if (!rt_overrun && actuator_out != nullptr) {
    *actuator_out = last_command_;
  }

  const bool stale = !valid_ || age_ > cfg_.stale_age_periods;
  const Mode mode = select_mode(rt_overrun, isolation_fault, stale);

  write_observation(observation_, period_start_ns, job_duration_ns, rt_overrun, sequence_, age_, valid_,
                    be_overrun_count_, drop_count_, isolation_fault, held, mode);
}

const Observation& Runtime::observation() const {
  if (observation_ != nullptr) {
    return *observation_;
  }
  static const Observation kEmpty{};
  return kEmpty;
}

Command Runtime::last_command() const { return last_command_; }

}  // namespace spine
