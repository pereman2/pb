#ifndef PB_H
#define PB_H

#include <stdio.h>
#include <stdlib.h>
#include <sys/mman.h>
#include <sys/time.h>
#include <x86intrin.h>

typedef unsigned char u8;
typedef unsigned short u16;
typedef unsigned int u32;
typedef unsigned long long u64;

typedef char i8;
typedef short i16;
typedef int i32;
typedef long long i64;

#define pb_assert(cond)           \
        if (!(cond)) {                  \
                printf("error: %s\n", #cond); \
                abort();                      \
        }
#define pb_debug(var) printf("%s: %d\n", #var, var)

typedef struct _Allocator {
        void* (*allocate)(struct _Allocator* allocator, u64 size);
        void (*deallocate)(struct _Allocator* allocator, void* memory);
        void (*set_auto_align)(struct _Allocator* allocator, u64 size);
        void* ctx;
} Allocator;

enum AllocatorType {
        PB_ALLOCATOR_ARENA,
        PB_ALLOCATOR_SYSTEM,
};

typedef struct _Arena {
        void* memory;
        u64 capacity;
        u64 pos;
        u64 auto_align;
} Arena;

inline Arena* pb_allocator_arena_get(Allocator* allocator) {
        return (Arena*)allocator->ctx;
}

Arena arena_allocate(u64 capacity);
void arena_release(Allocator* arena);
void arena_set_auto_align(Allocator* arena, u64 align);

u64 arena_pos(Allocator* arena);

void* arena_push_no_zero(Allocator* arena, u64 size);
void* arena_push_aligner(Allocator* arena, u64 align);
void* arena_push(Allocator* arena, u64 size);
void arena_pop_to(Allocator* arena, u64 pos);
void arena_pop(Allocator* arena, void* memory_to);
void arena_clear(Allocator* arena);

inline u64 pb_align(u64 value, u64 align);
inline void pb_memset(void* memory, u8 value, u64 size);
inline u64 pb_cycles();

typedef struct _SystemAllocator {
        u64 alignment;
} SystemAllocator;

inline SystemAllocator* pb_get_allocator_system_get(Allocator* allocator) {
        return (SystemAllocator*)allocator->ctx;
}

void* pb_sys_allocate(Allocator* sys_allocator, u64 size);
void pb_sys_deallocate(Allocator* sys_allocator, void* memory);
void pb_sys_set_auto_align(Allocator* sys_allocator, u64 size);



Allocator pb_allocator_create(enum AllocatorType type, u64 capacity);



#endif  // PB_H
