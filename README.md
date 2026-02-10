## Window Function Optimization: Co-Evaluation and Other Techniques

This repository contains an adapted version of DuckDB with predicate Co-Evaluation for window functions.
There are adaptations in multiple places:
- Co-Evaluation with Early Stop: [window_rank_function.cpp](./src/function/window/window_rank_function.cpp)
- Merge Pruning: [sorted_run_merger.cpp](./src/common/sort/sorted_run_merger.cpp)
- (Adaptive) Partition Pruning: [hashed_sort.cpp](./src/common/sort/hashed_sort.cpp)

### Reproducibility
- Build
  ```sh
  git submodule update --init --recursive
  COMMON_CMAKE_VARS="-DCMAKE_C_COMPILER=<your_c_compiler> -DCMAKE_CXX_COMPILER=<your_cxx_compiler>" \
      DISABLE_SANITIZER=1 DISABLE_VPTR_SANITIZER=1 GEN=ninja make release
  cd evaluation
  mkdir -p build
  cd build
  cmake .. -DCMAKE_BUILD_TYPE=Release -DCMAKE_C_COMPILER=<your_c_compiler> \
      -DCMAKE_CXX_COMPILER=<your_cxx_compiler> -GNinja
  cmake --build .
  cd ../..
  ```

- Data generation
  ```sh
  ./evaluation/scripts/generate_data.sh
  ```

- Run the microbenchmarks
  ```sh
  ./evaluation/scripts/run_experiments.sh
  ```
  The result data is located in `evaluation/experiments`.
  If you run on a multi-socket machine, you need to execute within a Docker container to restrict to a NUMA region:
  ```sh
    docker --run -v "$(pwd)":"$(pwd)" --cpuset-cpus <region CPUs> --cpuset-mems <region ID> -it ubuntu:24.04
  ```

- Plot
  ```sh
  ./evaluation/scripts/create_plots.sh
  ```
  The plots are located in `evaluation/figures`.

- Dependencies
  ```txt
  cmake ninja-build golang python3 texlive texlive-fonts-extra
  ```
