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
parser.add_argument("--time", "-t", type=int, default=60)
parser.add_argument("--port", "-p", type=int, default=5432)
parser.add_argument("--cores", type=int, default=28)
parser.add_argument("--clients", "-c", type=int, default=1)
parser.add_argument("--skip_warmup", action="store_true")
parser.add_argument("--skip_data_loading", action="store_true")
parser.add_argument("--explain", action="store_true")
parser.add_argument("--show", action="store_true")
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

def import_data():
    data_path = os.path.join(os.getcwd(), "resources/experiment_data")

    if args.dbms == "monetdb":
        load_command = """COPY INTO "{}" FROM '{}' USING DELIMITERS ',', '\n', '"' NULL AS '';"""
    elif args.dbms in ["hyrise", "hyrise-int"]:
        load_command = """COPY "{}" FROM '{}';"""
    elif args.dbms == "umbra":
        load_command = """COPY "{}" FROM '{}' WITH (FORMAT CSV, DELIMITER ',', NULL '', QUOTE '"');"""
    elif args.dbms == "greenplum":
        load_command = """COPY "{}" FROM '{}' WITH (FORMAT CSV, DELIMITER ',', NULL '', QUOTE '"');"""
    elif args.dbms == "duckdb":
        load_command = """COPY "{}" FROM '{}' WITH (FORMAT CSV, DELIMITER ',', NULL '', QUOTE '"');"""
    elif args.dbms == "postgres":
        load_command = """COPY "{}" FROM '{}' WITH (FORMAT CSV, DELIMITER ',', NULL '', QUOTE '"');"""

    connection, cursor = get_cursor()
    table_name_regex = re.compile(r'(?<=CREATE\sTABLE\s)"?\w+"?(?=\s*\()', flags=re.IGNORECASE)
    table_order = []
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
        table_file_path = f"{data_path}/{table_name}.csv"
        print(f" - ({t_id + 1}/{len(table_order)}) Import {table_name} from {table_file_path} ...", end=" ", flush=True)
        start = time.perf_counter()
        cursor.execute(load_command.format(table_name, table_file_path))
        end = time.perf_counter()
        print(f"({round(end - start, 1)} s)")

    cursor.close()
    if args.dbms in ["umbra", "greenplum", "postgres"]:
        connection.commit()
    connection.close()

def explain(queries):
    output_dir = f"db_comparison_results/plans/{args.dbms}"
    os.makedirs(output_dir, exist_ok=True)

    connection, cursor = get_cursor()
    for query_name, query in queries.items():
        cursor.execute("explain analyze " + query)
        with open(os.path.join(output_dir, f"plan_{query_name}.txt"), "w") as f:
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


def loop(thread_id, queries, query_id, start_time, successful_runs, timeout, is_warmup=False):
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
        else:
            break

    cursor.close()
    connection.close()


if not args.skip_data_loading:
    import_data()


print("Warming up database (complete single-threaded run): ", end="")
sys.stdout.flush()
loop(0, selected_benchmark_queries, "warmup", time.perf_counter(), [], 3600, True)
print(" done.")
sys.stdout.flush()

os.makedirs("db_comparison_results", exist_ok=True)

runtimes = {}
benchmark_queries = reversed(sorted(selected_benchmark_queries.keys()))

if args.explain:
    explain(selected_benchmark_queries)
    sys.exit()

if args.show:
    show(selected_benchmark_queries)
    sys.exit()

for query_name in benchmark_queries:
    print("Benchmarking {}...".format(query_name), end="", flush=True)

    successful_runs = []
    start_time = time.perf_counter()

    timeout = args.time

    threads = []
    for thread_id in range(0, args.clients):
        threads.append(
            threading.Thread(
                target=loop,
                args=(thread_id, selected_benchmark_queries, query_name, start_time, successful_runs, timeout),
            )
        )
        threads[-1].start()

    while True:
        time_left = start_time + timeout - time.perf_counter()
        if time_left < 0:
            break
        print("\rBenchmarking {}... {:.0f} seconds left".format(query_name, time_left), end="")
        time.sleep(1)

    while True:
        joined_threads = 0
        for thread_id in range(0, args.clients):
            if not threads[thread_id].is_alive():
                # print(f't{thread_id} finished')
                joined_threads += 1

        if joined_threads == args.clients:
            break
        else:
            print(
                "\rBenchmarking {}... waiting for {} more clients to finish".format(
                    query_name, args.clients - joined_threads
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

result_csv_filename = "db_comparison_results/database_comparison__{}.csv".format(args.dbms)

with open(result_csv_filename, "w") as result_csv:
    result_csv.write("DATABASE_SYSTEM,CORES,CLIENTS,ITEM_NAME,RUNTIME_MS\n")
    for item_name, runs in runtimes.items():
        for run in runs:
            result_csv.write(
                "{},{},{},{},{}\n".format(args.dbms, args.cores, args.clients, item_name, run)
            )
