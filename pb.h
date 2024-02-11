#ifndef PB_H
#define PB_H

#include <sys/mman.h>
#include <sys/time.h>
#include <x86intrin.h>

#include <stdio.h>
#include <stdlib.h>

typedef unsigned char u8;
typedef unsigned short u16;
typedef unsigned int u32;
typedef unsigned long long u64;

typedef char i8;
typedef short i16;
typedef int i32;
typedef long long i64;


#define pb_assert(cond) if (!(cond)) { printf("error: %s\n", #cond); abort(); }
#define pb_debug(var) printf("%s: %d\n", #var, var)

typedef struct _Arena {
  void* memory;
  u64 capacity;
  u64 pos;
  u64 auto_align;
} Arena;


Arena arena_allocate(u64 capacity);
void arena_release(Arena* arena);
void arena_set_auto_align(Arena *arena, u64 align);

u64 arena_pos(Arena *arena);

void* arena_push_no_zero(Arena* arena,  u64 size);
void* arena_push_aligner(Arena* arena, u64 align);
void* arena_push(Arena* arena, u64 size);
void arena_pop_to(Arena* arena, u64 pos);
void arena_pop(Arena* arena, u64 size);
void arena_clear(Arena* arena);

inline u64 pb_align(u64 value, u64 align);
inline void pb_memset(void* memory, u8 value, u64 size);
inline u64 pb_cycles();

#endif // PB_H
