#!/bin/bash

root_dir=$(pwd)
mkdir -p evaluation/experiments
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
git pull
cmake --build "${root_dir}"/build/release --config Release
cmake --build "${root_dir}"/evaluation/build --config Release

cd "${root_dir}"/evaluation/experiments

../build/playground 1000000 100 -w -l Baseline > pipeline_durations_100_partitions_baseline.txt
../build/playground 1000000 100 -w -l Combined > pipeline_durations_100_partitions_combined.txt
../build/playground 1000000 10000 -w -l Baseline > pipeline_durations_10000_partitions_baseline.txt
../build/playground 1000000 10000 -w -l Combined > pipeline_durations_10000_partitions_combined.txt

git checkout window_functions
cmake --build "${root_dir}"/build/release --config Release
cmake --build "${root_dir}"/evaluation/build --config Release
cd "${root_dir}"
