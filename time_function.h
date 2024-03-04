#pragma once

#include <asm/unistd.h>
#include <bits/types/FILE.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <fcntl.h>
#include <linux/perf_event.h>
#include <linux/hw_breakpoint.h>
#include <pthread.h>
#include <sys/ioctl.h>
#include <unistd.h>
#include <x86intrin.h>
#include <sys/mman.h>
#include <asm/unistd.h>

#define PROFILE_ASSERT(x) if (!(x)) { printf("assert failed %s %d\n", __FILE__, __LINE__); exit(EXIT_FAILURE); }

#define PROFILE_TO_STDOUT 1
#define PROFILE_MAX_THREADS 8192
#define PROFILE_MAX_ANCHORS 4096

#ifdef __cplusplus
#include <atomic>
typedef std::atomic<uint64_t> atomic_uint64_t;
#else
#include <stdatomic.h>
typedef _Atomic uint64_t atomic_uint64_t;
#endif

struct pb_profile_anchor {
  const char* name;
  atomic_uint64_t elapsed[PROFILE_MAX_THREADS]; // 1 per thread, atomic because there could be multiple threads mapping to same bin hash function is not perfect
  atomic_uint64_t hits[PROFILE_MAX_THREADS];
  atomic_uint64_t cpu_migrations[PROFILE_MAX_THREADS];
  atomic_uint64_t cache_misses[PROFILE_MAX_THREADS];
  atomic_uint64_t branch_misses[PROFILE_MAX_THREADS];
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
inline pthread_mutex_t& pb_file_mutex_get() {
  static pthread_mutex_t pb_file_mutex;
  return pb_file_mutex;
}
inline FILE** pb_profile_file_get() {
  static FILE* pb_profile_file = NULL;
  return &pb_profile_file;
};

inline pthread_t* pb_profile_thread_get() {
  static pthread_t pb_profile_thread = 0;
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
        printf("Error: unknown perf event type %d\n", type);
        exit(EXIT_FAILURE);
    }
    pb_profile_perf_events[index].fd = syscall(__NR_perf_event_open, &attr, 0, -1, -1, 0);
    if (pb_profile_perf_events[index].fd == -1) {
      printf("Error: perf_event_open failed for type %d\n", type);
      exit(EXIT_FAILURE);
    }
    pb_profile_perf_events[index].mmap = (perf_event_mmap_page*)mmap(NULL, 4096, PROT_READ | PROT_WRITE, MAP_SHARED, pb_profile_perf_events[index].fd, 0);
    if (pb_profile_perf_events[index].mmap == MAP_FAILED) {
      printf("Error: mmap failed for type %d\n", type);
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
  PROFILE_ASSERT(*pb_profile_file_get() != NULL);
  uint64_t total_elapsed = __rdtsc() - g_profiler_get().start;
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
      sum_cache_misses += g_profiler_get().anchors[i].cache_misses[j];
      sum_branch_misses += g_profiler_get().anchors[i].branch_misses[j];
    }
    uint64_t avg_elapsed = 0;
    if (sum_hits > 0) {
      avg_elapsed = sum_elapsed / sum_hits;
    }
    fprintf(*pb_profile_file_get(), "total_elapsed %lu\n", total_elapsed);
    fprintf(*pb_profile_file_get(), "%s: ", g_profiler_get().anchors[i].name);
    fprintf(*pb_profile_file_get(), "cycles=%lu ", sum_elapsed);
    fprintf(*pb_profile_file_get(), "hits=%lu ", sum_hits);
    fprintf(*pb_profile_file_get(), "cpu_migrations=%lu ", sum_migrations);
    fprintf(*pb_profile_file_get(), "cache_misses=%lu ", sum_cache_misses);
    fprintf(*pb_profile_file_get(), "branch_misses=%lu ", sum_branch_misses);
    fprintf(*pb_profile_file_get(), "\n");
#if PROFILE_TO_STDOUT == 1
    printf("%s: ", g_profiler_get().anchors[i].name);
    printf("cycles=%lu ", sum_elapsed);
    printf("hits=%lu ", sum_hits);
    printf("cpu_migrations=%lu ", sum_migrations);
    printf("cache_misses=%lu ", sum_cache_misses);
    printf("branch_misses=%lu ", sum_branch_misses);
    printf("\n");
#endif

  }
}

static void* profile_thread_entry(void* ctx) {
  while (pb_is_profiling_get()) {
    sleep(1);
    printf("profiling\n");
    print_profiling();
  }
  return NULL;
}


#define NameConcat2(A, B) A##B
#define NameConcat(A, B) NameConcat2(A, B)
#define PbProfileFunction(variable, label) PbProfile variable((const char*)label, (uint64_t)(__COUNTER__ + 1))
#define PbProfileFunctionF(variable, label, flags) PbProfile variable((const char*)label, (uint64_t)(__COUNTER__ + 1), flags)


static void pb_init_log_file(const char* filename) {
  g_profiler_get().start = __rdtsc();
  pb_is_profiling_get() = true;
  if (*pb_profile_file_get() == NULL) {
    (*pb_profile_file_get()) = fopen(filename, "w");
    if (pb_profile_file_get() == NULL) {
      printf("Error: fopen() failed log file %s\n", filename);
      exit(EXIT_FAILURE);
    }
  }
  int ret = pthread_create(pb_profile_thread_get(), NULL, profile_thread_entry, NULL);
  if (ret != 0) {
    printf("Error: pthread_create() failed\n");
    exit(EXIT_FAILURE);
  }
}

// Static method to close the log profile_file
static void pb_close_log_file() {
  pb_is_profiling_get() = false;
  pthread_join(*pb_profile_thread_get(), NULL);
  print_profiling();
  fclose(*pb_profile_file_get());
  *pb_profile_file_get() = NULL;
}


class PbProfilerStart {
  public:
  PbProfilerStart(const char* filename) {
    memset(&g_profiler_get(), 0, sizeof(pb_profiler));
    memset(&g_profiler_get().anchors, 0, sizeof(pb_profile_anchor) * PROFILE_MAX_ANCHORS);
    pb_init_log_file(filename); }
  ~PbProfilerStart() {
    pb_close_log_file();
  }
};

static PbProfilerStart pb_profiler_start("profile.log");
