#!/bin/bash

export PATH="$PATH:$(pwd)/build/release"
mkdir -p evaluation/experiments
ln -sf $(pwd)/evaluation/data $(pwd)/evaluation/experiments/data
cd evaluation/experiments

../build/microbench 10000
../build/microbench 10000 --st
../build/microbench 100000
../build/microbench 100000 --st
../build/microbench 1000000
../build/microbench 1000000 --st
../build/microbench 10000000
../build/microbench 10000000 --st
../build/microbench 100000000
../build/microbench 100000000 --st

../build/microbench --skewed
../build/microbench --skewed --st

../build/adaptive_strategy
../build/adaptive_strategy --st

git checkout pipeline-times
cd ../../build/release && cmake --build . --config Release ; cd -
cd ../build && cmake --build . --config Release ; cd -

../build/playground 1000000 100 -w -l Baseline > pipeline_durations_100_partitions_baseline.txt
../build/playground 1000000 100 -w -l Combined > pipeline_durations_100_partitions_combined.txt
../build/playground 1000000 10000 -w -l Baseline > pipeline_durations_10000_partitions_baseline.txt
../build/playground 1000000 10000 -w -l Combined > pipeline_durations_10000_partitions_combined.txt

git checkout main
cd ../../build/release && cmake --build . --config Release ; cd -
cd ../build && cmake --build . --config Release ; cd -
cd ../../experiments_analyses
ninja all_experiments
cp results/* ../evaluation/experiments
cd ../evaluation

git checkout window_functions
cd ../../build/release && cmake --build . --config Release ; cd -
cd ../build && cmake --build . --config Release ; cd -
cd ../..
