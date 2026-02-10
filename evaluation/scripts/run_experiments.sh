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
cd "${root_dir}"/build/release && cmake --build . --config Release
cd "${root_dir}"/evaluation/build && cmake --build . --config Release

cd "${root_dir}"/evaluation/experiments

../build/playground 1000000 100 -w -l Baseline > pipeline_durations_100_partitions_baseline.txt
../build/playground 1000000 100 -w -l Combined > pipeline_durations_100_partitions_combined.txt
../build/playground 1000000 10000 -w -l Baseline > pipeline_durations_10000_partitions_baseline.txt
../build/playground 1000000 10000 -w -l Combined > pipeline_durations_10000_partitions_combined.txt

git checkout main
git pull
cd "${root_dir}"/build/release && cmake --build . --config Release
cd "${root_dir}"/evaluation/build && cmake --build . --config Release
cd "${root_dir}"/experiments_analyses
ninja all_experiments
cp results/* "${root_dir}"/evaluation/experiments

git checkout window_functions
cd "${root_dir}"/build/release && cmake --build . --config Release
cd "${root_dir}"/evaluation/build && cmake --build . --config Release
cd "${root_dir}"
