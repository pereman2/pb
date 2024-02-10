#include "pb.c"

int main() {
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
