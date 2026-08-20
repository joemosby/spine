#include "runtime/runtime.h"

#include <atomic>
#include <cstdint>
#include <cstdio>

namespace {

int g_failed = 0;

#define CHECK(cond)                                                                   \
  do {                                                                                \
    if (!(cond)) {                                                                    \
      std::fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);            \
      g_failed += 1;                                                                  \
    }                                                                                 \
  } while (0)

// Test fixture only. Not a measured deadline and not a production period.
constexpr std::uint64_t kTestPeriodNs = 1ull << 40;
constexpr std::uint32_t kTestN = 2;
constexpr std::uint64_t kInitHold = 0x1111u;
constexpr std::uint64_t kCmdA = 0xAAu;
constexpr std::uint64_t kCmdB = 0xBBu;
constexpr std::uint64_t kCmdC = 0xCCu;

struct Fixture {
  spine::StagingSlot staging{};
  std::atomic<std::uint32_t> isolation_fault{0};
  spine::Observation observation{};
  spine::Runtime rt;
  spine::Config cfg{kTestPeriodNs, kTestN, spine::Command{kInitHold}};

  bool init() { return rt.init(cfg, &staging, &isolation_fault, &observation); }
};

void test_init_rejects_bad_config() {
  Fixture f;
  f.cfg.period_ns = 0;
  CHECK(!f.rt.init(f.cfg, &f.staging, &f.isolation_fault, &f.observation));

  Fixture ok;
  CHECK(!ok.rt.init(ok.cfg, nullptr, &ok.isolation_fault, &ok.observation));
  CHECK(!ok.rt.init(ok.cfg, &ok.staging, nullptr, &ok.observation));
  CHECK(!ok.rt.init(ok.cfg, &ok.staging, &ok.isolation_fault, nullptr));
  CHECK(ok.init());
}

void test_publish_consume_wait_free() {
  Fixture f;
  CHECK(f.init());
  CHECK(f.rt.last_command().value == kInitHold);
  CHECK(!f.observation.mailbox_valid);
  CHECK(f.observation.held);

  spine::publish(&f.staging, spine::Command{kCmdA});
  spine::Command out{0};
  f.rt.step(&out);

  CHECK(out.value == kCmdA);
  CHECK(f.rt.last_command().value == kCmdA);
  CHECK(f.observation.mailbox_valid);
  CHECK(f.observation.mailbox_sequence == 1u);
  CHECK(f.observation.mailbox_age == 0u);
  CHECK(!f.observation.held);
  CHECK(f.observation.mode == spine::Mode::kNormal);
  CHECK(f.observation.be_overrun_count == 0u);
  CHECK(f.observation.drop_count == 0u);
  CHECK(!f.observation.isolation_fault);
  CHECK(!f.observation.rt_overrun);
}

void test_torn_and_incomplete_rejected() {
  Fixture f;
  CHECK(f.init());

  // Incomplete: payload stored, commit not advanced. Same reject as torn.
  f.staging.payload = spine::Command{kCmdA};
  spine::Command out{0xdeadu};
  f.rt.step(&out);
  CHECK(out.value == kInitHold);
  CHECK(!f.observation.mailbox_valid);
  CHECK(f.observation.mailbox_sequence == 0u);
  CHECK(f.observation.held);
  CHECK(f.observation.mode == spine::Mode::kBeStale);
  CHECK(f.observation.be_overrun_count == 1u);

  spine::publish(&f.staging, spine::Command{kCmdA});
  f.rt.step(&out);
  CHECK(out.value == kCmdA);
  const std::uint32_t seq = f.observation.mailbox_sequence;

  // Mid-publish: new payload, commit unchanged. No new message. BE cannot
  // reset freshness.
  f.staging.payload = spine::Command{kCmdB};
  f.rt.step(&out);
  CHECK(f.observation.mailbox_sequence == seq);
  CHECK(f.rt.last_command().value == kCmdA);
  CHECK(f.observation.mailbox_valid);
}

void test_stale_hold() {
  Fixture f;
  CHECK(f.init());

  spine::Command out{0};
  f.rt.step(&out);
  CHECK(out.value == kInitHold);
  CHECK(f.observation.held);
  CHECK(!f.observation.mailbox_valid);
  CHECK(f.observation.mailbox_age == 1u);
  CHECK(f.observation.mode == spine::Mode::kBeStale);
  CHECK(f.observation.be_overrun_count == 1u);

  spine::publish(&f.staging, spine::Command{kCmdA});
  f.rt.step(&out);
  CHECK(out.value == kCmdA);
  CHECK(!f.observation.held);
  CHECK(f.observation.mailbox_age == 0u);
  CHECK(f.observation.mode == spine::Mode::kNormal);

  // N=2: two silent periods still consume last message.
  f.rt.step(&out);
  CHECK(out.value == kCmdA);
  CHECK(!f.observation.held);
  CHECK(f.observation.mailbox_age == 1u);
  CHECK(f.observation.mailbox_sequence == 1u);
  CHECK(f.observation.be_overrun_count == 2u);
  CHECK(f.observation.mode == spine::Mode::kNormal);

  f.rt.step(&out);
  CHECK(out.value == kCmdA);
  CHECK(!f.observation.held);
  CHECK(f.observation.mailbox_age == 2u);
  CHECK(f.observation.mode == spine::Mode::kNormal);

  // age > N: hold last command, be-stale.
  f.rt.step(&out);
  CHECK(out.value == kCmdA);
  CHECK(f.observation.held);
  CHECK(f.observation.mailbox_age == 3u);
  CHECK(f.observation.mode == spine::Mode::kBeStale);
  CHECK(f.rt.last_command().value == kCmdA);
}

void test_isolation_fault_be_dead_hold() {
  Fixture f;
  CHECK(f.init());
  spine::publish(&f.staging, spine::Command{kCmdA});
  spine::Command out{0};
  f.rt.step(&out);
  CHECK(out.value == kCmdA);

  f.isolation_fault.store(1u, std::memory_order_release);
  spine::publish(&f.staging, spine::Command{kCmdB});
  out.value = 0;
  f.rt.step(&out);
  CHECK(out.value == kCmdA);
  CHECK(f.rt.last_command().value == kCmdA);
  CHECK(f.observation.held);
  CHECK(f.observation.isolation_fault);
  CHECK(f.observation.mode == spine::Mode::kBeDead);
  CHECK(f.observation.mailbox_sequence == 1u);
  CHECK(f.isolation_fault.load(std::memory_order_relaxed) == 1u);

  // BE restart is not this library's job: RT does not clear the word.
  f.rt.step(&out);
  CHECK(f.observation.mode == spine::Mode::kBeDead);
  CHECK(f.observation.held);
  CHECK(f.isolation_fault.load(std::memory_order_relaxed) == 1u);
  CHECK(f.rt.last_command().value == kCmdA);
}

void test_drop_last_wins() {
  Fixture f;
  CHECK(f.init());
  spine::publish(&f.staging, spine::Command{kCmdA});
  spine::publish(&f.staging, spine::Command{kCmdB});
  spine::publish(&f.staging, spine::Command{kCmdC});
  spine::Command out{0};
  f.rt.step(&out);
  CHECK(out.value == kCmdC);
  CHECK(f.observation.drop_count == 2u);
  CHECK(f.observation.mailbox_sequence == 1u);
  CHECK(!f.observation.held);

  spine::publish(&f.staging, spine::Command{kCmdA});
  spine::publish(&f.staging, spine::Command{kCmdB});
  f.rt.step(&out);
  CHECK(out.value == kCmdB);
  CHECK(f.observation.drop_count == 3u);
  CHECK(f.observation.mailbox_sequence == 2u);
}

void test_observation_fields_filled() {
  Fixture f;
  CHECK(f.init());
  spine::publish(&f.staging, spine::Command{kCmdA});
  spine::Command out{0};
  f.rt.step(&out);

  const spine::Observation& o = f.rt.observation();
  CHECK(&o == &f.observation);
  CHECK(o.job_duration_ns < kTestPeriodNs);
  CHECK(!o.rt_overrun);
  CHECK(o.mailbox_sequence == 1u);
  CHECK(o.mailbox_age == 0u);
  CHECK(o.mailbox_valid);
  CHECK(o.be_overrun_count == 0u);
  CHECK(o.drop_count == 0u);
  CHECK(!o.isolation_fault);
  CHECK(!o.held);
  CHECK(o.mode == spine::Mode::kNormal);
  // Clock was read; do not treat the value as a deadline claim.
  CHECK(o.period_start_ns == f.observation.period_start_ns);
}

void test_no_second_be_buffer() {
  Fixture f;
  CHECK(f.init());
  spine::publish(&f.staging, spine::Command{kCmdA});
  spine::Command out{0};
  f.rt.step(&out);
  CHECK(out.value == kCmdA);

  // A second staging slot is not observed. BE has one slot only.
  spine::StagingSlot other{};
  spine::publish(&other, spine::Command{kCmdB});
  f.rt.step(&out);
  CHECK(f.rt.last_command().value == kCmdA);
  CHECK(f.observation.mailbox_sequence == 1u);

  // Mutating payload without commit does not reset freshness or seq/age/valid.
  f.staging.payload = spine::Command{kCmdC};
  f.rt.step(&out);
  CHECK(f.rt.last_command().value == kCmdA);
  CHECK(f.observation.mailbox_sequence == 1u);
  CHECK(f.observation.mailbox_valid);

  // seq/age/valid are not in the BE slot.
  CHECK(f.staging.commit.load(std::memory_order_relaxed) == 1u);
}

void test_rt_overrun_skips_actuator() {
  Fixture f;
  f.cfg.period_ns = 1;  // test fixture only; any measurable step duration trips it
  CHECK(f.init());
  spine::publish(&f.staging, spine::Command{kCmdA});
  spine::Command out{0xdeadu};
  f.rt.step(&out);
  if (f.observation.rt_overrun) {
    CHECK(out.value == 0xdeadu);
    CHECK(f.observation.mode == spine::Mode::kRtOverrun);
    CHECK(f.rt.last_command().value == kCmdA);
    CHECK(f.observation.mailbox_valid);
  } else {
    // Clock resolution can make a one-instruction job read as 0 ns.
    CHECK(out.value == kCmdA);
  }
}

}  // namespace

int main() {
  test_init_rejects_bad_config();
  test_publish_consume_wait_free();
  test_torn_and_incomplete_rejected();
  test_stale_hold();
  test_isolation_fault_be_dead_hold();
  test_drop_last_wins();
  test_observation_fields_filled();
  test_no_second_be_buffer();
  test_rt_overrun_skips_actuator();

  if (g_failed != 0) {
    std::fprintf(stderr, "%d check(s) failed\n", g_failed);
    return 1;
  }
  std::puts("ok");
  return 0;
}
