#pragma once


#include <asm-generic/errno-base.h>
#include <asm/unistd.h>
#include <bits/types/FILE.h>
#include <cstring>
#include <stdio.h>
#include <fcntl.h>
#include <pthread.h>
#include <sys/ioctl.h>
#include <unistd.h>
#include <sys/mman.h>
#include <asm/unistd.h>
#include <x86intrin.h>
#ifdef __cplusplus
#include <atomic>
    typedef std::atomic<uint64_t> atomic_uint64_t;
#else
#include <stdatomic.h>
    typedef _Atomic uint64_t atomic_uint64_t;
#endif


namespace pb_profiler {
#include <linux/perf_event.h>
#include <linux/hw_breakpoint.h>
#define PROFILE_ASSERT(x) if (!(x)) { printf("assert failed %s %d\n", __FILE__, __LINE__); exit(EXIT_FAILURE); }

#define PROFILE_TO_STDOUT 1
#define PROFILE_MAX_THREADS 64
#define PROFILE_MAX_ANCHORS 128


    struct ArenaRegion {
        void* start;
        void* end;
        void* current;
        ArenaRegion* next;
    };

    struct Arena {
        ArenaRegion* regions;
        ArenaRegion* current_region;
        bool growable;
    };


    enum pb_profile_anchor_result_type {
        PB_PROFILE_ANCHOR_CYCLES = 0,
        PB_PROFILE_ANCHOR_HITS = 1,
        PB_PROFILE_ANCHOR_CPU_MIGRATIONS = 2,
        PB_PROFILE_ANCHOR_CACHE_MISSES = 3,
        PB_PROFILE_ANCHOR_BRANCH_MISSES = 4,
        PB_PROFILE_ANCHOR_LAST = 5,
    };

    struct pb_profile_anchor_result {
        pb_profile_anchor_result_type type;
        uint64_t value;
    };

    struct pb_profile_flush_header {
        uint64_t thread_id;
        uint64_t name_length;
        uint64_t result_amount;
    };

    struct pb_profile_anchor {
        const char* name;
        pthread_mutex_t mutex[PROFILE_MAX_THREADS];
        Arena* anchor_results_arena[PROFILE_MAX_THREADS];
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
    };

    static thread_local pb_profile_perf_event pb_profile_perf_events[1024] = {{0}};

    struct pb_profiler_t {
        pb_profile_anchor* anchors;
        uint64_t anchor_count;
        uint64_t start;
        uint64_t total_elapsed;
        bool profiling = false;
        FILE* pb_profile_file;
        pthread_mutex_t pb_file_mutex;
        pthread_t pb_profile_thread;
    };

    extern pb_profiler_t g_profiler;
    extern pb_profiler_t g_profiler_get();

    struct read_format {
        uint64_t value;         /* The value of the event */
        uint64_t time_enabled;  /* if PERF_FORMAT_TOTAL_TIME_ENABLED */
        uint64_t time_running;  /* if PERF_FORMAT_TOTAL_TIME_RUNNING */
        uint64_t id;            /* if PERF_FORMAT_ID */
        uint64_t lost;          /* if PERF_FORMAT_LOST */
    };
    /* End globals */

    static inline uint64_t arena_region_size(ArenaRegion* region) {
        return (uintptr_t)region->end - (uintptr_t)region->start;
    }
    static inline ArenaRegion* arena_region_create(size_t size) {
        void* start = mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
        if (start == MAP_FAILED) {
            return NULL;
        }
        ArenaRegion* arena_region = (ArenaRegion*)malloc(sizeof(ArenaRegion));
        arena_region->start = start;
        arena_region->end = (void*)((uintptr_t)start + size);
        arena_region->current = start;
        arena_region->next = NULL;
        return arena_region;
    }

    static inline void arena_region_destroy(ArenaRegion* arena_region) {
        munmap(arena_region->start, (uintptr_t)arena_region->end - (uintptr_t)arena_region->start);
        free(arena_region);
    }

    static inline Arena* arena_create(size_t size, bool growable) {
        void* start = mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
        if (start == MAP_FAILED) {
            return NULL;
        }
        ArenaRegion* arena_region = arena_region_create(size);
        Arena* arena = (Arena*)malloc(sizeof(Arena));

        arena->growable = growable;
        arena->regions = arena_region;
        arena->current_region = arena_region;
        return arena;
    }

    static inline void* arena_alloc(Arena* arena, size_t size) {
        uint64_t possible_end = (uintptr_t)arena->current_region->current + size;
        if (possible_end > (uintptr_t)arena->current_region->end) {
            if (!arena->growable) {
                return NULL;
            }
            // by default create same region size, if we request more than that then stick to it
            uint64_t new_size = arena_region_size(arena->current_region);
            if (size > new_size) {
                new_size = size;
            }
            // Create a new region with fixed first size
            ArenaRegion* new_region = arena_region_create(new_size);
            if (new_region == NULL) {
                printf("Error: arena_region_create failed\n");
                return NULL;
            }
            arena->current_region->next = new_region;
            arena->current_region = new_region;
        }

        ArenaRegion* region = arena->current_region;
        void* result = region->current;
        region->current = (void*)((uintptr_t)region->current + size);
        return result;
    }

    static inline void arena_destroy(Arena* arena) {
        ArenaRegion* region = arena->regions;
        while (region != NULL) {
            ArenaRegion* next = region->next;
            arena_region_destroy(region);
            region = next;
        }
        free(arena);
    }

    static inline void pb_profile_anchor_thread_flush(pb_profile_anchor* anchor, uint64_t thread_id) {
        pthread_mutex_lock(&g_profiler.pb_file_mutex);
        Arena* arena = anchor->anchor_results_arena[thread_id];
        //assert mutex is locked
        PROFILE_ASSERT(pthread_mutex_trylock(&anchor->mutex[thread_id]) == EBUSY);
        PROFILE_ASSERT(arena->growable == false);
        FILE* log_file = g_profiler.pb_profile_file;
        uint64_t amount = (uintptr_t)arena->current_region->current - (uintptr_t)arena->current_region->start;
        int ret;
        if (amount > 0) {
            pb_profile_flush_header header;
            header.thread_id = thread_id;
            header.name_length = strlen(anchor->name);
            header.result_amount = amount;
            // printf("writing %lu bytes\n", amount);
            ret = fwrite(&header, sizeof(pb_profile_flush_header), 1, log_file);
            if (ret == 0) {
                printf("Error: fprintf header failed\n");
                exit(EXIT_FAILURE);
            }
            ret = fwrite(anchor->name, 1, header.name_length, log_file);
            if (ret == 0) {
                printf("Error: fprintf name failed\n");
                exit(EXIT_FAILURE);
            }
            ret = fwrite(arena->current_region->start, 1, (uintptr_t)arena->current_region->current - (uintptr_t)arena->current_region->start, log_file);
            if (ret == 0) {
                printf("Error: fwrite flush failed\n");
                exit(EXIT_FAILURE);
            }
        }
        arena->current_region->current = arena->current_region->start;
        pthread_mutex_unlock(&g_profiler.pb_file_mutex);
    }
    static inline void pb_profile_anchor_result_add(pb_profile_anchor* anchor, uint64_t thread_id, pb_profile_anchor_result_type type, uint64_t value) {
        pthread_mutex_lock(&anchor->mutex[thread_id]);
        Arena* arena = anchor->anchor_results_arena[thread_id];
        pb_profile_anchor_result* result_ptr = (pb_profile_anchor_result*)arena_alloc(arena, sizeof(pb_profile_anchor_result));
        if (result_ptr == NULL) {
            pb_profile_anchor_thread_flush(anchor, thread_id);
            result_ptr = (pb_profile_anchor_result*)arena_alloc(arena, sizeof(pb_profile_anchor_result));
        }
        result_ptr->type = type;
        result_ptr->value = value;
        pthread_mutex_unlock(&anchor->mutex[thread_id]);
    }

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
            uint64_t start_cycles, start_cache, start_branch;
            const char* function;
            uint64_t index;
            uint32_t processor_id;
            uint64_t flags;
            PbProfile(const char* function, uint64_t index, uint64_t flags = 0) {
                if (!g_profiler.profiling) {
                    return;
                }
                g_profiler.anchors[index].name = function;
                this->function = function;
                this->index = index;
                this->flags = flags;
                // start = __rdtscp(&processor_id);


                {
                    pb_perf_event_open(PB_PERF_CYCLES);
                    uint64_t prev = pb_perf_event_read(PB_PERF_CYCLES);
                    start_cycles = prev;
                }
                if (flags & PB_PROFILE_CACHE) {
                    pb_perf_event_open(PB_PERF_CACHE_MISSES);
                    uint64_t prev = pb_perf_event_read(PB_PERF_CACHE_MISSES);
                    start_cache = prev;
                }

                if (flags & PB_PROFILE_BRANCH) {
                    pb_perf_event_open(PB_PERF_BRANCH_MISS);
                    uint64_t prev = pb_perf_event_read(PB_PERF_BRANCH_MISS);
                    start_branch = prev;
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
                if (!g_profiler.profiling) {
                    return;
                }
                // uint64_t elapsed = __rdtscp(&end_processor_id) - start;
                uint64_t thread_id = hash_thread_id();

                {
                    uint64_t count = pb_perf_event_read(PB_PERF_CYCLES);
                    count -= start_cycles;
                    pb_profile_anchor_result_add(&g_profiler.anchors[index], thread_id, PB_PROFILE_ANCHOR_CYCLES, count);
                }

                if (flags & PB_PROFILE_CACHE) {
                    // TODO(pere): deal with overflow
                    uint64_t count = pb_perf_event_read(PB_PERF_CACHE_MISSES);
                    count -= start_cache;
                    pb_profile_anchor_result_add(&g_profiler.anchors[index], thread_id, PB_PROFILE_ANCHOR_CACHE_MISSES, count);
                }
                if (flags & PB_PROFILE_BRANCH) {
                    uint64_t count = pb_perf_event_read(PB_PERF_BRANCH_MISS);
                    count -= start_branch;
                    pb_profile_anchor_result_add(&g_profiler.anchors[index], thread_id, PB_PROFILE_ANCHOR_BRANCH_MISSES, count);
                }

                // if (processor_id != end_processor_id) {
                //   pb_profile_anchor_result_add(&g_profiler.anchors[index], thread_id, PB_PROFILE_ANCHOR_CPU_MIGRATIONS, elapsed);
                //   return;
                // }
                // pb_profile_anchor_result_add(&g_profiler.anchors[index], thread_id, PB_PROFILE_ANCHOR_CYCLES, elapsed);
                // g_profiler.anchors[index].hits[thread_id]++;
            }
    };

//     static void print_profiling() {
//         PROFILE_ASSERT(g_profiler.pb_profile_file != NULL);
//         // uint64_t total_elapsed = __rdtsc() - g_profiler.start;
//         for (uint64_t i = 0; i < PROFILE_MAX_ANCHORS; i++) {
//             if (g_profiler.anchors[i].name == nullptr) {
//                 continue;
//             }
//             uint64_t sum_elapsed = 0;
//             uint64_t sum_hits = 0;
//             uint64_t sum_migrations = 0;
//             uint64_t sum_cache_misses = 0;
//             uint64_t sum_branch_misses = 0;
//             // TODO(pere): lightweight mode to avoid huge files, deal here
//             for (uint64_t j = 0; j < PROFILE_MAX_THREADS; j++) {
//                 // sum_elapsed += g_profiler.anchors[i].elapsed[j];
//                 // sum_hits += g_profiler.anchors[i].hits[j];
//                 // sum_migrations += g_profiler.anchors[i].cpu_migrations[j];
//                 // uint64_t count = g_profiler.anchors[i].cache_misses[j];
//                 // sum_cache_misses += g_profiler.anchors[i].cache_misses[j];
//                 // sum_branch_misses += g_profiler.anchors[i].branch_misses[j];
//             }
//             uint64_t avg_elapsed = 0;
//             if (sum_hits > 0) {
//                 avg_elapsed = sum_elapsed / sum_hits;
//             }
//             // fprintf(g_profiler.pb_profile_file, "total_elapsed %lu\n", total_elapsed);
//             // fprintf(g_profiler.pb_profile_file, "%s: ", g_profiler.anchors[i].name);
//             // fprintf(g_profiler.pb_profile_file, "cycles=%lu ", sum_elapsed);
//             // fprintf(g_profiler.pb_profile_file, "hits=%lu ", sum_hits);
//             // fprintf(g_profiler.pb_profile_file, "cpu_migrations=%lu ", sum_migrations);
//             // fprintf(g_profiler.pb_profile_file, "cache_misses=%lu ", sum_cache_misses);
//             // fprintf(g_profiler.pb_profile_file, "branch_misses=%lu ", sum_branch_misses);
//             // fprintf(g_profiler.pb_profile_file, "\n");
// #if PROFILE_TO_STDOUT == 1
//             printf("%s: ", g_profiler.anchors[i].name);
//             printf("cycles=%lu ", sum_elapsed);
//             printf("hits=%lu ", sum_hits);
//             printf("cpu_migrations=%lu ", sum_migrations);
//             printf("cache_misses=%lu ", sum_cache_misses);
//             printf("branch_misses=%lu ", sum_branch_misses);
//             printf("\n");
// #endif
//
//         }
//     }

    static void* profile_thread_entry(void* ctx) {
        while (g_profiler.profiling) {
            sleep(1);
            printf("profiling\n");
            // print_profiling();
        }
        return NULL;
    }




    static void pb_init_log_file(const char* filename) {
        // g_profiler.start = __rdtsc();
        g_profiler.profiling = true;
        g_profiler.pb_profile_file = NULL;
        g_profiler.pb_profile_file = fopen(filename, "w");
        if (g_profiler.pb_profile_file == NULL) {
            printf("Error: fopen() failed log file %s\n", filename);
            exit(EXIT_FAILURE);
        }
        int ret = pthread_create(&g_profiler.pb_profile_thread, NULL, profile_thread_entry, NULL);
        if (ret != 0) {
            printf("Error: pthread_create() failed %s\n", strerror(ret));
            exit(EXIT_FAILURE);
        }
    }

    // Static method to close the log profile_file
    static void pb_close_log_file() {
        pb_profiler_t &profiler = g_profiler;
        g_profiler.profiling = false;
        pthread_join(g_profiler.pb_profile_thread, NULL);
        // print_profiling();
        for ( uint64_t i = 0; i < PROFILE_MAX_ANCHORS; i++) {
            for (uint64_t j = 0; j < PROFILE_MAX_THREADS; j++) {
                pthread_mutex_lock(&profiler.anchors[i].mutex[j]);
                pb_profile_anchor_thread_flush(&profiler.anchors[i], j);
                pthread_mutex_unlock(&profiler.anchors[i].mutex[j]);

                pthread_mutex_destroy(&profiler.anchors[i].mutex[j]);
                arena_destroy(profiler.anchors[i].anchor_results_arena[j]);
            }
        }
        fclose(g_profiler.pb_profile_file);
        g_profiler.pb_profile_file = NULL;
    }


    class PbProfilerStart {
        Arena* profiler_arena;
        public:
        PbProfilerStart(const char* filename) {
            pb_profiler_t& profiler = g_profiler;
            char buffer[1024];
            profiler_arena = arena_create(sizeof(pb_profile_anchor) * PROFILE_MAX_ANCHORS, true);
            if (profiler_arena == NULL) {
                printf("Error: arena_create failed\n");
                exit(EXIT_FAILURE);
            }
            sprintf(buffer, "%d-%s", getpid(), filename);
            memset(&profiler, 0, sizeof(pb_profiler_t));
            profiler.anchors = (pb_profile_anchor*)arena_alloc(profiler_arena, sizeof(pb_profile_anchor) * PROFILE_MAX_ANCHORS);
            if (profiler.anchors == NULL) {
                printf("Error: arena_alloc anchors failed\n");
                exit(EXIT_FAILURE);
            }
            memset(profiler.anchors, 0, sizeof(pb_profile_anchor) * PROFILE_MAX_ANCHORS);
            for (uint64_t i = 0; i < PROFILE_MAX_ANCHORS; i++) {
                for (uint64_t j = 0; j < PROFILE_MAX_THREADS; j++) {
                    pthread_mutex_init(&profiler.anchors[i].mutex[j], NULL);
                    profiler.anchors[i].anchor_results_arena[j] = arena_create(1024 * 1024*20, false);
                }
            }
            pb_init_log_file(buffer); 
        }
        ~PbProfilerStart() {
          if (g_profiler.profiling) {
            pb_close_log_file();
            arena_destroy(profiler_arena);
          }
        }
    };
}

#define NameConcat2(A, B) A##B
#define NameConcat(A, B) NameConcat2(A, B)
#define PbProfileFunction(variable, label) pb_profiler::PbProfile variable((const char*)label, (uint64_t)(__COUNTER__ + 1))
#define PbProfileFunctionF(variable, label, flags) pb_profiler::PbProfile variable((const char*)label, (uint64_t)(__COUNTER__ + 1), flags)

#define PROFILE_MANUAL
#ifndef PROFILE_MANUAL
static pb_profiler::PbProfilerStart pb_profiler_start("profile.log"); // includes pid in filename
#endif

