#include "harness/shared.h"

#include <fcntl.h>
#include <pthread.h>
#include <sched.h>
#include <signal.h>
#include <sys/mman.h>
#include <sys/prctl.h>
#include <sys/stat.h>
#include <unistd.h>

#include <atomic>
#include <cstdint>
#include <cstdio>

namespace {

std::atomic<std::uint32_t> g_phase{spine::harness::kBeIdle};
std::atomic<int> g_stop{0};

void on_usr1(int) {
  const std::uint32_t p = g_phase.load(std::memory_order_relaxed);
  if (p < spine::harness::kBePublishOne) {
    g_phase.store(p + 1u, std::memory_order_relaxed);
  }
}

void on_term(int) { g_stop.store(1, std::memory_order_relaxed); }

void* map_staging(const char* name) {
  const int fd = shm_open(name, O_RDWR, 0);
  if (fd < 0) {
    return nullptr;
  }
  void* p = mmap(nullptr, sizeof(spine::harness::StagingMap), PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
  close(fd);
  if (p == MAP_FAILED) {
    return nullptr;
  }
  return p;
}

void* burn(void*) {
  volatile std::uint32_t sink = 0;
  while (g_phase.load(std::memory_order_relaxed) == spine::harness::kBeSaturate &&
         g_stop.load(std::memory_order_relaxed) == 0) {
    sink += 1u;
  }
  (void)sink;
  return nullptr;
}

int cpu_count() {
  cpu_set_t set;
  CPU_ZERO(&set);
  if (sched_getaffinity(0, sizeof(set), &set) != 0) {
    const long n = sysconf(_SC_NPROCESSORS_ONLN);
    return n > 0 ? static_cast<int>(n) : 1;
  }
  int n = 0;
  for (int i = 0; i < CPU_SETSIZE; ++i) {
    if (CPU_ISSET(i, &set)) {
      n += 1;
    }
  }
  return n > 0 ? n : 1;
}

}  // namespace

int main(int argc, char** argv) {
  if (argc != 2) {
    std::fprintf(stderr, "usage: be <staging_shm>\n");
    return 2;
  }

  prctl(PR_SET_PDEATHSIG, SIGKILL);
  signal(SIGUSR1, on_usr1);
  signal(SIGTERM, on_term);
  signal(SIGINT, on_term);

  // BE maps staging only (payload + commit). Does not map Observation or Runtime.
  auto* staging = static_cast<spine::harness::StagingMap*>(map_staging(argv[1]));
  if (staging == nullptr) {
    std::fprintf(stderr, "be: staging map failed\n");
    return 1;
  }

  sched_param other{};
  other.sched_priority = 0;
  sched_setscheduler(0, SCHED_OTHER, &other);

  pthread_t threads[CPU_SETSIZE];
  int nthreads = 0;

  while (g_stop.load(std::memory_order_relaxed) == 0) {
    const std::uint32_t phase = g_phase.load(std::memory_order_relaxed);
    if (phase == spine::harness::kBeIdle || phase == spine::harness::kBeSilent) {
      pause();
      continue;
    }
    if (phase == spine::harness::kBeSaturate) {
      nthreads = cpu_count();
      if (nthreads > CPU_SETSIZE) {
        nthreads = CPU_SETSIZE;
      }
      for (int i = 0; i < nthreads; ++i) {
        if (pthread_create(&threads[i], nullptr, burn, nullptr) != 0) {
          nthreads = i;
          break;
        }
      }
      while (g_phase.load(std::memory_order_relaxed) == spine::harness::kBeSaturate &&
             g_stop.load(std::memory_order_relaxed) == 0) {
        spine::publish(&staging->slot, spine::Command{spine::harness::kPublishCmd});
      }
      for (int i = 0; i < nthreads; ++i) {
        pthread_join(threads[i], nullptr);
      }
      nthreads = 0;
      continue;
    }
    if (phase == spine::harness::kBePublishOne) {
      spine::publish(&staging->slot, spine::Command{spine::harness::kPublishCmd});
      g_phase.store(spine::harness::kBeSilent, std::memory_order_relaxed);
      continue;
    }
  }

  munmap(staging, sizeof(*staging));
  return 0;
}
