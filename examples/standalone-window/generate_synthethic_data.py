#!/usr/bin/env python3

import argparse as ap
import random
import os
from collections import defaultdict

import numpy as np
import scipy

def parse_args():
    parser = ap.ArgumentParser()
    parser.add_argument("--cardinality", "-c", type=int, default=10**6)
    parser.add_argument("--partitions", "-p", type=int, default=100)
    parser.add_argument("--directory", "-d", type=str, default="data")
    parser.add_argument("--skewed", "-s", action="store_true", default=False)
    parser.add_argument("--randomize", "-r", action="store_true", default=False)
    return parser.parse_args()

def main(cardinality, partitions, directory, skewed, randomize):
    if not skewed:
        assert cardinality > partitions, "Cardinality must be larger than number of partitions"
        rows_per_partition = [round(cardinality / partitions) for _ in range(partitions)]
    else:
        skewness = 1.2

        rand = np.random.default_rng(1717)
        distribution = rand.zipf(skewness, size=cardinality)
        rows_per_partition = defaultdict(int)
        for i in distribution:
            rows_per_partition[i] += 1
        rows_per_partition = [c for c in rows_per_partition.values()]
        rows_per_partition.sort(reverse=True)
        partitions = len(rows_per_partition)
        # print(partitions)
        # print(rows_per_partition[:10])
        # print([i / cardinality for i in rows_per_partition[:10]])
        # print(rows_per_partition[-10:])
        # return

        # print("PDF")
        # zipf_pdf = [cardinality*(k**-skewness)/scipy.special.zeta(skewness) for k in range(1, partitions + 1)]
        # norm = sum(zipf_pdf)
        # print("probs")
        # probs = [p / norm for p in zipf_pdf]
        # print("counts")
        # counts = [p * cardinality for p in probs]
        # rows_per_partition = [int(i) for i in counts]
        # init_count = sum(rows_per_partition)
        # remaining_tuples = cardinality - init_count
        # print(remaining_tuples, partitions, remaining_tuples / partitions)
        # print("remainders")
        # remainders = [(counts[i] % 1, i) for i in range(partitions)]
        # print("sort")
        # remainders.sort(key=lambda x: x[0], reverse=True)
        # print("distribute")
        # for _, offset in remainders:
        #     if remaining_tuples == 0:
        #         break
        #     rows_per_partition[offset] += 1
        #     remaining_tuples -= 1
        # print(sum(rows_per_partition))
        # print(rows_per_partition[:10])
        # print(rows_per_partition[-10:])
        # print(sum([1 for i in rows_per_partition if i > 0]))
        # print(sum([1 for i in rows_per_partition if i > 1]))

    partition_values = range(partitions)
    if randomize:
        partition_values = random.Random(17).sample(range(2**30, 2**31), k=partitions)

    print(partition_values)


    tuples = []
    for partition, count in zip(partition_values, rows_per_partition):
        for row in range(count):
            tuples.append((partition, row))

    random.Random(17).shuffle(tuples)

    os.makedirs(directory, exist_ok=True)
    file_name = f"synthetic_{cardinality}-rows_{partitions}-partitions{'_skewed' if skewed else ''}{'_randomized' if randomize else ''}.csv"
    with open(os.path.join(directory, file_name), "w") as f:
        for a, b in tuples:
            f.write(f"{a},{b},{b}\n")

if __name__ == '__main__':
    args = parse_args()
    main(args.cardinality, args.partitions, args.directory, args.skewed, args.randomize)
