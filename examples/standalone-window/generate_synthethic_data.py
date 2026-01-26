#!/usr/bin/env python3

import argparse as ap
import random
import os
from collections import defaultdict

import numpy as np
import scipy

import matplotlib.pyplot as plt

def parse_args():
    parser = ap.ArgumentParser()
    parser.add_argument("--cardinality", "-c", type=int, default=10**6)
    parser.add_argument("--partitions", "-p", type=int, default=100)
    parser.add_argument("--directory", "-d", type=str, default="data")
    parser.add_argument("--skewed", "-s", action="store_true", default=False)
    parser.add_argument("--skewness", type=float, default=1.2)
    parser.add_argument("--randomize", "-r", action="store_true", default=False)
    return parser.parse_args()

def main(cardinality, partitions, directory, skewed, randomize, skewness):
    if not skewed:
        assert cardinality > partitions, "Cardinality must be larger than number of partitions"
        rows_per_partition = [round(cardinality / partitions) for _ in range(partitions)]
    else:

        rand = np.random.default_rng(1717)
        distribution = rand.zipf(skewness, size=cardinality)
        rows_per_partition = defaultdict(int)
        for i in distribution:
            rows_per_partition[i] += 1
        rows_per_partition = [c for c in rows_per_partition.values()]
        rows_per_partition.sort(reverse=True)
        partitions = len(rows_per_partition)

    partition_values = range(partitions)
    if randomize:
        partition_values = random.Random(17).sample(range(2**30, 2**31), k=partitions)

    tuples = []
    for partition, count in zip(partition_values, rows_per_partition):
        for row in range(count):
            tuples.append((partition, row))

    random.Random(17).shuffle(tuples)

    os.makedirs(directory, exist_ok=True)
    skew = f"_skewed_{skewness}" if skewed else ""
    file_name = f"synthetic_{cardinality}-rows_{partitions}-partitions{skew}{'_randomized' if randomize else ''}.csv"
    with open(os.path.join(directory, file_name), "w") as f:
        for a, b in tuples:
            f.write(f"{a},{b},{b}\n")

if __name__ == '__main__':
    args = parse_args()
    main(args.cardinality, args.partitions, args.directory, args.skewed, args.randomize, args.skewness)
