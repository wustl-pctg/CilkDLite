# Parlay benchmark drivers

`BFS` and `pagerank` are the maintained backtracking build targets in this
snapshot. They use the pinned Parlay headers in `src/parlaylib/include` and its
example helpers in `src/parlaylib/examples`; neither a global installation nor
a private home-directory path is required.

```sh
src/cheetah/parlaylib/run.sh --smoke --workers 1,2
src/cheetah/parlaylib/run.sh --benchmarks BFS --size 1024 --repeats 3
```

The size is the number of vertices in the generated graph (a power of two,
at least 2). Direct executable
invocation also accepts the graph filename supported by the original drivers.
BFS and pagerank retain their internal timing repetitions (3 and 5,
respectively); runner repetitions repeat the entire process. The runner checks graph dimensions, BFS visited-vertex bounds, and finite
nonnegative PageRank output; these are sanity checks, not complete algorithm
correctness proofs.

All other `.cpp`/`.h` drivers are retained as research source. They have not all
been adapted to enter/leave the backtracking runtime and register spawn maps,
and are excluded from the default build. Build and run outputs are placed under
`build/benchmarks/parlaylib` and `build/results`. See
`../cilkbench/README.md` for runner options.
