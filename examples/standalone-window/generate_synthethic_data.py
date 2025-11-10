#!/usr/bin/env python3

import argparse as ap
import random
import os

import numpy as np
import scipy

def parse_args():
    parser = ap.ArgumentParser()
    parser.add_argument("--cardinality", "-c", type=int, default=10**6)
    parser.add_argument("--partitions", "-p", type=int, default=100)
    parser.add_argument("--directory", "-d", type=str, default="data")
    parser.add_argument("--skewed", "-s", action="store_true", default=False)
    return parser.parse_args()

def main(cardinality, partitions, directory, skewed):
    assert cardinality > partitions, "Cardinality must be larger than number of partitions"
    file_name = f"synthetic_{cardinality}-rows_{partitions}-partitions{'_skewed' if skewed else ''}.csv"

    if not skewed:
        rows_per_partition = [round(cardinality / partitions) for _ in range(partitions)]
    else:
        skewness = 1.3
        zipf_pdf = [k**-skewness/scipy.special.zeta(skewness) for k in range(1, partitions + 1)]
        norm = sum(zipf_pdf)
        probs = [p / norm for p in zipf_pdf]
        counts = [p * cardinality for p in probs]
        rows_per_partition = [int(i) for i in counts]
        init_count = sum(rows_per_partition)
        remaining_tuples = cardinality - init_count
        remainders = [(counts[i] % 1, i) for i in range(partitions)]
        remainders.sort(key=lambda x: x[0], reverse=True)
        while remaining_tuples > 0:
            rows_per_partition[remainders[0][1]] += 1
            remainders = remainders[1:]
            remaining_tuples -= 1

    tuples = []
    for partition, count in zip(range(partitions), rows_per_partition):
        for row in range(count):
            tuples.append((partition, row))

    random.Random(17).shuffle(tuples)

    os.makedirs(directory, exist_ok=True)
    with open(os.path.join(directory, file_name), "w") as f:
        for a, b in tuples:
            f.write(f"{a},{b},{b}\n")

if __name__ == '__main__':
    args = parse_args()
    main(args.cardinality, args.partitions, args.directory, args.skewed)
