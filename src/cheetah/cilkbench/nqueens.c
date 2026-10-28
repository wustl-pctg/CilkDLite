#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>

#include <cilk/cilk.h>

#if CILKSAN
#include "cilksan.h"
#endif

unsigned long long todval (struct timeval *tp) {
    return tp->tv_sec * 1000 * 1000 + tp->tv_usec;
}
#include "../runtime/cilk2c.h"
extern uintptr_t _SPAWN_ADDR_MAP_START_;
extern uintptr_t _SPAWN_ADDR_MAP_END_;
__attribute__((constructor(200))) void register_spawns() {
    uint32_t* offset = &_SPAWN_ADDR_MAP_START_;
    while (offset < &_SPAWN_ADDR_MAP_END_) {
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

// int * count;

/* 
 * nqueen  4 = 2 
 * nqueen  5 = 10 
 * nqueen  6 = 4 
 * nqueen  7 = 40 
 * nqueen  8 = 92 
 * nqueen  9 = 352 
 * nqueen 10 = 724
 * nqueen 11 = 2680 
 * nqueen 12 = 14200 
 * nqueen 13 = 73712 
 * nqueen 14 = 365596 
 * nqueen 15 = 2279184 
 */

/*
 * <a> contains array of <n> queen positions.  Returns 1
 * if none of the queens conflict, and returns 0 otherwise.
 */
int ok (int n, char *a) {

  int i, j;
  char p, q;

  for (i = 0; i < n; i++) {
    p = a[i];
    for (j = i + 1; j < n; j++) {
      q = a[j];
      if (q == p || q == p - (j - i) || q == p + (j - i))
        return 0;
    }
  }

  return 1;
}

int nqueens (int n, int j, char *a) {

  char *b;
  int i;
  int *count;
  int solNum = 0;

  if (n == j) {
    return 1;
  }

  count = (int *) alloca(n * sizeof(int));
  (void) memset(count, 0, n * sizeof (int));

  for (i = 0; i < n; i++) {

    /***
     * ANGE: strictly speaking, this (alloca after spawn) is frowned 
     * up on, but in this case, this is ok, because b returned by 
     * alloca is only used in this iteration; later spawns don't 
     * need to be able to access copies of b from previous iterations 
     ***/
    b = (char *) alloca((j + 1) * sizeof (char));
    memcpy(b, a, j * sizeof (char));
    b[j] = i;

    if(ok (j + 1, b)) {
      count[i] = cilk_spawn nqueens(n, j + 1, b);
    }
  }
  cilk_sync;

  //#pragma clang loop vectorize(disable)
  for(i = 0; i < n; i++) {
    solNum += count[i];
  }

  return solNum;
}


int main(int argc, char *argv[]) { 

  int n = 13;
  char *a;
  int res;

  if (argc < 2) {
    fprintf (stderr, "Usage: %s [<cilk-options>] <n>\n", argv[0]);
    fprintf (stderr, "Use default board size, n = 13.\n");

  } else {
    n = atoi (argv[1]);
    fprintf (stderr, "Running %s with n = %d.\n", argv[0], n);
  }

  a = (char *) alloca (n * sizeof (char));
  res = 0;

  struct timeval t1, t2;
  gettimeofday(&t1,0);
  cilkify(default_cilkrts);
  res = nqueens(n, 0, a);
  uncilkify(default_cilkrts);
  gettimeofday(&t2,0);
  unsigned long long runtime_ms = (todval(&t2)-todval(&t1))/1000;
  printf("%f\n", runtime_ms/1000.0);

  if (res == 0) {
    fprintf (stderr, "No solution found.\n");
  } else {
    fprintf (stderr, "Total number of solutions : %d\n", res);
  }

  return 0;
}
