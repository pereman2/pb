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
#include <sys/mman.h>
#include <asm/unistd.h>

#define PROFILE_ASSERT(x) if (!(x)) { std::cerr << "Assertion failed " << #x << std::endl; exit(EXIT_FAILURE); }

#define PROFILE_MAX_THREADS 8192
#define PROFILE_MAX_ANCHORS 4096

struct pb_profile_anchor {
  const char* name;
  std::atomic_uint64_t elapsed[PROFILE_MAX_THREADS]; // 1 per thread, atomic because there could be multiple threads mapping to same bin hash function is not perfect
  std::atomic_uint64_t hits[PROFILE_MAX_THREADS];
  std::atomic_uint64_t cpu_migrations[PROFILE_MAX_THREADS];
  std::atomic_uint64_t cache_misses[PROFILE_MAX_THREADS];
  std::atomic_uint64_t branch_misses[PROFILE_MAX_THREADS];
};

enum pb_perf_event_type {
  PB_PERF_CACHE_MISSES = 0,
  PB_PERF_CACHE_REFERENCES = 1,
  PB_PERF_INSTRUCTIONS = 2,
  PB_PERF_CYCLES = 3,
  PB_PERF_PAGE_FAULTS = 4,
  PB_PERF_BRANCH_MISS = 5,
};

struct pb_profile_perf_event {
  int initailized;
  int fd;
  perf_event_mmap_page* mmap;
  uint64_t prev_value;
};

static thread_local pb_profile_perf_event pb_profile_perf_events[1024] = {{0}};

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


static inline uint64_t pb_perf_event_read(pb_perf_event_type type) {
  int index = type;
  perf_event_mmap_page* buf = pb_profile_perf_events[index].mmap;
  PROFILE_ASSERT(buf != NULL);
  uint32_t seq;
  uint64_t count;
  uint64_t offset;

  do {
		seq = buf->lock;
		// rmb();
		index = buf->index;
		offset = buf->offset;
		if (index == 0) { /* rdpmc not allowed */
			count = 0;
			break;
		}
		count = _rdpmc(index - 1);
		// rmb();
  } while (buf->lock != seq);

	return (count + offset) & 0xffffffffffff;
  return count;
}

inline void pb_perf_event_open(pb_perf_event_type type) {
  int index = type;
  if (pb_profile_perf_events[index].initailized == 0) {
    perf_event_attr attr;
    memset(&attr, 0, sizeof(perf_event_attr));
    attr.size = sizeof(perf_event_attr);
    attr.disabled = 1;
    attr.exclude_kernel = 1;
    attr.exclude_hv = 1;
    attr.mmap = 1;
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
      case PB_PERF_BRANCH_MISS:
        attr.type = PERF_TYPE_HARDWARE;
        attr.config = PERF_COUNT_HW_BRANCH_MISSES;
        break;
      default:
        std::cerr << "Invalid perf event type" << std::endl;
        exit(EXIT_FAILURE);
    }
    pb_profile_perf_events[index].fd = syscall(__NR_perf_event_open, &attr, 0, -1, -1, 0);
    if (pb_profile_perf_events[index].fd == -1) {
      std::cerr << "Error opening leader " << attr.config << std::endl;
      exit(EXIT_FAILURE);
    }
    pb_profile_perf_events[index].mmap = (perf_event_mmap_page*)mmap(NULL, 4096, PROT_READ | PROT_WRITE, MAP_SHARED, pb_profile_perf_events[index].fd, 0);
    if (pb_profile_perf_events[index].mmap == MAP_FAILED) {
      std::cerr << "Error mapping file" << std::endl;
      exit(EXIT_FAILURE);
    }
    ioctl(pb_profile_perf_events[index].fd, PERF_EVENT_IOC_RESET, 0);
    ioctl(pb_profile_perf_events[index].fd, PERF_EVENT_IOC_ENABLE);
    pb_profile_perf_events[index].initailized = 1;
  }
} 

enum PbProfileFlags {
  PB_PROFILE_CACHE = 1,
  PB_PROFILE_PAGE_FAULTS = 2,
  PB_PROFILE_INSTRUCTIONS = 4,
  PB_PROFILE_CYCLES = 8,
  PB_PROFILE_BRANCH = 16,
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
      uint64_t prev = pb_perf_event_read(PB_PERF_CACHE_MISSES);
      if (prev > UINT64_MAX / 2) {
        prev = 0;
        ioctl(pb_profile_perf_events[PB_PERF_CACHE_MISSES].fd, PERF_EVENT_IOC_RESET, 0);
      }
      pb_profile_perf_events[PB_PERF_CACHE_MISSES].prev_value = prev;
    }

    if (flags & PB_PROFILE_BRANCH) {
      pb_perf_event_open(PB_PERF_BRANCH_MISS);
      uint64_t prev = pb_perf_event_read(PB_PERF_BRANCH_MISS);
      if (prev > UINT64_MAX / 2) {
        printf("too bigg\n");
        prev = 0;
        ioctl(pb_profile_perf_events[PB_PERF_BRANCH_MISS].fd, PERF_EVENT_IOC_RESET, 0);
      }
      pb_profile_perf_events[PB_PERF_BRANCH_MISS].prev_value = prev;
    }
    
  }

  inline uint64_t hash_thread_id() {
    uint64_t thread_id = pthread_self();
    for (uint64_t i = 0; i < sizeof(thread_id); i++) {
      thread_id = (thread_id << 7) ^ (thread_id >> 3);
    }
    return thread_id % PROFILE_MAX_THREADS;
  }


  ~PbProfile() {
    uint32_t end_processor_id;
    uint64_t elapsed = __rdtscp(&end_processor_id) - start;
    uint64_t thread_id = hash_thread_id();

    if (flags & PB_PROFILE_CACHE) {
      // TODO(pere): deal with overflow
      uint64_t count = pb_perf_event_read(PB_PERF_CACHE_MISSES);
      count -= pb_profile_perf_events[PB_PERF_CACHE_MISSES].prev_value;
      g_profiler_get().anchors[index].cache_misses[thread_id] += count;
    }
    if (flags & PB_PROFILE_BRANCH) {
      uint64_t count = pb_perf_event_read(PB_PERF_BRANCH_MISS);
      count -= pb_profile_perf_events[PB_PERF_BRANCH_MISS].prev_value;
      g_profiler_get().anchors[index].branch_misses[thread_id] += count;
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
    uint64_t sum_branch_misses = 0;
    for (uint64_t j = 0; j < PROFILE_MAX_THREADS; j++) {
      sum_elapsed += g_profiler_get().anchors[i].elapsed[j];
      sum_hits += g_profiler_get().anchors[i].hits[j];
      sum_migrations += g_profiler_get().anchors[i].cpu_migrations[j];
      uint64_t count = g_profiler_get().anchors[i].cache_misses[j];
      if (count > 0)
        printf("count thread %lu: %lu\n", j, count);
      sum_cache_misses += g_profiler_get().anchors[i].cache_misses[j].load();
      sum_branch_misses += g_profiler_get().anchors[i].branch_misses[j];
    }
    uint64_t avg_elapsed = 0;
    if (sum_hits > 0) {
      avg_elapsed = sum_elapsed / sum_hits;
    }
    pb_profile_file_get() << g_profiler_get().anchors[i].name << " cycles " << sum_elapsed 
      << " hits " << sum_hits << " avg_elapsed " << avg_elapsed 
      << " cpu_migrations " << sum_migrations 
      << " cache_misses " << sum_cache_misses 
      << " branch_misses " << sum_branch_misses 
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
    memset(&g_profiler_get().anchors, 0, sizeof(pb_profile_anchor) * PROFILE_MAX_ANCHORS);
    pb_init_log_file(filename); }
  ~PbProfilerStart() {
    pb_close_log_file();
  }
};

static PbProfilerStart pb_profiler_start("profile.log");