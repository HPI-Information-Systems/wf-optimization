#!/usr/bin/env python3

import argparse as ap
import random
import os

def parse_args():
    parser = ap.ArgumentParser()
    parser.add_argument("--cardinality", "-c", type=int, default=10**6)
    parser.add_argument("--partitions", "-p", type=int, default=100)
    parser.add_argument("--directory", "-d", type=str, default="data")
    return parser.parse_args()

def main(cardinality, partitions, directory):
    file_name = os.path.join(directory, f"synthetic_{cardinality}-rows_{partitions}-partitions.csv")
    rows_per_partition = round(cardinality / partitions)

    tuples = []
    for partition in range(partitions):
        for row in range(rows_per_partition):
            tuples.append((partition, row))

    random.Random(17).shuffle(tuples)

    with open(file_name, "w") as f:
        for a, b in tuples:
            f.write(f"{a},{b},{b}\n")

if __name__ == '__main__':
    args = parse_args()
    main(args.cardinality, args.partitions, args.directory)
