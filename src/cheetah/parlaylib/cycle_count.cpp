#include <cilk/cilk.h>

#include <iostream>
#include <string>

#include <parlay/primitives.h>
#include <parlay/random.h>
#include <parlay/internal/get_time.h>

#include "cycle_count.h"
extern "C" {
  #include "../runtime/cilk2c.h"
  extern uintptr_t _SPAWN_ADDR_MAP_START_;
  extern uintptr_t _SPAWN_ADDR_MAP_END_;
  __attribute__((constructor(200))) void register_spawns() {
      uint32_t* offset = (uint32_t*) &_SPAWN_ADDR_MAP_START_;
      while (offset < (uint32_t*) &_SPAWN_ADDR_MAP_END_) {
          uintptr_t func_addr = *((uintptr_t*) offset);
          //printf("Found spawning function at %p\n", func_addr);
          offset += 2;
          uint64_t total_count = *((uintptr_t*) offset);
          offset += 2;
          for (int i = 0; i < total_count; i++) {
              offset += 3;
              uintptr_t retpad = func_addr + (*offset);
              offset += 4;
              uintptr_t landpad = func_addr + (*offset);
              offset += 4;
              uintptr_t poppad = func_addr + (*offset);
              offset += 1;
              //printf("Found retpad %p with landpad %p and poppad %p\n", retpad, landpad, poppad);
              int errval = __cilkrts_register_spawn_state(
                              retpad,
                              retpad,
                              poppad,
                              landpad);
              if (errval) printf("Error %d loading spawn states\n", errval);
          }
      }
  }
}

// **************************************************************
// Driver
// **************************************************************
int main(int argc, char* argv[]) {
  auto usage = "cycle_count <n>";
  if (argc != 2) std::cout << usage << std::endl;
  else {
    long n;
    try { n = std::stol(argv[1]); }
    catch (...) { std::cout << usage << std::endl; return 1; }

    auto permutation = parlay::random_permutation(n);
    cilkify(default_cilkrts);
    parlay::internal::timer t("Time");
    long count;
    for (int i=0; i < 3; i++) {
      count = cycle_count(permutation);
      t.next("Cycle Count");
    }
    uncilkify(default_cilkrts);

    std::cout << "number of cycles: " << count << std::endl;
  }
}
