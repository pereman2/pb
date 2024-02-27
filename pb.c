#include "pb.h"

Arena pb_arena_allocate(u64 capacity) {
        void* memory = mmap(0, capacity, PROT_READ | PROT_WRITE,
                        MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
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

void pb_arena_release(Allocator* allocator) {
        Arena* arena = pb_allocator_arena_get(allocator);
        munmap(arena, arena->capacity + sizeof(Arena));
}
void pb_arena_set_auto_align(Allocator* allocator, u64 align) {
        Arena* arena = pb_allocator_arena_get(allocator);
        pb_assert((align & (align - 1)) == 0);
        arena->auto_align = align;
}

u64 pb_arena_pos(Allocator* allocator) { 
        Arena* arena = pb_allocator_arena_get(allocator);
        return arena->pos; 
}

void* pb_arena_push_no_zero(Allocator* allocator, u64 size) {
        Arena* arena = pb_allocator_arena_get(allocator);
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

void* pb_arena_push_aligner(Allocator* allocator, u64 align) {
        // ?
}

void* pb_arena_push(Allocator* allocator, u64 size) {
        Arena* arena = pb_allocator_arena_get(allocator);
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

void pb_arena_pop_to(Allocator* allocator, u64 pos) { 
        Arena* arena = pb_allocator_arena_get(allocator);
        arena->pos = pos; 
}

void pb_arena_pop(Allocator* allocator, void* memory_to) { 
        Arena* arena = pb_allocator_arena_get(allocator);
        u64 size = (u64)memory_to - pb_arena_pos(allocator);
        arena->pos -= size; 
}

void pb_arena_clear(Allocator* allocator) { 
        Arena* arena = pb_allocator_arena_get(allocator);
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

inline u64 pb_cycles() { return __rdtsc(); }

void* pb_sys_allocate(Allocator* alloc, u64 size) {
        // TODO(Pere): Add alignment
        return malloc(size);
}
void pb_sys_deallocate(Allocator* alloc, void* memory) {
        free(memory);
}
void pb_sys_set_auto_align(Allocator* alloc, u64 size) {
        SystemAllocator* sys_alloc = pb_get_allocator_system_get(alloc);
        sys_alloc->alignment = size;
}

Allocator pb_allocator_create(enum AllocatorType type, u64 capacity) {
        Allocator allocator;
        switch (type) {
                case PB_ALLOCATOR_SYSTEM:
                        allocator.allocate = pb_sys_allocate;
                        allocator.deallocate = pb_sys_deallocate;
                        allocator.set_auto_align = pb_sys_set_auto_align;
                        break;
                case PB_ALLOCATOR_ARENA:
                        allocator.allocate = pb_arena_push;
                        allocator.deallocate = pb_arena_pop;
                        allocator.set_auto_align = pb_arena_set_auto_align;
                        break;
                default:
                        pb_assert(0);
        }
        return allocator;
}
