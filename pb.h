#ifndef PB_H
#define PB_H

#include <string.h>
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

typedef struct _Arena {
        void* memory;
        u64 capacity;
        u64 pos;
        u64 auto_align;
} Arena;

typedef struct _SystemAllocator {
        u64 alignment;
} SystemAllocator;

typedef struct _Allocator {
        void* (*allocate)(struct _Allocator* allocator, u64 size);
        void (*deallocate)(struct _Allocator* allocator, void* memory);
        void (*set_auto_align)(struct _Allocator* allocator, u64 size);
        union {
          Arena arena;
          SystemAllocator system_allocator;
        } ctx;
} Allocator;

enum AllocatorType {
        PB_ALLOCATOR_ARENA,
        PB_ALLOCATOR_SYSTEM,
};


inline Arena* pb_allocator_arena_get(Allocator* allocator) {
        return (Arena*)&allocator->ctx.arena;
}

Arena pb_arena_allocate(u64 capacity);
void pb_arena_release(Allocator* arena);
void pb_arena_set_auto_align(Allocator* arena, u64 align);

u64 pb_arena_pos(Allocator* arena);

void* pb_arena_push_no_zero(Allocator* arena, u64 size);
void* pb_arena_push_aligner(Allocator* arena, u64 align);
void* pb_arena_push(Allocator* arena, u64 size);
void pb_arena_deallocate(Allocator* allocator, void* memory_to);
void pb_arena_pop_to(Allocator* arena, u64 pos);
void pb_arena_pop(Allocator* arena, void* memory_to);
void pb_arena_clear(Allocator* arena);

inline u64 pb_align(u64 value, u64 align);
inline void pb_memset(void* memory, u8 value, u64 size);
inline u64 pb_cycles();


inline SystemAllocator* pb_get_allocator_system_get(Allocator* allocator) {
        return (SystemAllocator*)&allocator->ctx.system_allocator;
}

void* pb_sys_allocate(Allocator* sys_allocator, u64 size);
void pb_sys_deallocate(Allocator* sys_allocator, void* memory);
void pb_sys_set_auto_align(Allocator* sys_allocator, u64 size);



Allocator pb_allocator_create(enum AllocatorType type, u64 capacity);


typedef struct _DynamicArray {
  Allocator* allocator;
  u64 capacity;
  u64 size;
  u64 element_size;
} DynamicArrayHeader;


#define DynamicArray(Type) Type* 
#define pb_dynamic_array_header(array) ((DynamicArrayHeader*)array-1)
#define pb_dynamic_array_size(array) pb_dynamic_array_header(array)->size
#define pb_dynamic_array_capacity(array) pb_dynamic_array_header(array)->capacity
#define pb_dynamic_array_element_size(array) pb_dynamic_array_header(array)->element_size
#define pb_dynamic_array_start(array) (void*)((u8*)array)

#define pb_dynamic_array_init(array, allocator, capacity) pb_dynamic_array_init_impl((void**)&array, sizeof(*array), allocator, capacity)
#define pb_dynamic_array_push(array, value) do {\
  pb_dynamic_array_grow((void**)&array); \
  array[pb_dynamic_array_size(array)++] = value; \
} while(0)

void pb_dynamic_array_init_impl(void** array, u64 element_size, Allocator* allocator, u64 capacity);
void pb_dynamic_array_release(void* array, Allocator* allocator);
void pb_dynamic_array_push_impl(void** array, void* value);
void pb_dynamic_array_clear(void* array);
void pb_dynamic_array_grow(void** array);


inline void pb_dynamic_array_init_impl(void** array, u64 element_size, Allocator* allocator, u64 capacity) {
  DynamicArrayHeader* header = (DynamicArrayHeader*)allocator->allocate(allocator, (capacity * element_size) + sizeof(DynamicArrayHeader));
  header->allocator = allocator;
  header->capacity = capacity;
  header->size = 0;
  header->element_size = element_size;
  *array = (void*)(header + 1);
}

inline void pb_dynamic_array_release(void* array, Allocator* allocator) {
  allocator->deallocate(allocator, array);
}

inline void pb_dynamic_array_push_impl(void** array, void* value) {
  DynamicArrayHeader* header = pb_dynamic_array_header(array);
  pb_dynamic_array_grow(array);
  u8* pos = (u8*)pb_dynamic_array_start(array) + header->size;
  memcpy(pos, value, header->element_size);
  header->size += header->element_size;
}

inline void pb_dynamic_array_grow(void** array) {
  DynamicArrayHeader* header = pb_dynamic_array_header(*array);
  if (header->size+1 >= header->capacity) {
    u64 new_capacity = (header->capacity * 2) + 8;
    DynamicArrayHeader* new_array = (DynamicArrayHeader*)header->allocator->allocate(header->allocator, new_capacity * header->element_size + sizeof(DynamicArrayHeader));
    memcpy((void*)new_array, (void*)header, header->capacity * header->element_size + sizeof(DynamicArrayHeader));

    new_array->capacity = new_capacity;

    *array = (void*)(new_array+1);
    header->allocator->deallocate(header->allocator, header);
    pb_assert(header->allocator == ((DynamicArrayHeader*)(*array)-1)->allocator);
  }
}

inline void pb_dynamic_array_clear(void* array) {
  DynamicArrayHeader* header = pb_dynamic_array_header(array);
  header->size = 0;
}



#endif  // PB_H
