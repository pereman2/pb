#include <algorithm>
#include <math.h>
#define PROFILE_MANUAL
#include "time_function.h"

#include <map>
#include <string>
#include <vector>



const char* pb_profile_anchor_type_to_string(pb_profile_anchor_result_type type) {
  switch (type) {
    case PB_PROFILE_ANCHOR_CYCLES:
      return "cycles";
    case PB_PROFILE_ANCHOR_HITS:
      return "hits";
    case PB_PROFILE_ANCHOR_CPU_MIGRATIONS:
      return "cpu_migrations";
    case PB_PROFILE_ANCHOR_CACHE_MISSES:
      return "cache_misses";
    case PB_PROFILE_ANCHOR_BRANCH_MISSES:
      return "branch_misses";
  }
  return "unknown";
}

void print_results(const char* function, std::map<std::string, std::vector<std::vector<uint64_t>>> &per_function_results) {
  auto stdev = [](std::vector<uint64_t>& values) {
    uint64_t sum = 0;
    for (int i = 0; i < values.size(); i++) {
      sum += values[i];
    }
    uint64_t mean = sum / values.size();
    uint64_t sum_of_squares = 0;
    for (int i = 0; i < values.size(); i++) {
      sum_of_squares += (values[i] - mean) * (values[i] - mean);
    }
    return sqrt(sum_of_squares / values.size());
  };

  auto p99 = [](std::vector<uint64_t>& values) {
    return values[values.size() * 99 / 100];
  };

  printf("Results %s:\n", function);
  for (auto it = per_function_results.begin(); it != per_function_results.end(); it++) {
    printf("Function %s:\n", it->first.c_str());
    for (int i = 0; i < PB_PROFILE_ANCHOR_LAST; i++) {
      if (it->second[i].size() > 0) {
        std::sort(it->second[i].begin(), it->second[i].end());
        uint64_t sum = 0;
        for (int j = 0; j < it->second[i].size(); j++) {
          sum += it->second[i][j];
        }
        printf("  %20s: samples: %20lu, avg: %20lu stdev %20f p99 %20lu\n", 
            pb_profile_anchor_type_to_string((pb_profile_anchor_result_type)i), 
            it->second[i].size(), 
            sum / it->second[i].size(), 
            stdev(it->second[i]),
            p99(it->second[i]));
      }
    }
  }
}
void parse_stats(const char* filename, std::map<std::string, std::vector<std::vector<uint64_t>>> &per_function_results_all) {
  FILE* file = fopen(filename, "r");
  if (file == NULL) {
    printf("Error: file not found\n");
    return;
  }
  Arena* arena = arena_create(1024*1024, true);

  pb_profile_anchor_result result;
  pb_profile_flush_header header;
  int ret;
  std::map<std::string, std::vector<std::vector<uint64_t>>> per_function_results;
  while (fread(&header, sizeof(pb_profile_flush_header), 1, file) == 1) {
    // printf("Thread %lu amount %lu ", header.thread_id, header.result_amount);
    char* function = (char*)arena_alloc(arena, header.name_length);
    ret = fread(function, 1, header.name_length, file);
    if (ret != header.name_length) {
      printf("Error: could not read function %d\n", ret);
      break;
    }
    // printf("Function %s:\n", function);

    char* results_raw = (char*)arena_alloc(arena, header.result_amount);
    ret = fread(results_raw, 1, header.result_amount, file);
    if (ret != header.result_amount) {
      printf("Error: could not read results %d\n", ret);
      break;
    }
    if (per_function_results.find(function) == per_function_results.end()) {
      per_function_results[function] = std::vector<std::vector<uint64_t>>(PB_PROFILE_ANCHOR_LAST);
    }
    pb_profile_anchor_result* results = (pb_profile_anchor_result*)results_raw;
    for (int i = 0; i < header.result_amount / sizeof(pb_profile_anchor_result); i++) {
      result = results[i];
      per_function_results[function][result.type].push_back(result.value);
      // printf("  %s: %lu\n", pb_profile_anchor_type_to_string(result.type), result.value);
    }
  }

  print_results(filename, per_function_results);

  for (auto &function_results : per_function_results) {
    if (per_function_results_all.find(function_results.first) == per_function_results_all.end()) {
      per_function_results_all[function_results.first] = std::vector<std::vector<uint64_t>>(PB_PROFILE_ANCHOR_LAST);
    }
    for (int i = 0; i < PB_PROFILE_ANCHOR_LAST; i++) {
      per_function_results_all[function_results.first][i].insert(per_function_results_all[function_results.first][i].end(), function_results.second[i].begin(), function_results.second[i].end());
    }
  }
  fclose(file);
  printf("Done\n");
  arena_destroy(arena);
}

int main(int argc, char** argv) {
  if (argc < 2) {
    printf("Usage: %s <profile.log>...\n", argv[0]);
    return 1;
  }
  std::map<std::string, std::vector<std::vector<uint64_t>>> per_function_results_all;
  for (int i = 1; i < argc; i++) {
    parse_stats(argv[i], per_function_results_all);
  }
  print_results("All", per_function_results_all);
  return 0;
}
