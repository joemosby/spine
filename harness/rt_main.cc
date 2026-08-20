#define _GNU_SOURCE

#include "harness/shared.h"

#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <sys/mman.h>
#include <sys/prctl.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#include <atomic>
#include <cstdint>
#include <cstdio>

namespace {

std::atomic<int> g_stop{0};

void on_term(int) { g_stop.store(1, std::memory_order_relaxed); }

void* map_shm(const char* name, std::size_t size, int prot) {
  const int fd = shm_open(name, O_RDWR, 0);
  if (fd < 0) {
    return nullptr;
  }
  void* p = mmap(nullptr, size, prot, MAP_SHARED, fd, 0);
  close(fd);
  if (p == MAP_FAILED) {
    return nullptr;
  }
  return p;
}

void add_ns(timespec* ts, std::uint64_t ns) {
  ts->tv_sec += static_cast<time_t>(ns / 1000000000ull);
  ts->tv_nsec += static_cast<long>(ns % 1000000000ull);
  if (ts->tv_nsec >= 1000000000L) {
    ts->tv_nsec -= 1000000000L;
    ts->tv_sec += 1;
  }
}

int cmp_ts(const timespec& a, const timespec& b) {
  if (a.tv_sec < b.tv_sec) {
    return -1;
  }
  if (a.tv_sec > b.tv_sec) {
    return 1;
  }
  if (a.tv_nsec < b.tv_nsec) {
    return -1;
  }
  if (a.tv_nsec > b.tv_nsec) {
    return 1;
  }
  return 0;
}

}  // namespace

int main(int argc, char** argv) {
  if (argc != 3) {
    std::fprintf(stderr, "usage: rt <staging_shm> <rt_shm>\n");
    return 2;
  }

  prctl(PR_SET_PDEATHSIG, SIGKILL);
  signal(SIGTERM, on_term);
  signal(SIGINT, on_term);

  auto* staging = static_cast<spine::harness::StagingMap*>(
      map_shm(argv[1], sizeof(spine::harness::StagingMap), PROT_READ | PROT_WRITE));
  auto* vis = static_cast<spine::harness::RtVisible*>(
      map_shm(argv[2], sizeof(spine::harness::RtVisible), PROT_READ | PROT_WRITE));
  if (staging == nullptr || vis == nullptr) {
    std::fprintf(stderr, "rt: shm map failed\n");
    return 1;
  }

  // Runtime lives in this process only. Not in the BE map.
  spine::Runtime rt;
  const spine::Config cfg{spine::harness::kPeriodNs, spine::harness::kStaleAgePeriods,
                          spine::Command{spine::harness::kInitHold}};
  if (!rt.init(cfg, &staging->slot, &vis->isolation_fault, &vis->observation)) {
    std::fprintf(stderr, "rt: init failed\n");
    return 1;
  }
  vis->last_command.store(rt.last_command().value, std::memory_order_release);

  // Period wait uses CLOCK_MONOTONIC. Measurement clock is CLOCK_MONOTONIC_RAW
  // inside Runtime::step. Wait is not on the job path.
  timespec next;
  if (clock_gettime(CLOCK_MONOTONIC, &next) != 0) {
    std::fprintf(stderr, "rt: CLOCK_MONOTONIC failed\n");
    return 1;
  }

  while (g_stop.load(std::memory_order_relaxed) == 0) {
    spine::Command actuator{};
    rt.step(&actuator);
    vis->last_command.store(rt.last_command().value, std::memory_order_release);

    add_ns(&next, spine::harness::kPeriodNs);
    timespec now;
    if (clock_gettime(CLOCK_MONOTONIC, &now) == 0) {
      while (cmp_ts(now, next) > 0) {
        add_ns(&next, spine::harness::kPeriodNs);
      }
    }
    int rc;
    do {
      rc = clock_nanosleep(CLOCK_MONOTONIC, TIMER_ABSTIME, &next, nullptr);
    } while (rc == EINTR && g_stop.load(std::memory_order_relaxed) == 0);
  }

  munmap(staging, sizeof(*staging));
  munmap(vis, sizeof(*vis));
  return 0;
}
