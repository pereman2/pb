#include "pb.h"


Arena arena_allocate(u64 capacity) {
  void* memory = mmap(0, capacity, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
  if (memory == MAP_FAILED) {
    printf("Failed to allocate memory\n");
    abort();
  }
  Arena arena;
  arena.memory = memory;
  arena.capacity = capacity;
  arena.pos = 0;
  arena.auto_align = 0;
  return arena;
}

void arena_release(Arena* arena) {
  munmap(arena, arena->capacity + sizeof(Arena));
}
void arena_set_auto_align(Arena *arena, u64 align) {
  pb_assert((align & (align-1)) == 0);
  arena->auto_align = align;
}

u64 arena_pos(Arena *arena) {
  return arena->pos;
}

void* arena_push_no_zero(Arena* arena, u64 size) {
  u64 align = arena->auto_align;
  if (align > 0) {
    u64 left = pb_align((u64)arena->memory + arena->pos, align);
    pb_debug(left);
    arena->pos += left;
  }

  void* result = arena->memory + arena->pos;
  arena->pos += size;
  return result;
}

void* arena_push_aligner(Arena* arena, u64 align) {
  // ?
}

void* arena_push(Arena* arena, u64 size) {
  u64 align = arena->auto_align;
  if (align > 0) {
    u64 left = pb_align((u64)arena->memory + arena->pos, align);
    pb_debug(left);
    arena->pos += left;
  }

  void* result = arena->memory + arena->pos;
  arena->pos += size;
  pb_memset(result, 0, size);
  return result;
}

void arena_pop_to(Arena* arena, u64 pos) {
  arena->pos = pos;
}

void arena_pop(Arena* arena, u64 size) {
  arena->pos -= size;
}

void arena_clear(Arena* arena) {
  arena->pos = 0;
}

inline u64 pb_align(u64 value, u64 align) {
  return (align - ((align - 1) & value)) & (align - 1);
}

inline void pb_memset(void* memory, u8 value, u64 size) {
  u8* p = (u8*)memory;
  for (u64 i = 0; i < size; i++) {
    p[i] = value;
  }
}

inline u64 pb_cycles() {
  return __rdtsc();
}

