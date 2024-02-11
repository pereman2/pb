#include "pb.c"

inline void no_optimize(void* value) {
#if defined(__clang__)
  asm volatile("" : "+r,m"(value) : : "memory");
#else
  asm volatile("" : "+m,r"(value) : : "memory");
#endif
}

void benchmark_unaligned() {
  u64 total_memory = 1024*1024*1024;
  // mix aligned and unaligned
  u64 blocks[] = {64, 65, 128, 129};

  {
    Arena arena = arena_allocate(total_memory);
    u64 block_index = 0;
    u64 amount = 0;
    u64 start = pb_cycles();
    while(amount < total_memory) {
      void* p = arena_push_no_zero(&arena, blocks[block_index]);
      no_optimize(p);
      amount += blocks[block_index];
    }
    u64 end = pb_cycles();
    printf("%20s: %15llu\n", "Cycles arena", end - start);
    arena_release(&arena);
  }

  {
    u64 block_index = 0;
    u64 amount = 0;
    u64 start = pb_cycles();
    while(amount < total_memory) {
      void* p = malloc(blocks[block_index]);
      no_optimize(p);
      amount += blocks[block_index];

    }
    u64 end = pb_cycles();
    printf("%20s: %15llu\n", "Cycles malloc", end - start);
  }
}

void verify() {
  Arena arena = arena_allocate(1024);

  arena_set_auto_align(&arena, 64);
  pb_assert(arena.auto_align == 64);
  pb_assert(arena.pos == 0);

  void* p1 = arena_push_no_zero(&arena, 10);
  pb_assert(p1 == arena.memory);
  pb_assert(arena.pos == 10);
  pb_memset(p1, 0, 10);

  void* p2 = arena_push_no_zero(&arena, 10);
  pb_assert(p2 == arena.memory + 64);
  pb_assert(arena.pos == 64+10);

  arena_release(&arena);

}

int main() {
  benchmark_unaligned();
}