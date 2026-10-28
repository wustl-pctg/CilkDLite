# OpenCilk backtracking

This repo contains the compiler, runtime, and benchmarks. The build scripts use
an Ubuntu 22.04 Docker container.

## Build

You'll need a Linux x86-64 machine, Docker, and internet access for the initial
downloads. Run these commands from the repo root:

```sh
./scripts/build-container.sh
./scripts/launch-container.sh ./scripts/build.sh
```

This builds the compiler, libraries, runtime, and benchmarks, then runs smoke
tests. The first build takes a while. Later builds reuse the files in
`build/`.

Builds use up to eight jobs by default. If you run out of memory, lower the
job count and rerun:

```sh
./scripts/launch-container.sh env JOBS=4 LINK_JOBS=1 ./scripts/build.sh
```

After the initial build, you can rebuild a single stage:

```sh
./scripts/launch-container.sh ./scripts/build.sh benchmarks
```

The stages are `compiler`, `deps`, `runtime`, `benchmarks`, and `smoke`.
To open a shell in the container, run `./scripts/launch-container.sh`.
The repo is mounted at `/workspace`.

## Run benchmarks

Run all supported benchmarks with small inputs and one and two workers:

```sh
./scripts/launch-container.sh python3 src/cheetah/cilkbench/run.py \
    --suite all --workers 1,2
```

To run a single benchmark:

```sh
./scripts/launch-container.sh python3 src/cheetah/cilkbench/run.py \
    --suite cilkbench --benchmarks fib --size 20 --workers 1,2,4 --repeats 3
```

The suites are `cilkbench`, `parlaylib`, `compiled_test`
Use `--smoke` for a smaller set of runs, `--list` to see the supported targets,
and `--help` for all options:

```sh
./scripts/launch-container.sh python3 src/cheetah/cilkbench/run.py --suite all --list
./scripts/launch-container.sh python3 src/cheetah/cilkbench/run.py --help
```

Results and logs go in `build/results/`. Executables are in
`build/benchmarks/<suite>/`. To run one directly, use the wrapper so it loads
the required libraries:

```sh
./scripts/launch-container.sh env CILK_NWORKERS=2 \
    ./scripts/run-benchmark.sh ./build/benchmarks/compiled_test/fib 20
```

The compiler tools are in `build/opencilk/bin/`, and the runtime libraries and
headers are in `build/cheetah/`.
