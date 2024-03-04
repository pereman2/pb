#include <cstdint>
#include <linux/perf_event.h>
#include <unistd.h>
#include <cstdlib>
#include <iostream>
#include <thread>
#include "time_function.h"
#include <vector>

using namespace std;

struct Blob {
  char a[64];
};

const uint64_t amount = 1000;
Blob blobs[amount][amount];

void second() {
  PbProfileFunctionF(f, "second", PB_PROFILE_CACHE | PB_PROFILE_BRANCH);
  for (int i = 0; i < 1000; i++) {
    for (int j = 0; j < 1000; j++) {
      // 1 cache miss
      blobs[j][i].a[0] = 0;
    }
  }
}
void first() {
  PbProfileFunctionF(f, "first", PB_PROFILE_CACHE | PB_PROFILE_BRANCH);
  for (int i = 0; i < amount; i++) {
    for (int j = 0; j < amount; j++) {
      // 1 cache miss
      blobs[j][i].a[0] = 0;
    }
  }
  second();
}

int main() {
  PbProfileFunctionF(f, "main", 0);
  std::vector<std::thread> threads;
  for (int i = 0; i < 10; i++) {
    std::thread t(first);
    threads.push_back(std::move(t));
  }

  for (auto& t : threads) {
    t.join();
  }
}
