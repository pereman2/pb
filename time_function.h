#pragma once

#include <atomic>
#include <asm/unistd.h>
#include <cstdint>
#include <cstring>
#include <fcntl.h>
#include <iostream>
#include <linux/perf_event.h>
#include <linux/hw_breakpoint.h>
#include <map>
#include <mutex>
#include <pthread.h>
#include <string>
#include <sys/ioctl.h>
#include <thread>
#include <vector>
#include <x86intrin.h>
#include <fstream>

#define PROFILE_MAX_THREADS 8192
#define PROFILE_MAX_ANCHORS 4096

struct pb_profile_anchor {
  const char* name;
  std::atomic_uint64_t elapsed[PROFILE_MAX_THREADS]; // 1 per thread, atomic because there could be multiple threads mapping to same bin hash function is not perfect
  std::atomic_uint64_t hits[PROFILE_MAX_THREADS];
  std::atomic_uint64_t cpu_migrations[PROFILE_MAX_THREADS];
  std::atomic_uint64_t cache_misses[PROFILE_MAX_THREADS];
  std::atomic_uint64_t page_faults[PROFILE_MAX_THREADS];
};

enum pb_perf_event_type {
  PB_PERF_CACHE_MISSES = 0,
  PB_PERF_CACHE_REFERENCES = 1,
  PB_PERF_INSTRUCTIONS = 2,
  PB_PERF_CYCLES = 3,
  PB_PERF_PAGE_FAULTS = 4,
};

static thread_local int pb_profile_perf_event_fds[1024] = {-1};

struct pb_profiler {
  pb_profile_anchor anchors[PROFILE_MAX_ANCHORS];
  uint64_t anchor_count;
  uint64_t start;
  uint64_t total_elapsed;
};

/* Globals */
inline std::mutex& pb_file_mutex_get() {
  static std::mutex pb_file_mutex;
  return pb_file_mutex;
}
inline std::ofstream& pb_profile_file_get() {
  static std::ofstream pb_profile_file;
  return pb_profile_file;
};

inline std::thread** pb_profile_thread_get() {
  static std::thread* pb_profile_thread = nullptr;
  return &pb_profile_thread;
};

inline bool& pb_is_profiling_get() {
  static bool pb_is_profiling = false;
  return pb_is_profiling;
};

inline pb_profiler& g_profiler_get() {
  static pb_profiler g_profiler;
  return g_profiler;
};

struct read_format {
  uint64_t value;         /* The value of the event */
  uint64_t time_enabled;  /* if PERF_FORMAT_TOTAL_TIME_ENABLED */
  uint64_t time_running;  /* if PERF_FORMAT_TOTAL_TIME_RUNNING */
  uint64_t id;            /* if PERF_FORMAT_ID */
  uint64_t lost;          /* if PERF_FORMAT_LOST */
};
/* End globals */

inline void pb_perf_event_open(int type) {
  int index = type;
  if (pb_profile_perf_event_fds[index] == -1) {
    perf_event_attr attr;
    memset(&attr, 0, sizeof(perf_event_attr));
    attr.size = sizeof(perf_event_attr);
    attr.disabled = 1;
    attr.exclude_kernel = 1;
    attr.exclude_hv = 1;
    switch (type) {
      case PB_PERF_CACHE_MISSES:
        attr.type = PERF_TYPE_HARDWARE;
        attr.config = PERF_COUNT_HW_CACHE_MISSES;
        break;
      case PB_PERF_CACHE_REFERENCES:
        attr.type = PERF_TYPE_HARDWARE;
        attr.config = PERF_COUNT_HW_CACHE_REFERENCES;
        break;
      case PB_PERF_INSTRUCTIONS:
        attr.type = PERF_TYPE_HARDWARE;
        attr.config = PERF_COUNT_HW_INSTRUCTIONS;
        break;
      case PB_PERF_CYCLES:
        attr.type = PERF_TYPE_HARDWARE;
        attr.config = PERF_COUNT_HW_CPU_CYCLES;
        break;
      case PB_PERF_PAGE_FAULTS:
        attr.type = PERF_TYPE_SOFTWARE;
        attr.config = PERF_COUNT_SW_PAGE_FAULTS;
        break;
      default:
        std::cerr << "Invalid perf event type" << std::endl;
        exit(EXIT_FAILURE);
    }
    pb_profile_perf_event_fds[index] = syscall(__NR_perf_event_open, &attr, 0, -1, -1, 0);
    if (pb_profile_perf_event_fds[index] == -1) {
      std::cerr << "Error opening leader " << attr.config << std::endl;
      exit(EXIT_FAILURE);
    }
  }
} 

enum PbProfileFlags {
  PB_PROFILE_CACHE = 1,
  PB_PROFILE_PAGE_FAULTS = 2,
  PB_PROFILE_INSTRUCTIONS = 4,
  PB_PROFILE_CYCLES = 8,
  PB_PROFILE_ALL = 15,
};

class PbProfile {
  public:
  uint64_t start;
  const char* function;
  uint64_t index;
  uint32_t processor_id;
  uint64_t flags;
  PbProfile(const char* function, uint64_t index, uint64_t flags = 0) {
    g_profiler_get().anchors[index].name = function;
    this->function = function;
    this->index = index;
    this->flags = flags;
    start = __rdtscp(&processor_id);
    

    if (flags & PB_PROFILE_CACHE) {
      pb_perf_event_open(PB_PERF_CACHE_MISSES);
      ioctl(pb_profile_perf_event_fds[PB_PERF_CACHE_MISSES], PERF_EVENT_IOC_RESET, 0);
      ioctl(pb_profile_perf_event_fds[PB_PERF_CACHE_MISSES], PERF_EVENT_IOC_ENABLE);
    }

    if (flags & PB_PROFILE_PAGE_FAULTS) {
      pb_perf_event_open(PB_PERF_PAGE_FAULTS);
      ioctl(pb_profile_perf_event_fds[PB_PERF_PAGE_FAULTS], PERF_EVENT_IOC_RESET, 0);
      ioctl(pb_profile_perf_event_fds[PB_PERF_PAGE_FAULTS], PERF_EVENT_IOC_ENABLE);
    }
    
  }

  ~PbProfile() {
    uint32_t end_processor_id;
    uint64_t elapsed = __rdtscp(&end_processor_id) - start;
    uint64_t thread_id = pthread_self() % 8192; // seems like a good number

    read_format perf_read;
    if (flags & PB_PROFILE_CACHE) {
      read(pb_profile_perf_event_fds[PB_PERF_CACHE_MISSES], &perf_read, sizeof(read_format));
      g_profiler_get().anchors[index].cache_misses[thread_id] += perf_read.value;
    }
    if (flags & PB_PROFILE_PAGE_FAULTS) {
      read(pb_profile_perf_event_fds[PB_PERF_PAGE_FAULTS], &perf_read, sizeof(read_format));
      g_profiler_get().anchors[index].page_faults[thread_id] += perf_read.value;
    }

    if (processor_id != end_processor_id) {
      g_profiler_get().anchors[index].cpu_migrations[thread_id]++;
      return;
    }
    if (elapsed > 0 && g_profiler_get().anchors[index].elapsed[thread_id] > UINT64_MAX - elapsed) {
      g_profiler_get().anchors[index].elapsed[thread_id] = elapsed;
      // {
      //   std::unique_lock<std::mutex> lock(pb_file_mutex_get());
      //   pb_profile_file_get() << g_profiler_get().anchors[index].name << " cycles " << g_profiler_get().anchors[index].elapsed << " hits " << g_profiler_get().anchors[index].hits << std::endl;
      // }
    } else {
      g_profiler_get().anchors[index].elapsed[thread_id] += elapsed;
      g_profiler_get().anchors[index].hits[thread_id]++;
    }
  }
};

static void print_profiling() {
  uint64_t total_elapsed = __rdtsc() - g_profiler_get().start;
  pb_profile_file_get() << "total_elapsed " << total_elapsed << std::endl;
  for (uint64_t i = 0; i < PROFILE_MAX_ANCHORS; i++) {
    if (g_profiler_get().anchors[i].name == nullptr) {
      continue;
    }
    uint64_t sum_elapsed = 0;
    uint64_t sum_hits = 0;
    uint64_t sum_migrations = 0;
    uint64_t sum_cache_misses = 0;
    uint64_t sum_page_faults = 0;
    for (uint64_t j = 0; j < PROFILE_MAX_THREADS; j++) {
      sum_elapsed += g_profiler_get().anchors[i].elapsed[j];
      sum_hits += g_profiler_get().anchors[i].hits[j];
      sum_migrations += g_profiler_get().anchors[i].cpu_migrations[j];
      sum_cache_misses += g_profiler_get().anchors[i].cache_misses[j];
      sum_page_faults += g_profiler_get().anchors[i].page_faults[j];
    }
    uint64_t avg_elapsed = 0;
    if (sum_hits > 0) {
      avg_elapsed = sum_elapsed / sum_hits;
    }
    pb_profile_file_get() << g_profiler_get().anchors[i].name << " cycles " << sum_elapsed 
      << " hits " << sum_hits << " avg_elapsed " << avg_elapsed 
      << " cpu_migrations " << sum_migrations 
      << " cache_misses " << sum_cache_misses 
      << " page_faults " << sum_page_faults 
      << std::endl;

  }
}

static void profile_thread_entry() {
  while (pb_is_profiling_get()) {
    std::this_thread::sleep_for(std::chrono::seconds(1));
    std::cout << "profiling" << std::endl;
    print_profiling();
  }
}


#define NameConcat2(A, B) A##B
#define NameConcat(A, B) NameConcat2(A, B)
#define PbProfileFunction(variable, label) PbProfile variable((const char*)label, (uint64_t)(__COUNTER__ + 1))
#define PbProfileFunctionF(variable, label, flags) PbProfile variable((const char*)label, (uint64_t)(__COUNTER__ + 1), flags)


static void pb_init_log_file(const std::string& filename) {
  g_profiler_get().start = __rdtsc();
  pb_is_profiling_get() = true;
  pb_profile_file_get().open(filename, std::ios::out | std::ios::app); // Open in append mode
  if (!pb_profile_file_get().is_open()) {
    throw std::runtime_error("Failed to open log profile_file: " + filename);
  }
  *pb_profile_thread_get() = new std::thread(profile_thread_entry);
}

// Static method to close the log profile_file
static void pb_close_log_file() {
  pb_is_profiling_get() = false;
  (*pb_profile_thread_get())->join();
  print_profiling();
  if (pb_profile_file_get().is_open()) {
    pb_profile_file_get().close();
  }
}


class PbProfilerStart {
  public:
  PbProfilerStart(const std::string& filename) {
    memset(&g_profiler_get(), 0, sizeof(pb_profiler));
    pb_init_log_file(filename); }
  ~PbProfilerStart() {
    pb_close_log_file();
  }
};

static PbProfilerStart pb_profiler_start("profile.log");
