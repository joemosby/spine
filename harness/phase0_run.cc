#include "harness/shared.h"

#include <errno.h>
#include <fcntl.h>
#include <sched.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/utsname.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#include <atomic>
#include <cstdint>
#include <cstdio>

namespace {

struct Snapshot {
  spine::Observation o{};
  std::uint64_t last_command = 0;
  bool ok = false;
};

struct PhaseAccum {
  std::uint32_t periods = 0;
  std::uint32_t rt_overrun_true = 0;
  std::uint64_t min_job = 0;
  std::uint64_t max_job = 0;
  std::uint64_t last_job = 0;
  bool have_job = false;
  spine::Mode last_mode = spine::Mode::kNormal;
  std::uint32_t last_be_overrun = 0;
  std::uint32_t last_drop = 0;
  bool have_sample = false;
};

struct Knobs {
  bool cgroups_applied = false;
  bool used_sudo = false;
  bool fifo_set = false;
  int fifo_errno = 0;
  int nproc = 0;
  int rt_cpu = -1;
  char be_cpus[64]{};
  char rt_cpus[16]{};
  char knobs_text[4096]{};
  char kill_method[128]{};
};

struct Host {
  char uname_r[128]{};
  int nproc = 0;
  bool cgroup2 = false;
  char cgroup2_mp[128]{};
  char sched_rt_runtime_us[64]{};
};

char g_knobs_buf[4096];
std::size_t g_knobs_len = 0;

void knobs_note(const char* line) {
  const std::size_t n = strlen(line);
  if (g_knobs_len + n + 2 >= sizeof(g_knobs_buf)) {
    return;
  }
  memcpy(g_knobs_buf + g_knobs_len, line, n);
  g_knobs_len += n;
  g_knobs_buf[g_knobs_len++] = '\n';
  g_knobs_buf[g_knobs_len] = 0;
}

void sleep_ns(std::uint64_t ns) {
  timespec ts;
  ts.tv_sec = static_cast<time_t>(ns / 1000000000ull);
  ts.tv_nsec = static_cast<long>(ns % 1000000000ull);
  while (clock_nanosleep(CLOCK_MONOTONIC, 0, &ts, &ts) == EINTR) {
  }
}

bool read_text(const char* path, char* out, std::size_t out_sz) {
  const int fd = open(path, O_RDONLY);
  if (fd < 0) {
    return false;
  }
  const ssize_t n = read(fd, out, out_sz - 1);
  close(fd);
  if (n < 0) {
    return false;
  }
  out[n] = 0;
  for (ssize_t i = 0; i < n; ++i) {
    if (out[i] == '\n') {
      out[i] = 0;
      break;
    }
  }
  return true;
}

int run_argv(char* const argv[]) {
  const pid_t pid = fork();
  if (pid < 0) {
    return 127;
  }
  if (pid == 0) {
    execvp(argv[0], argv);
    _exit(127);
  }
  int st = 0;
  if (waitpid(pid, &st, 0) < 0) {
    return 127;
  }
  if (WIFEXITED(st)) {
    return WEXITSTATUS(st);
  }
  return 1;
}

bool write_text(const char* path, const char* text, bool* used_sudo) {
  const int fd = open(path, O_WRONLY | O_TRUNC);
  if (fd >= 0) {
    const std::size_t n = strlen(text);
    const ssize_t w = write(fd, text, n);
    close(fd);
    return w == static_cast<ssize_t>(n);
  }
  int pfd[2];
  if (pipe(pfd) != 0) {
    return false;
  }
  const pid_t pid = fork();
  if (pid < 0) {
    close(pfd[0]);
    close(pfd[1]);
    return false;
  }
  if (pid == 0) {
    dup2(pfd[0], 0);
    close(pfd[0]);
    close(pfd[1]);
    const int nullfd = open("/dev/null", O_WRONLY);
    if (nullfd >= 0) {
      dup2(nullfd, 1);
      close(nullfd);
    }
    execlp("sudo", "sudo", "-n", "tee", path, static_cast<char*>(nullptr));
    _exit(127);
  }
  close(pfd[0]);
  const std::size_t n = strlen(text);
  const ssize_t w = write(pfd[1], text, n);
  close(pfd[1]);
  int st = 0;
  waitpid(pid, &st, 0);
  if (w == static_cast<ssize_t>(n) && WIFEXITED(st) && WEXITSTATUS(st) == 0) {
    if (used_sudo != nullptr) {
      *used_sudo = true;
    }
    return true;
  }
  return false;
}

bool mkdir_p(const char* path, bool* used_sudo) {
  if (mkdir(path, 0755) == 0 || errno == EEXIST) {
    return true;
  }
  char* argv[] = {const_cast<char*>("sudo"), const_cast<char*>("-n"), const_cast<char*>("mkdir"),
                  const_cast<char*>("-p"), const_cast<char*>(path), nullptr};
  if (run_argv(argv) == 0) {
    if (used_sudo != nullptr) {
      *used_sudo = true;
    }
    return true;
  }
  return false;
}

bool rmdir_priv(const char* path) {
  if (rmdir(path) == 0) {
    return true;
  }
  char* argv[] = {const_cast<char*>("sudo"), const_cast<char*>("-n"), const_cast<char*>("rmdir"),
                  const_cast<char*>(path), nullptr};
  return run_argv(argv) == 0;
}

void* create_shm(const char* name, std::size_t size) {
  shm_unlink(name);
  const int fd = shm_open(name, O_CREAT | O_RDWR, 0600);
  if (fd < 0) {
    return nullptr;
  }
  if (ftruncate(fd, static_cast<off_t>(size)) != 0) {
    close(fd);
    shm_unlink(name);
    return nullptr;
  }
  void* p = mmap(nullptr, size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
  close(fd);
  if (p == MAP_FAILED) {
    shm_unlink(name);
    return nullptr;
  }
  memset(p, 0, size);
  return p;
}

Snapshot copy_obs(spine::harness::RtVisible* vis) {
  Snapshot s{};
  for (int i = 0; i < 8; ++i) {
    const std::uint64_t t1 = vis->observation.period_start_ns;
    s.o = vis->observation;
    const std::uint64_t t2 = vis->observation.period_start_ns;
    s.last_command = vis->last_command.load(std::memory_order_acquire);
    if (t1 == t2) {
      s.ok = t1 != 0;
      return s;
    }
  }
  s.ok = false;
  return s;
}

void accum(PhaseAccum* a, const spine::Observation& o) {
  a->periods += 1u;
  if (o.rt_overrun) {
    a->rt_overrun_true += 1u;
  }
  if (!a->have_job || o.job_duration_ns < a->min_job) {
    a->min_job = o.job_duration_ns;
  }
  if (!a->have_job || o.job_duration_ns > a->max_job) {
    a->max_job = o.job_duration_ns;
  }
  a->last_job = o.job_duration_ns;
  a->have_job = true;
  a->last_mode = o.mode;
  a->last_be_overrun = o.be_overrun_count;
  a->last_drop = o.drop_count;
  a->have_sample = true;
}

bool collect_k(spine::harness::RtVisible* vis, PhaseAccum* a, std::uint32_t k, std::uint64_t timeout_ns,
               std::uint64_t* last_ps) {
  timespec start;
  clock_gettime(CLOCK_MONOTONIC, &start);
  while (a->periods < k) {
    timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);
    const std::uint64_t elapsed = static_cast<std::uint64_t>(now.tv_sec - start.tv_sec) * 1000000000ull +
                                  static_cast<std::uint64_t>(now.tv_nsec - start.tv_nsec);
    if (elapsed > timeout_ns) {
      return false;
    }
    const Snapshot s = copy_obs(vis);
    if (s.ok && s.o.period_start_ns != *last_ps) {
      *last_ps = s.o.period_start_ns;
      accum(a, s.o);
    }
    sleep_ns(200000ull);
  }
  return true;
}

bool wait_pred(spine::harness::RtVisible* vis, std::uint64_t timeout_ns, Snapshot* out,
               bool (*pred)(const Snapshot&)) {
  timespec start;
  clock_gettime(CLOCK_MONOTONIC, &start);
  for (;;) {
    timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);
    const std::uint64_t elapsed = static_cast<std::uint64_t>(now.tv_sec - start.tv_sec) * 1000000000ull +
                                  static_cast<std::uint64_t>(now.tv_nsec - start.tv_nsec);
    if (elapsed > timeout_ns) {
      return false;
    }
    const Snapshot s = copy_obs(vis);
    if (s.ok && pred(s)) {
      *out = s;
      return true;
    }
    sleep_ns(200000ull);
  }
}

const char* mode_name(spine::Mode m) {
  switch (m) {
    case spine::Mode::kNormal:
      return "normal";
    case spine::Mode::kBeStale:
      return "be-stale";
    case spine::Mode::kBeDead:
      return "be-dead";
    case spine::Mode::kRtOverrun:
      return "rt-overrun";
  }
  return "unknown";
}

void write_tf(FILE* f, const char* name, bool v) {
  std::fprintf(f, "- %s: %s\n", name, v ? "true" : "false");
}

void write_snap(FILE* f, const Snapshot& s) {
  if (!s.ok) {
    std::fprintf(f, "No consistent Observation snapshot (period_start_ns was 0 or torn).\n");
    return;
  }
  std::fprintf(f, "- period_start_ns: %llu\n", static_cast<unsigned long long>(s.o.period_start_ns));
  std::fprintf(f, "- job_duration_ns: %llu\n", static_cast<unsigned long long>(s.o.job_duration_ns));
  write_tf(f, "rt_overrun", s.o.rt_overrun);
  std::fprintf(f, "- mailbox_sequence: %u\n", s.o.mailbox_sequence);
  std::fprintf(f, "- mailbox_age: %u\n", s.o.mailbox_age);
  write_tf(f, "mailbox_valid", s.o.mailbox_valid);
  std::fprintf(f, "- be_overrun_count: %u\n", s.o.be_overrun_count);
  std::fprintf(f, "- drop_count: %u\n", s.o.drop_count);
  write_tf(f, "isolation_fault", s.o.isolation_fault);
  write_tf(f, "held", s.o.held);
  std::fprintf(f, "- mode: %s\n", mode_name(s.o.mode));
  std::fprintf(f, "- last_command: %llu\n", static_cast<unsigned long long>(s.last_command));
}

void write_phase(FILE* f, const char* title, const PhaseAccum& a) {
  std::fprintf(f, "## %s\n\n", title);
  if (!a.have_sample) {
    std::fprintf(f, "No Observation samples in this phase. Numbers omitted.\n\n");
    return;
  }
  std::fprintf(f, "- periods (Observation period_start_ns changes): %u\n", a.periods);
  std::fprintf(f, "- rt_overrun count (samples with rt_overrun==true): %u\n", a.rt_overrun_true);
  if (a.have_job) {
    std::fprintf(f, "- min job_duration_ns: %llu\n", static_cast<unsigned long long>(a.min_job));
    std::fprintf(f, "- max job_duration_ns: %llu\n", static_cast<unsigned long long>(a.max_job));
    std::fprintf(f, "- last job_duration_ns: %llu\n", static_cast<unsigned long long>(a.last_job));
  }
  std::fprintf(f, "- last mode: %s\n", mode_name(a.last_mode));
  std::fprintf(f, "- last be_overrun_count: %u\n", a.last_be_overrun);
  std::fprintf(f, "- last drop_count: %u\n", a.last_drop);
  std::fprintf(f, "\n");
}

Host read_host() {
  Host h{};
  utsname u{};
  if (uname(&u) == 0) {
    snprintf(h.uname_r, sizeof(h.uname_r), "%s", u.release);
  }
  const long n = sysconf(_SC_NPROCESSORS_ONLN);
  h.nproc = n > 0 ? static_cast<int>(n) : 0;
  FILE* mounts = fopen("/proc/mounts", "r");
  if (mounts != nullptr) {
    char line[512];
    while (fgets(line, sizeof(line), mounts) != nullptr) {
      char dev[128];
      char mp[128];
      char type[64];
      if (sscanf(line, "%127s %127s %63s", dev, mp, type) == 3 && strcmp(type, "cgroup2") == 0) {
        h.cgroup2 = true;
        snprintf(h.cgroup2_mp, sizeof(h.cgroup2_mp), "%s", mp);
        break;
      }
    }
    fclose(mounts);
  }
  if (!read_text("/proc/sys/kernel/sched_rt_runtime_us", h.sched_rt_runtime_us, sizeof(h.sched_rt_runtime_us))) {
    snprintf(h.sched_rt_runtime_us, sizeof(h.sched_rt_runtime_us), "(unreadable)");
  }
  return h;
}

bool try_sched_fifo(pid_t pid, int prio, int* err_out) {
  sched_param sp{};
  sp.sched_priority = prio;
  if (sched_setscheduler(pid, SCHED_FIFO, &sp) == 0) {
    return true;
  }
  *err_out = errno;
  char prio_s[16];
  char pid_s[32];
  snprintf(prio_s, sizeof(prio_s), "%d", prio);
  snprintf(pid_s, sizeof(pid_s), "%d", static_cast<int>(pid));
  char* argv[] = {const_cast<char*>("sudo"), const_cast<char*>("-n"), const_cast<char*>("chrt"),
                  const_cast<char*>("-f"),   const_cast<char*>("-p"), prio_s,
                  pid_s,                     nullptr};
  if (run_argv(argv) == 0) {
    return true;
  }
  return false;
}

bool setup_cgroups(Knobs* k) {
  k->nproc = static_cast<int>(sysconf(_SC_NPROCESSORS_ONLN));
  if (k->nproc < 2) {
    knobs_note("cgroups not applied. nproc < 2; cannot make a disjoint exclusive RT CPU.");
    snprintf(k->kill_method, sizeof(k->kill_method), "SIGKILL not cgroup.kill (cgroups not applied)");
    return false;
  }
  if (access("/sys/fs/cgroup/cgroup.controllers", F_OK) != 0) {
    knobs_note("cgroups not applied. /sys/fs/cgroup/cgroup.controllers missing.");
    snprintf(k->kill_method, sizeof(k->kill_method), "SIGKILL not cgroup.kill (cgroups not applied)");
    return false;
  }

  if (!mkdir_p("/sys/fs/cgroup/spine", &k->used_sudo)) {
    knobs_note("cgroups not applied. mkdir /sys/fs/cgroup/spine failed.");
    snprintf(k->kill_method, sizeof(k->kill_method), "SIGKILL not cgroup.kill (cgroups not applied)");
    return false;
  }
  write_text("/sys/fs/cgroup/spine/cgroup.subtree_control", "+cpuset +cpu +memory +pids\n", &k->used_sudo);
  if (!mkdir_p("/sys/fs/cgroup/spine/rt", &k->used_sudo) || !mkdir_p("/sys/fs/cgroup/spine/be", &k->used_sudo)) {
    knobs_note("cgroups not applied. mkdir spine/rt or spine/be failed.");
    snprintf(k->kill_method, sizeof(k->kill_method), "SIGKILL not cgroup.kill (cgroups not applied)");
    return false;
  }

  k->rt_cpu = k->nproc - 1;
  snprintf(k->rt_cpus, sizeof(k->rt_cpus), "%d", k->rt_cpu);
  if (k->nproc == 2) {
    snprintf(k->be_cpus, sizeof(k->be_cpus), "0");
  } else {
    snprintf(k->be_cpus, sizeof(k->be_cpus), "0-%d", k->nproc - 2);
  }

  if (!write_text("/sys/fs/cgroup/spine/rt/cpuset.mems", "0\n", &k->used_sudo) ||
      !write_text("/sys/fs/cgroup/spine/be/cpuset.mems", "0\n", &k->used_sudo)) {
    knobs_note("cgroups not applied. cpuset.mems write failed.");
    snprintf(k->kill_method, sizeof(k->kill_method), "SIGKILL not cgroup.kill (cgroups not applied)");
    return false;
  }
  char rt_cpus_nl[32];
  char be_cpus_nl[80];
  snprintf(rt_cpus_nl, sizeof(rt_cpus_nl), "%s\n", k->rt_cpus);
  snprintf(be_cpus_nl, sizeof(be_cpus_nl), "%s\n", k->be_cpus);
  if (!write_text("/sys/fs/cgroup/spine/rt/cpuset.cpus", rt_cpus_nl, &k->used_sudo) ||
      !write_text("/sys/fs/cgroup/spine/be/cpuset.cpus", be_cpus_nl, &k->used_sudo)) {
    knobs_note("cgroups not applied. cpuset.cpus write failed.");
    snprintf(k->kill_method, sizeof(k->kill_method), "SIGKILL not cgroup.kill (cgroups not applied)");
    return false;
  }

  k->cgroups_applied = true;
  knobs_note("cgroups applied: leaves /sys/fs/cgroup/spine/rt and /sys/fs/cgroup/spine/be.");
  char line[256];
  snprintf(line, sizeof(line), "disjoint cpuset.cpus: rt=%s be=%s (no overlap).", k->rt_cpus, k->be_cpus);
  knobs_note(line);
  knobs_note("cpu.max on RT: not written.");
  knobs_note("sched_rt_runtime_us: not written.");
  knobs_note("no isolcpus, no irqaffinity, no PREEMPT_RT install.");
  snprintf(k->kill_method, sizeof(k->kill_method), "cgroup.kill on /sys/fs/cgroup/spine/be");
  return true;
}

bool move_pid(const char* procs_path, pid_t pid, bool* used_sudo) {
  char buf[32];
  snprintf(buf, sizeof(buf), "%d\n", static_cast<int>(pid));
  return write_text(procs_path, buf, used_sudo);
}

void cleanup_cgroups() {
  rmdir_priv("/sys/fs/cgroup/spine/rt");
  rmdir_priv("/sys/fs/cgroup/spine/be");
  rmdir_priv("/sys/fs/cgroup/spine");
}

pid_t spawn(const char* bin, char* const argv[]) {
  const pid_t pid = fork();
  if (pid < 0) {
    return -1;
  }
  if (pid == 0) {
    execv(bin, argv);
    _exit(127);
  }
  return pid;
}

std::uint32_t g_wait_be_overrun = 0;
std::uint32_t g_wait_seq = 0;
std::uint64_t g_wait_ps = 0;

bool pred_be_overrun(const Snapshot& s) { return s.o.be_overrun_count > g_wait_be_overrun; }
bool pred_stale(const Snapshot& s) {
  return s.o.mode == spine::Mode::kBeStale && s.o.mailbox_age > spine::harness::kStaleAgePeriods;
}
bool pred_seq(const Snapshot& s) { return s.o.mailbox_sequence > g_wait_seq && !s.o.held; }
bool pred_dead(const Snapshot& s) { return s.o.mode == spine::Mode::kBeDead && s.o.isolation_fault; }
bool pred_later_period(const Snapshot& s) { return s.o.period_start_ns != g_wait_ps; }

void write_log(FILE* f, const Host& host, const Knobs& knobs, const char* build_note, const char* argv0,
               const char* rt_bin, const char* be_bin, const PhaseAccum& idle, const PhaseAccum& sat,
               const Snapshot& over, const Snapshot& stale, const Snapshot& accepted, const Snapshot& killed,
               const Snapshot& after_kill, bool saw_after) {
  std::fprintf(f, "# Phase 0 proof run log\n\n");
  std::fprintf(f, "Status: measurement, not a stamp. Architect stamps any deadline claim.\n");
  std::fprintf(f, "This file is a record of one run. It does not prove isolation.\n");
  std::fprintf(f, "It does not claim a deadline was met.\n\n");

  std::fprintf(f, "## Host\n\n");
  std::fprintf(f, "- uname -r: %s\n", host.uname_r);
  std::fprintf(f, "- nproc: %d\n", host.nproc);
  if (host.cgroup2) {
    std::fprintf(f, "- cgroup v2 mounted: yes (%s)\n", host.cgroup2_mp);
  } else {
    std::fprintf(f, "- cgroup v2 mounted: no\n");
  }
  std::fprintf(f, "- sched_rt_runtime_us (read, not set): %s\n\n", host.sched_rt_runtime_us);

  std::fprintf(f, "## Knobs actually applied\n\n");
  if (!knobs.cgroups_applied) {
    std::fprintf(f, "cgroups not applied.\n");
  }
  std::fprintf(f, "%s", g_knobs_buf[0] != 0 ? g_knobs_buf : "(no knob notes)\n");
  if (knobs.used_sudo) {
    std::fprintf(f, "sudo -n was used for some cgroup file writes.\n");
  }
  if (knobs.fifo_set) {
    std::fprintf(f, "RT SCHED_FIFO priority %d was set.\n", spine::harness::kFifoPriority);
  } else {
    std::fprintf(f, "SCHED_FIFO was not set");
    if (knobs.fifo_errno != 0) {
      std::fprintf(f, " (errno %d: %s)", knobs.fifo_errno, strerror(knobs.fifo_errno));
    }
    std::fprintf(f, ".\n");
  }
  std::fprintf(f, "BE SCHED_OTHER: requested in the BE process (default class).\n");
  if (!knobs.cgroups_applied) {
    std::fprintf(f, "If be-saturate can schedule on the RT CPU, that is knobs-not-applied, not a harder load.\n");
  }
  std::fprintf(f, "Kill method: %s.\n\n", knobs.kill_method);

  std::fprintf(f, "## CONFIG (not claims)\n\n");
  std::fprintf(f, "- period_ns T = %llu\n", static_cast<unsigned long long>(spine::harness::kPeriodNs));
  std::fprintf(f, "- stale_age_periods N = %u\n", spine::harness::kStaleAgePeriods);
  std::fprintf(f, "- periods per load phase K = %u\n", spine::harness::kPeriodsPerLoad);
  std::fprintf(f, "- init_hold = %llu\n", static_cast<unsigned long long>(spine::harness::kInitHold));
  std::fprintf(f, "- first published command = %llu\n\n", static_cast<unsigned long long>(spine::harness::kPublishCmd));

  std::fprintf(f, "## Clock\n\n");
  std::fprintf(f, "- job-path measurement clock: CLOCK_MONOTONIC_RAW via clock_gettime (Runtime::step)\n");
  std::fprintf(f, "- period wait clock: CLOCK_MONOTONIC via clock_nanosleep (not the measurement clock)\n\n");

  std::fprintf(f, "## Build\n\n");
  std::fprintf(f, "- supervisor argv0: %s\n", argv0);
  std::fprintf(f, "- rt binary: %s\n", rt_bin);
  std::fprintf(f, "- be binary: %s\n", be_bin);
  if (build_note != nullptr && build_note[0] != 0) {
    std::fprintf(f, "- commands that actually ran:\n\n```\n%s\n```\n\n", build_note);
  } else {
    std::fprintf(f, "- SPINE_BUILD_NOTE was empty.\n\n");
  }

  std::fprintf(f, "Counts below are computed from the existing Observation record only.\n");
  std::fprintf(f, "last_command is Runtime::last_command, written by the RT process into the\n");
  std::fprintf(f, "RT-owned mapping (Observation + isolation_fault). BE does not map that mapping.\n\n");

  write_phase(f, "be-idle", idle);
  write_phase(f, "be-saturate", sat);

  std::fprintf(f, "## be-saturate rt_overrun fact\n\n");
  if (!sat.have_sample) {
    std::fprintf(f, "No be-saturate Observation samples; omitted.\n\n");
  } else if (sat.rt_overrun_true == 0) {
    std::fprintf(f,
                 "Every sampled be-saturate period had rt_overrun==false (%u of %u samples from the Observation "
                 "record). Record fact, not a deadline claim.\n\n",
                 sat.periods, sat.periods);
  } else {
    std::fprintf(f,
                 "%u of %u sampled be-saturate periods had rt_overrun==true. Record fact, not a deadline claim.\n\n",
                 sat.rt_overrun_true, sat.periods);
  }

  std::fprintf(f, "## inj-be-overrun\n\n");
  std::fprintf(f, "Observation snapshot after BE stopped publishing for at least one RT period.\n");
  write_snap(f, over);
  std::fprintf(f, "\n");

  std::fprintf(f, "## inj-stale-mailbox\n\n");
  std::fprintf(f, "Observation snapshot after mailbox_age > N.\n");
  write_snap(f, stale);
  std::fprintf(f, "\n");

  std::fprintf(f, "## accept after stale (before kill)\n\n");
  std::fprintf(f, "Observation snapshot after BE published one complete message and RT accepted it.\n");
  write_snap(f, accepted);
  std::fprintf(f, "\n");

  std::fprintf(f, "## inj-be-kill\n\n");
  std::fprintf(f, "Observation snapshot after kill + isolation_fault set.\n");
  write_snap(f, killed);
  std::fprintf(f, "\n");
  if (saw_after && after_kill.ok) {
    std::fprintf(f, "A later Observation.period_start_ns after that snapshot: %llu (RT still stepped).\n\n",
                 static_cast<unsigned long long>(after_kill.o.period_start_ns));
  } else {
    std::fprintf(f, "No later Observation.period_start_ns was observed after the kill snapshot.\n\n");
  }
}

}  // namespace

int main(int argc, char** argv) {
  if (argc != 4) {
    std::fprintf(stderr, "usage: phase0_run <rt_bin> <be_bin> <log_path>\n");
    return 2;
  }
  const char* rt_bin = argv[1];
  const char* be_bin = argv[2];
  const char* log_path = argv[3];
  const char* build_note = getenv("SPINE_BUILD_NOTE");
  if (build_note == nullptr) {
    build_note = "";
  }

  const Host host = read_host();
  Knobs knobs{};
  setup_cgroups(&knobs);

  char stg_name[64];
  char rt_name[64];
  snprintf(stg_name, sizeof(stg_name), "/spine0s%d", static_cast<int>(getpid()));
  snprintf(rt_name, sizeof(rt_name), "/spine0r%d", static_cast<int>(getpid()));

  auto* staging = static_cast<spine::harness::StagingMap*>(
      create_shm(stg_name, sizeof(spine::harness::StagingMap)));
  auto* vis = static_cast<spine::harness::RtVisible*>(create_shm(rt_name, sizeof(spine::harness::RtVisible)));
  if (staging == nullptr || vis == nullptr) {
    std::fprintf(stderr, "phase0_run: shm create failed\n");
    cleanup_cgroups();
    return 1;
  }

  char* rt_argv[] = {const_cast<char*>(rt_bin), stg_name, rt_name, nullptr};
  char* be_argv[] = {const_cast<char*>(be_bin), stg_name, nullptr};
  const pid_t rt_pid = spawn(rt_bin, rt_argv);
  const pid_t be_pid = spawn(be_bin, be_argv);
  if (rt_pid < 0 || be_pid < 0) {
    std::fprintf(stderr, "phase0_run: fork/exec failed\n");
    if (rt_pid > 0) {
      kill(rt_pid, SIGKILL);
    }
    if (be_pid > 0) {
      kill(be_pid, SIGKILL);
    }
    cleanup_cgroups();
    return 1;
  }

  if (knobs.cgroups_applied) {
    if (!move_pid("/sys/fs/cgroup/spine/rt/cgroup.procs", rt_pid, &knobs.used_sudo)) {
      knobs_note("cgroups not applied. moving RT pid into spine/rt failed.");
      knobs.cgroups_applied = false;
      snprintf(knobs.kill_method, sizeof(knobs.kill_method), "SIGKILL not cgroup.kill (cgroups not applied)");
    } else if (!move_pid("/sys/fs/cgroup/spine/be/cgroup.procs", be_pid, &knobs.used_sudo)) {
      knobs_note("cgroups not applied. moving BE pid into spine/be failed.");
      knobs.cgroups_applied = false;
      snprintf(knobs.kill_method, sizeof(knobs.kill_method), "SIGKILL not cgroup.kill (cgroups not applied)");
    } else {
      knobs_note("RT pid moved to spine/rt. BE pid moved to spine/be. Supervisor left in its original cgroup.");
    }
  }

  knobs.fifo_set = try_sched_fifo(rt_pid, spine::harness::kFifoPriority, &knobs.fifo_errno);

  const std::uint64_t phase_timeout =
      static_cast<std::uint64_t>(spine::harness::kPeriodsPerLoad) * spine::harness::kPeriodNs * 4ull + 2000000000ull;

  Snapshot ready{};
  g_wait_ps = 0;
  if (!wait_pred(vis, 2000000000ull, &ready, pred_later_period)) {
    knobs_note("RT did not write a non-zero Observation.period_start_ns within 2s.");
  }

  std::uint64_t last_ps = ready.ok ? ready.o.period_start_ns : 0;
  PhaseAccum idle{};
  const bool idle_ok = collect_k(vis, &idle, spine::harness::kPeriodsPerLoad, phase_timeout, &last_ps);
  if (!idle_ok) {
    knobs_note("be-idle ended on timeout before K Observation periods.");
  }

  kill(be_pid, SIGUSR1);  // idle -> saturate
  PhaseAccum sat{};
  const bool sat_ok = collect_k(vis, &sat, spine::harness::kPeriodsPerLoad, phase_timeout, &last_ps);
  if (!sat_ok) {
    knobs_note("be-saturate ended on timeout before K Observation periods.");
  }

  kill(be_pid, SIGUSR1);  // saturate -> silent
  Snapshot over{};
  g_wait_be_overrun = sat.have_sample ? sat.last_be_overrun : (idle.have_sample ? idle.last_be_overrun : 0);
  if (!wait_pred(vis, 2000000000ull, &over, pred_be_overrun)) {
    knobs_note("inj-be-overrun: be_overrun_count did not increment in time.");
  }

  Snapshot stale{};
  if (!wait_pred(vis, static_cast<std::uint64_t>(spine::harness::kStaleAgePeriods + 8u) * spine::harness::kPeriodNs,
                 &stale, pred_stale)) {
    knobs_note("inj-stale-mailbox: mode be-stale with age > N not observed in time.");
  }

  g_wait_seq = stale.ok ? stale.o.mailbox_sequence : (over.ok ? over.o.mailbox_sequence : 0);
  kill(be_pid, SIGUSR1);  // silent -> publish one
  Snapshot accepted{};
  if (!wait_pred(vis, 2000000000ull, &accepted, pred_seq)) {
    knobs_note("post-stale publish: sequence did not advance in time.");
  }

  vis->isolation_fault.store(1u, std::memory_order_release);
  if (knobs.cgroups_applied && access("/sys/fs/cgroup/spine/be/cgroup.kill", W_OK) == 0) {
    if (!write_text("/sys/fs/cgroup/spine/be/cgroup.kill", "1\n", &knobs.used_sudo)) {
      kill(be_pid, SIGKILL);
      snprintf(knobs.kill_method, sizeof(knobs.kill_method),
               "SIGKILL not cgroup.kill (cgroup.kill write failed after cgroups were set up)");
      knobs_note("cgroup.kill write failed; used SIGKILL.");
    }
  } else if (knobs.cgroups_applied) {
    if (!write_text("/sys/fs/cgroup/spine/be/cgroup.kill", "1\n", &knobs.used_sudo)) {
      kill(be_pid, SIGKILL);
      snprintf(knobs.kill_method, sizeof(knobs.kill_method),
               "SIGKILL not cgroup.kill (cgroup.kill not writable)");
      knobs_note("cgroup.kill not writable; used SIGKILL.");
    }
  } else {
    kill(be_pid, SIGKILL);
  }

  Snapshot killed{};
  if (!wait_pred(vis, 2000000000ull, &killed, pred_dead)) {
    knobs_note("inj-be-kill: mode be-dead with isolation_fault was not observed in time.");
  }

  Snapshot after{};
  bool saw_after = false;
  if (killed.ok) {
    g_wait_ps = killed.o.period_start_ns;
    saw_after = wait_pred(vis, 2000000000ull, &after, pred_later_period);
  }

  FILE* log = fopen(log_path, "w");
  if (log == nullptr) {
    std::fprintf(stderr, "phase0_run: cannot write %s\n", log_path);
    kill(rt_pid, SIGTERM);
    waitpid(rt_pid, nullptr, 0);
    waitpid(be_pid, nullptr, WNOHANG);
    shm_unlink(stg_name);
    shm_unlink(rt_name);
    cleanup_cgroups();
    return 1;
  }
  write_log(log, host, knobs, build_note, argv[0], rt_bin, be_bin, idle, sat, over, stale, accepted, killed, after,
            saw_after);
  fclose(log);

  kill(rt_pid, SIGTERM);
  waitpid(rt_pid, nullptr, 0);
  waitpid(be_pid, nullptr, WNOHANG);
  munmap(staging, sizeof(*staging));
  munmap(vis, sizeof(*vis));
  shm_unlink(stg_name);
  shm_unlink(rt_name);
  cleanup_cgroups();
  return 0;
}
