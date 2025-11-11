#!/usr/bin/env python3.11

import argparse as ap
import json
import os
import re

import matplotlib as mpl
import matplotlib.pyplot as plt
import numpy as np
import pandas as pd
import seaborn as sns
from matplotlib.ticker import FixedLocator, FuncFormatter
from palettable.cartocolors.qualitative import Safe_10


def parse_args():
    parser = ap.ArgumentParser()
    parser.add_argument("input_file", type=str)
    return parser.parse_args()


def format_number(n):
    if n < 1:
        return str(round(n, 1))
    return f"{int(n):,.0f}".replace(",", r"\thinspace") if n % 1 == 0 else str(n)


def to_s(lst):
    return [x / 10**9 for x in lst]


def get_old_new_latencies(old_path, new_path):
    with open(old_path) as old_file:
        old_data = json.load(old_file)

    with open(new_path) as new_file:
        new_data = json.load(new_file)

    if old_data["context"]["benchmark_mode"] != new_data["context"]["benchmark_mode"]:
        exit("Benchmark runs with different modes (ordered/shuffled) are not comparable")

    old_latencies = list()
    new_latencies = list()

    for old, new in zip(old_data["benchmarks"], new_data["benchmarks"]):
        # Create numpy arrays for old/new successful/unsuccessful runs from benchmark dictionary
        old_successful_durations = np.array([run["duration"] for run in old["successful_runs"]], dtype=np.float64)
        new_successful_durations = np.array([run["duration"] for run in new["successful_runs"]], dtype=np.float64)
        # np.mean() defaults to np.float64 for int input
        old_latencies.append(np.mean(old_successful_durations))
        new_latencies.append(np.mean(new_successful_durations))

    return old_latencies, new_latencies


def get_trend(old, new):
    diff = new / old
    if diff <= 0.95:
        return "better"
    elif diff >= 1.05:
        return "worse"
    return "same"


def main(input_file):
    sns.set_theme(style="white")

    mpl.use("pgf")

    plt.rcParams.update(
        {
            "font.family": "serif",  # use serif/main font for text elements
            "text.usetex": True,  # use inline math for ticks
            "pgf.rcfonts": False,  # don't setup fonts from rc parameters
            "pgf.preamble": r"""\usepackage{iftex}
  \ifxetex
    \usepackage[libertine]{newtxmath}
    \usepackage[tt=false]{libertine}
    \setmonofont[StylisticSet=3]{inconsolata}
  \else
    \ifluatex
      \usepackage[libertine]{newtxmath}
      \usepackage[tt=false]{libertine}
      \setmonofont[StylisticSet=3]{inconsolata}
    \else
       \usepackage[tt=false, type1=true]{libertine}
       \usepackage[varqu]{zi4}
       \usepackage[libertine]{newtxmath}
    \fi
  \fi""",
        }
    )

    row_count = int(re.search(r"\d+(?=-rows)", input_file).group())
    st = "_st" in input_file
    skewed = "_skewed" in input_file

    runtimes = pd.read_csv(input_file)


    base_palette = Safe_10.hex_colors
    runtimes["RUNTIME_MS"] = runtimes["RUNTIME_NS"] / 10**6
    runtimes.rename(columns={'CONFIGURATION': 'Configuration'}, inplace=True)
    print(runtimes.groupby(by=["PARTITION_COUNT", "Configuration"]).RUNTIME_MS.min())
    runtimes = runtimes.groupby(by=["PARTITION_COUNT", "Configuration"]).RUNTIME_MS.median().reset_index()
    print(runtimes)
    # print(runtimes.describe())
    print(runtimes.PARTITION_COUNT.unique())
    order = ["Default", "Filter", "EarlyOut", "SkipSort"][:len(runtimes.Configuration.unique())]
    sns.lineplot(data=runtimes, x="PARTITION_COUNT", y="RUNTIME_MS", hue="Configuration", style="Configuration", palette=base_palette[:3], markers=["^", "X", "s"], hue_order=order, style_order=order, markersize=8)


    #sns.scatterplot(data=values, x="old", y="new", palette=colors, hue="trend", s=80, legend=False)

    ax = plt.gca()

    ax.set_xscale("log")
    # if scale != "symlog":
    #     ax.set_yscale(scale)
    #     ax.set_xscale(scale)
    # else:
    #     ax.set_yscale("symlog", linthresh=0.1)
    #     ax.set_xscale("symlog", linthresh=0.1)

    # print(
    #     benchmark,
    #     round((1 - sum(new_latencies) / sum(old_latencies)) * 100),
    #     "% improvement,",
    #     len(values[values.trend == "better"]),
    #     "better,",
    #     len(values[values.trend == "worse"]),
    #     "worse",
    # )

    # min_lim = min(ax.get_ylim()[0], ax.get_xlim()[0])
    # max_lim = max(ax.get_ylim()[1], ax.get_xlim()[1])
    # if scale == "symlog":
    #     max_lim *= 1.3
    # if scale != "log":
    #     min_lim = 0

    # possible_ticks_below_one = [10 ** (-exp) for exp in reversed(range(1, 4))]
    # possible_minor_ticks_below_one = [x / 100 for x in range(1, 10)] + [x / 10 for x in range(1, 10)]
    # possible_ticks_above_one = [1, 10]
    # possible_minor_ticks_above_one = list(range(2, 10))
    # if scale == "symlog":
    #     possible_ticks_below_one = [0, 0.1]
    # elif scale == "linear":
    #     possible_ticks_below_one = [0]
    #     possible_ticks_above_one = list(range(1, 11))
    #     possible_minor_ticks_below_one = []
    #     possible_minor_ticks_above_one = []
    # ticks = list()
    # minor_ticks = list()
    # for tick in possible_ticks_below_one + possible_ticks_above_one:
    #     if tick >= min_lim and tick <= max_lim:
    #         ticks.append(tick)

    # for tick in possible_minor_ticks_below_one + possible_minor_ticks_above_one:
    #     if tick >= min_lim and tick <= max_lim:
    #         minor_ticks.append(tick)

    # ax.set_ylim(min_lim, max_lim)
    # ax.set_xlim(min_lim, max_lim)

    y_ticks = sorted(list(runtimes.PARTITION_COUNT.unique()))
    # ax.xaxis.set_major_locator(FixedLocator(ticks))
    ax.xaxis.set_major_locator(FixedLocator(y_ticks))
    #ax.xaxis.set_major_formatter(FuncFormatter(lambda x, _: format_number(x)))
    ax.yaxis.set_major_formatter(FuncFormatter(lambda x, _: format_number(x)))
    # ax.xaxis.set_minor_locator(FixedLocator(minor_ticks))
    # ax.yaxis.set_minor_locator(FixedLocator(minor_ticks))
    plt.ylabel("Runtime [ms]", fontsize=8 * 2)
    plt.xlabel(r"\# Partitions", fontsize=8 * 2)
    ax.tick_params(axis="both", which="major", labelsize=7 * 2, width=1, length=6, bottom=True, left=True)
    # ax.tick_params(axis="both", which="minor", labelsize=7 * 2, width=0.5, length=4, bottom=True, left=True)
    # plt.grid(which="minor", axis="y", visible=True, linewidth=0.5, alpha=0.5)
    plt.grid(which="major", axis="y", visible=True)
    # plt.grid(which="major", axis="x", visible=True)
    plt.legend(loc="best", fancybox=False, framealpha=1.0, edgecolor="black")

    fig = plt.gcf()
    column_width = 3.3374
    fig_width = column_width * 0.475 * 2
    fig.set_size_inches(fig_width, fig_width)
    #ax.set_aspect(1)
    plt.tight_layout(pad=0)

    output_file = f"microbench_{row_count}_{'st' if st else 'mt'}{'_skewed' if skewed else ''}.pdf"
    plt.savefig(os.path.join(".", output_file), dpi=300, bbox_inches="tight", pad_inches=0.01)
    plt.close()


if __name__ == "__main__":
    args = parse_args()
    main(args.input_file)
