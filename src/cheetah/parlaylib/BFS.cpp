#include <iostream>
#include <string>

#include <parlay/primitives.h>
#include <parlay/sequence.h>
#include <parlay/internal/get_time.h>

#include "BFS.h"
#include "helper/graph_utils.h"

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
using vertex = int;
using nested_seq = parlay::sequence<parlay::sequence<vertex>>;
using graph = nested_seq;
using utils = graph_utils<vertex>;

int main(int argc, char* argv[]) {
  cilkify(default_cilkrts);
  {
    auto usage = "Usage: BFS <n> || BFS <filename>";
    if (argc != 2) std::cout << usage << std::endl;
    else {
      long n = 0;
      graph G;
      try { n = std::stol(argv[1]); }
      catch (...) {}
      if (n == 0) {
        G = utils::read_graph_from_file_pbbs(argv[1]);
        n = G.size();
      } else {
        G = utils::rmat_graph(n, 20*n);
      }
      utils::print_graph_stats(G);
      nested_seq result;
      parlay::internal::timer t("Time");
      for (int i=0; i < 3; i++) {
        result = BFS(1, G);
        t.next("BFS");
      }

      long visited = parlay::reduce(parlay::map(result, parlay::size_of()));
      std::cout << "num vertices visited: " << visited << std::endl;
    }
  }
  uncilkify(default_cilkrts);
}
