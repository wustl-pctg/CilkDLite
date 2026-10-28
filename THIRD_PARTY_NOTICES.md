# Component sources and notices

This distribution preserves the selected source trees and their existing
licenses. It does not replace individual source-file notices with one blanket
license. Exact revisions and download checksums are listed in `sources.json`.

| Component | Location / notice |
| --- | --- |
| OpenCilk / LLVM / Clang | `src/opencilk/LICENSE.TXT`, `MIT_LICENSE.TXT`, and component notices |
| Backtracking Cheetah runtime and benchmarks | `src/cheetah/LICENSE.TXT` and source-file notices |
| OpenCilk productivity tools | `src/cilktools/LICENSE.TXT` |
| Parlay | `src/parlaylib/LICENSE` |
| gperftools | `src/gperftools/COPYING` and bundled component notices |
| glibc 2.35 (downloaded during build) | GNU source archive `COPYING`, `COPYING.LIB`, `LICENSES` |
| GCC 13.2.0 / libstdc++ (downloaded during build) | GNU source archive `COPYING`, `COPYING3`, `COPYING.RUNTIME`, and library notices |

Linker scripts and individual benchmark files may carry additional upstream
notices. These are preserved in the snapshot. The Parlay include adjustment is
recorded separately in `patches/parlay-opencilk.patch`.

The GCC archive checksum is published in
[GNU's GCC 13.2.0 release checksums](https://gcc.gnu.org/pub/gcc/releases/gcc-13.2.0/sha512.sum).
The libstdc++ recipe builds through GCC's source/build system, following the
[upstream build model](https://gcc.gnu.org/onlinedocs/libstdc++/manual/setup.html).
