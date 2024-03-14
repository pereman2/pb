#include <cstdint>
#include <linux/perf_event.h>
#include <unistd.h>
#include <cstdlib>
#include <iostream>
#include <thread>
// #define WITH_PB
#ifndef WITH_PB
#define PROFILE_MANUAL
#endif
#include "time_function.h"
#ifndef WITH_PB
#define PbProfileFunctionF(f, name, flags)
#endif
#include <vector>
#define PL_IMPLEMENTATION 1
#include "palanteer/c++/palanteer.h"


using namespace std;

struct Blob {
  char a[64];
};

const uint64_t amount = 1000;
Blob blobs[amount][amount];

void second() {
  for (int i = 0; i < 1000; i++) {
    for (int j = 0; j < 1000; j++) {
      PbProfileFunctionF(f, "second", PB_PROFILE_CACHE | PB_PROFILE_BRANCH);
      // 1 cache miss
      blobs[j][i].a[0] = 0;
    }
  }
}

void first() {
  for (int i = 0; i < amount; i++) {
    for (int j = 0; j < amount; j++) {
      // 1 cache miss
      blobs[j][i].a[0] = 0;
      PbProfileFunctionF(f, "first", PB_PROFILE_CACHE | PB_PROFILE_BRANCH);
    }
  }
  second();
}

void do_not_optimize_away(volatile void* p) {
  asm volatile("" : : "r,m"(p) : "memory");
}


void infinite() {
  PbProfileFunctionF(f, "infinite", PB_PROFILE_CACHE | PB_PROFILE_BRANCH);
  while (true) {
    first();
  }
}

void third() {
  for (int i = 0; i < 1000; i++) {
    PbProfileFunctionF(f, "third", 0);
    for (int j = 0; j < 1000; j++) {
      // 1 cache miss
      blobs[j][i].a[0] = rand() % 1000;
      do_not_optimize_away((volatile void*)&blobs[j][i].a);
    }
  }
}

int main() {
  pb_profiler::PbProfilerStart pb_profiler_start("profile.log"); // includes pid in filename
  std::vector<std::thread> threads;
  for (int i = 0; i < 8; i++) {
    std::thread t(third);
    threads.push_back(std::move(t));
  }

  for (auto& t : threads) {
    t.join();
  }
}
