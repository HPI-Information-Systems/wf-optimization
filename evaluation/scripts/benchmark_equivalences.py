#!/usr/bin/python3
# Thanks to Markus Dreseler, who initially built this script, and Martin Boissier, who extended it.

import argparse
import atexit
import csv
import datetime
import json
import os
import random
import re
import socket
import statistics
import struct
import subprocess
import sys
import threading
import time
from collections import defaultdict
from pathlib import Path

import pandas as pd


def load_queries(dir, blacklist={}):
    queries = {}
    for filename in Path(dir).glob("*.sql"):
        if not filename.is_file() or filename.name in blacklist:
            continue

        with open(str(filename), "r") as sql_file:
            sql_query = sql_file.read()
            queries[filename.stem] = sql_query.strip()
    return queries


parser = argparse.ArgumentParser()
parser.add_argument(
    "dbms",
    type=str,
    choices=["umbra", "duckdb"],
)
parser.add_argument("--time", "-t", type=float, default=60)
parser.add_argument("--port", "-p", type=int, default=5432)
parser.add_argument("--cores", type=int, default=28)
parser.add_argument("--runs", "-r", type=int, default=5)
parser.add_argument("--cold", action="store_true")
args = parser.parse_args()

def add_count(query):
    adapted_query = query.strip().replace(";", "")
    return f"SELECT count(*) FROM ({adapted_query})"

selected_benchmark_queries = load_queries("resources", {"schema.sql"})
# if args.dbms == "duckdb":
    # selected_benchmark_queries = {n: add_count(q) for n, q in selected_benchmark_queries.items()}

def get_cursor():
    if args.dbms == "umbra":
        connection = psycopg2.connect(host="127.0.0.1", user="postgres", password="postgres", port=args.port)
    elif args.dbms == "duckdb":
        connection = duckdb.connect(database="db.duckdb", read_only=False)

    cursor = connection.cursor()
    return (connection, cursor)


dbms_process = None

def cleanup():
    if args.dbms == "duckdb":
        _, cursor = get_cursor()
        # cursor.execute("PRAGMA enable_optimizer;")
    if dbms_process:
        print("Shutting {} down...".format(args.dbms))
        dbms_process.kill()
        time.sleep(10)


atexit.register(cleanup)

print("Starting {}...".format(args.dbms))

if args.dbms == "umbra":
    import psycopg2

    print("Make sure to start Umbra before by starting the Docker container")
    time.sleep(1)
elif args.dbms == "duckdb":
    import duckdb
    # TODO import from local build
    _, cursor = get_cursor()
    cursor.execute("PRAGMA enable_optimizer;")
else:
    raise AttributeError(f"Unknown DBMS: '{args.dbms}'")

def import_data(row_count_e, row_count_s, partitions_e, partitions_s, alpha):
    data_path = os.path.join(os.getcwd(), "resources/experiment_data")

    if args.dbms == "monetdb":
        load_command = """COPY INTO "{}" FROM '{}' USING DELIMITERS ',', '\n', '"' NULL AS '';"""
    elif args.dbms in ["hyrise", "hyrise-int"]:
        load_command = """COPY "{}" FROM '{}';"""
    elif args.dbms == "umbra":
        load_command = """COPY "{}" FROM '{}' WITH (FORMAT CSV, DELIMITER ',', NULL '', QUOTE '"', HEADER true);"""
    elif args.dbms == "greenplum":
        load_command = """COPY "{}" FROM '{}' WITH (FORMAT CSV, DELIMITER ',', NULL '', QUOTE '"');"""
    elif args.dbms == "duckdb":
        load_command = """COPY "{}" FROM '{}' WITH (FORMAT CSV, DELIMITER ',', NULL '', QUOTE '"', HEADER true);"""
    elif args.dbms == "postgres":
        load_command = """COPY "{}" FROM '{}' WITH (FORMAT CSV, DELIMITER ',', NULL '', QUOTE '"');"""

    connection, cursor = get_cursor()
    table_name_regex = re.compile(r'(?<=CREATE\sTABLE\s)"?\w+"?(?=\s*\()', flags=re.IGNORECASE)
    table_order = []
    if not args.cold:
        print("- Loading data ...")

    with open(f"resources/schema.sql") as f:
        for line in f:
            stripped_line = line.strip()
            if not stripped_line:
                continue
            table_name = table_name_regex.search(stripped_line).group().replace('"', "")
            table_order.append(table_name)
            cursor.execute(f'DROP TABLE IF EXISTS "{table_name}";')
            cursor.execute(stripped_line)

    for t_id, table_name in enumerate(table_order):
        num_rows = row_count_e if table_name == "employees" else row_count_s
        num_partitions = partitions_e if table_name == "employees" else partitions_s
        num_rows = {10**4: "10k", 10**5: "100k", 10**6: "1m", 10**7: "10m", 10**8: "100m"}[num_rows]
        table_file_path = f"{data_path}/{table_name}_p{num_partitions}_{num_rows}_a{alpha:.1f}_s42.csv"
        if not args.cold:
            print(f" - ({t_id + 1}/{len(table_order)}) Import {table_name} from {table_file_path} ...", end=" ", flush=True)
        start = time.perf_counter()
        cursor.execute(load_command.format(table_name, table_file_path))
        end = time.perf_counter()
        if not args.cold:
            print(f"({round(end - start, 1)} s)")

    cursor.close()
    if args.dbms in ["umbra", "greenplum", "postgres"]:
        connection.commit()
    connection.close()

def explain(queries, experiment, run_id):
    output_dir = f"db_comparison_results/plans/{args.dbms}"
    os.makedirs(output_dir, exist_ok=True)

    connection, cursor = get_cursor()
    for query_name, query in queries.items():
        cursor.execute("explain analyze " + query)
        with open(os.path.join(output_dir, f"plan_{query_name}_{experiment}_{run_id}.txt"), "w") as f:
            for line in cursor.fetchall():
                for t in line:
                    f.write(t)


def show(queries):
    output_dir = f"db_comparison_results/plans/{args.dbms}"
    os.makedirs(output_dir, exist_ok=True)

    connection, cursor = get_cursor()
    for query_name, query in queries.items():
        cursor.execute(query)
        with open(os.path.join(output_dir, f"result_{query_name}.txt"), "w") as f:
            for line in cursor.fetchall():
                f.write("\t".join(str(t) for t in line))
                f.write("\n")


def loop(queries, query_id, start_time, successful_runs, timeout, is_warmup=False):
    connection, cursor = get_cursor()
    # cursor.execute("PRAGMA disable_optimizer;")

    if is_warmup:
        if args.skip_warmup:
            return

        for q_id, query in sorted(queries.items(), key=lambda x: x[0]):
            try:
                cursor.execute(query)
                print("({})".format(q_id), end="", flush=True)
            except Exception as e:
                print(e)
                print(query)
                raise e

        cursor.close()
        connection.close()
        return

    while True:
        if query_id == "shuffled":
            items = list(queries.values())
            random.shuffle(items)
        else:
            items = [queries[query_id]]
        item_start_time = time.perf_counter()
        for query in items:
            cursor.execute(query)
            # cursor.fetchall()
        item_end_time = time.perf_counter()

        if (item_end_time - start_time < timeout) or len(successful_runs) == 0:
            successful_runs.append((item_end_time - item_start_time) * 1000)
            if args.cold:
                break
        else:
            break

    cursor.close()
    connection.close()

os.makedirs("db_comparison_results", exist_ok=True)

benchmark_queries = list(reversed(sorted(selected_benchmark_queries.keys())))

def run_experiment(experiment, run_id, force_new_file, benchmark_rows, benchmark_partitions, alpha):

    if not args.cold:
        import_data(benchmark_rows["employees"], benchmark_rows["sales"], benchmark_partitions["employees"], benchmark_partitions["sales"], alpha)
        explain(selected_benchmark_queries, experiment, run_id)

    runtimes = {}
    rows_per_item = {}
    partitions_per_item = {}
    for query_name in benchmark_queries:

        if not args.cold:
            # Warmup run
            print("Benchmarking {}...".format(query_name), end="", flush=True)
            connection, cursor = get_cursor()
            cursor.execute(selected_benchmark_queries[query_name])
            cursor.close()
            connection.close()
        else:
            import_data(benchmark_rows["employees"], benchmark_rows["sales"], benchmark_partitions["employees"], benchmark_partitions["sales"], alpha)

        successful_runs = []
        start_time = time.perf_counter()

        timeout = args.time
        max_rows = max([v for v in benchmark_rows.values()])
        if max_rows > 10**7:
            timeout = max(timeout, 120)

        thread = threading.Thread(
                    target=loop,
                    args=(selected_benchmark_queries, query_name, start_time, successful_runs, timeout),
                )
        thread.start()

        while True:
            time_left = start_time + timeout - time.perf_counter()
            if time_left < 0 or args.cold:
                break
            print("\rBenchmarking {}... {:.0f} seconds left".format(query_name, time_left), end="")
            time.sleep(1)

        while True:
            if not thread.is_alive():
                break

            else:
                print(
                    "\rBenchmarking {}... waiting for more client to finish".format(
                        query_name
                    ),
                    end="",
                )
                time.sleep(1)

        print("\r" + " " * 80, end="")
        item_runs = successful_runs
        print(
            "\r{}\t>>\t avg.: {:10.4f} ms\tmed.: {:10.4f} ms\tmin.: {:10.4f} ms\tmax.: {:10.4f} ms\tfinished {}".format(
                query_name,
                sum(item_runs) / len(item_runs) if len(item_runs) > 0 else 0,
                statistics.median(item_runs) if len(item_runs) > 0 else 0,
                min(item_runs) if len(item_runs) > 0 else 0,
                max(item_runs) if len(item_runs) > 0 else 0,
                str(datetime.datetime.now().time()),
            )
        )

        runtimes[query_name] = successful_runs
        on_employees = query_name.startswith("equiv6") or query_name.startswith("equiv2")
        table = "employees" if on_employees else "sales"
        rows_per_item[query_name] = benchmark_rows[table]
        partitions_per_item[query_name] = benchmark_partitions[table]


    result_csv_filename = f"db_comparison_results/database_comparison__{args.dbms}{'_cold' if args.cold else ''}.csv"
    with open(result_csv_filename, "w" if force_new_file else "a") as result_csv:
        if force_new_file:
            result_csv.write("DATABASE_SYSTEM,CORES,ITEM_NAME,ROWS,PARTITIONS,ALPHA,EXPERIMENT,RUNTIME_MS\n")
        for item_name, runs in runtimes.items():
            run_rows = rows_per_item[item_name]
            run_partitions = partitions_per_item[item_name]
            for run in runs:
                result_csv.write(f"{args.dbms},{args.cores},{item_name},{run_rows},{run_partitions},{alpha},{experiment},{run}\n")


alphas = [0.0, 0.5, 1.0, 1.5, 2.0]
base_rows = {"employees": 10**7, "sales": 10**6}
partitions = {"employees": [10, 100, 1000, 10000, 100000], "sales": [10, 50, 100, 200, 400]}
base_partitions = {"employees": 10, "sales": 50}
rows = [10**4, 10**5, 10**6, 10**7, 10**8]

# First experiment: Number of rows
print("EXPERIMENT 1: ROW COUNT")
for experiment_id, row_count in enumerate(rows):
    print()
    print(f"- {row_count} rows")
    if not args.cold:
        create_new_file = experiment_id == 0
        run_experiment("rows", experiment_id, create_new_file, {"employees": row_count, "sales": row_count}, base_partitions, 0.0)
    else:
        my_start = time.perf_counter()
        executions = 0
        while time.perf_counter() - my_start < args.time or executions < args.runs:
            create_new_file = experiment_id == 0 and executions == 0
            run_experiment("rows", experiment_id, create_new_file, {"employees": row_count, "sales": row_count}, base_partitions, 0.0)
            executions += 1

# Second experiment: Number of partitions
print("\n\nEXPERIMENT 2: PARTITION COUNT")
assert len(partitions["employees"]) == len(partitions["sales"])
for experiment_id in range(len(partitions["employees"])):
    partitions_e = partitions["employees"][experiment_id]
    partitions_s = partitions["sales"][experiment_id]
    print()
    print(f"- {partitions_e}/{partitions_s} partitions")
    if not args.cold:
        run_experiment("partitions", experiment_id, False, base_rows, {"employees": partitions_e, "sales": partitions_s}, 0.0)
    else:
        my_start = time.perf_counter()
        executions = 0
        while time.perf_counter() - my_start < args.time or executions < args.runs:
            run_experiment("partitions", experiment_id, False, base_rows, {"employees": partitions_e, "sales": partitions_s}, 0.0)
            executions += 1


# Third experiment: Skewness
print("\n\nEXPERIMENT 3: SKEWNESS")
for experiment_id, alpha in enumerate(alphas):
    print()
    print(f"- Alpha {alpha}")
    if not args.cold:
        run_experiment("alpha", experiment_id, False, base_rows, {"employees": 1000, "sales": 50}, alpha)
    else:
        my_start = time.perf_counter()
        executions = 0
        while time.perf_counter() - my_start < args.time or executions < args.runs:
            run_experiment("alpha", experiment_id, False, base_rows, {"employees": 1000, "sales": 50}, alpha)
            executions += 1
