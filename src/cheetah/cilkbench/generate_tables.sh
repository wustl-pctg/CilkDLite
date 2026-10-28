#!/usr/bin/env bash
set -euo pipefail
suite_dir=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)

benchmarks=${1:-"fib,nqueens,cilksort,fft,cholesky,heat,strassen,matmul"}
IFS=',' read -ra bench_array <<< "$benchmarks"

echo "\begin{tabular}{r|llllll}

\hdr{ bench-size-runtime }  & \hdr{\$T_1\$} & \hdr{\$T_2\$} & \hdr{\$T_5\$} & \hdr{\$T_{10}\$} & \hdr{\$T_{20}\$} & \hdr{\$T_{40}\$} \\\\
\hline
"

for benchmark in "${bench_array[@]}"; do
    python3 "$suite_dir/chart_benchmarks.py" "$benchmark"
done

echo "\end{tabular}"