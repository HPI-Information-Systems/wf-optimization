#!/usr/bin/env python3.11

import argparse as ap
import json
import os
import re
from collections import defaultdict

import matplotlib as mpl
import matplotlib.pyplot as plt
import numpy as np
import pandas as pd
import seaborn as sns
import scipy
from matplotlib.ticker import FixedLocator, FuncFormatter
from palettable.cartocolors.qualitative import Safe_10


def parse_args():
    parser = ap.ArgumentParser()
    parser.add_argument("--data", "-d", type=str, default="./experiments")
    parser.add_argument("--output", "-o", type=str, default="./figures")
    parser.add_argument("--st", action="store_true", default=False)
    parser.add_argument("--skewed", action="store_true", default=False)
    parser.add_argument("--speedup", action="store_true", default=False)
    return parser.parse_args()


def generate_dist(size):
    alphas = [11] + list(range(12, 41, 2))
    alphas = [a / 10 for a in alphas]
    partition_counts = {}
    for a in alphas:
        rand = np.random.default_rng(1717)
        distribution = rand.zipf(a, size=size)
        rows_per_partition = defaultdict(int)
        for i in distribution:
            rows_per_partition[i] += 1
        rows_per_partition = [c for c in rows_per_partition.values()]
        rows_per_partition.sort(reverse=True)
        partition_counts[len(rows_per_partition)] = a
    return partition_counts


def format_number(n):
    return f"{int(n):,.0f}".replace(",", r"\thinspace") if n % 1 == 0 else f"{round(n, 1):,.1f}".replace(",", r"\thinspace")


def plot_data(data, skewed=False, order=[], speedup=False, **kwargs):
    sns.set_theme(style="white")
    configs = data.Configuration.unique()
    plot_order = list(reversed([c for c in order if c in configs]))
    config_count = len(configs)
    palette = list(reversed(Safe_10.hex_colors[:config_count]))

    markers = ["^", "X", "s", "D", "v", "o", "P", "*"]
    markers = list(reversed(markers[:config_count]))
    y = "RUNTIME_MS" if not speedup else "Speedup"

    ax = plt.gca()
    x = "PARTITION_COUNT" if not skewed else "Skew"
    sns.lineplot(
        data,
        x=x,
        y=y,
        hue="Configuration",
        style="Configuration",
        markers=markers,
        palette=palette,
        hue_order=plot_order,
        style_order=plot_order,
        markersize=7,
        dashes=False
    )
    y_ticks = sorted(list(data.PARTITION_COUNT.unique())) if not skewed else sorted(list(data.Rows.unique()))
    if not skewed:
        ax.set_xscale("log")
    if not speedup:
        ax.yaxis.set_major_formatter(FuncFormatter(lambda x, _: format_number(x)))
        base_max = max(data[data.Configuration == "Baseline"].RUNTIME_MS)
        y_min = max(0, ax.get_ylim()[0])
        cap = 3 * base_max
        y_max = min(ax.get_ylim()[1], cap)
        y_min = 0
        ax.set_ylim((y_min, y_max))
        if y_max == cap:
            largers = data[data.RUNTIME_MS >= cap]
            largers = largers[[x, "Configuration", "RUNTIME_MS"]]
            for row in largers.itertuples(index=False):
                color = palette[plot_order.index(row.Configuration)]
                ax.text(row[0], cap * 0.99, r"$\ast$", va="top", ha="center", color=color, fontsize=8*2)
    else:
        y_max = max(data.Speedup)
        ax.set_ylim((0.75, 1.6))


    if not skewed:
        ax.xaxis.set_major_locator(FixedLocator(y_ticks))

    label = r"\# Partitions" if not skewed else r"Skew factor $\alpha$"
    ax.set_xlabel(label, fontsize=8 * 2)
    ax.tick_params(axis="both", which="major", labelsize=7 * 2, width=1, length=6, bottom=True, left=True)
    ax.tick_params(axis="both", which="minor", bottom=False, left=False)
    ax.grid(which="major", axis="y", visible=True)
    for spine in ax.spines.values():
        spine.set_visible(True)
        spine.set_edgecolor('black')

    mode = data.Mode.unique()[0]
    rows = data.Rows.unique()[0]
    if skewed:
        ax.set_title(mode, fontsize=7*2)
    else:
        ax.set_title(f"{format_number(rows)} Rows ({mode})", fontsize=7*2)


def get_name(conf):
    names = {
        "Baseline": "Baseline",
        "Filter": "Co-Evaluation",
        "EarlyOut": "Co-Evaluation + Stop",
        "ShrinkRuns": "Merge Pruning",
        "ShrinkPartitions": "Partition Pruning",
        "ShrinkPartitionsAdaptive": "Adaptive Part. Pruning (chunk)",
        "ShrinkPartitionsAdaptiveContext": "Adaptive Part. Prun.",
        "Combined": "Combined",
    }
    return names[conf]


def main(input_dir, output_dir, skewed, speedup):
    mpl.use("pgf")

    plt.rcParams.update(
        {
            #"legend.loc": "upper center",
            "legend.fancybox": False,
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

    csv_files = [f for f in os.listdir(input_dir) if f.endswith(".csv")]
    files = [f for f in csv_files if f.startswith("microbenchmark_")]
    files = [f for f in files if not "level" in f]
    files = [f for f in  files if skewed == ("_skewed" in f)]
    data = []

    for input_file in files:
        raw_data = pd.read_csv(os.path.join(input_dir, input_file))
        raw_data["Mode"] = "MT" if "_mt" in input_file else "ST"
        raw_data["RUNTIME_MS"] = raw_data["RUNTIME_NS"] / 10**6
        raw_data.rename(columns={'CONFIGURATION': 'Configuration'}, inplace=True)
        raw_data = raw_data[["Configuration", "ROW_COUNT", "PARTITION_COUNT", "Mode", "RUNTIME_MS"]]
        data.append(raw_data)

    runtimes = pd.concat(data)
    runtimes = runtimes[runtimes.Configuration != "Filter"]
    skew_factors = None
    if skewed:
        assert(len(runtimes.ROW_COUNT.unique()) == 1)
        row_count = runtimes.ROW_COUNT.unique()[0]
        skew_factors = generate_dist(row_count)
        runtimes["Skew"] = [skew_factors[n] for n in runtimes.PARTITION_COUNT]

    configs = runtimes.Configuration.unique()
    if not skewed:
        runtimes = runtimes.groupby(by=["ROW_COUNT", "PARTITION_COUNT", "Configuration", "Mode"]).RUNTIME_MS.median().reset_index()
    else:
        runtimes = runtimes.groupby(by=["ROW_COUNT", "Skew", "Configuration", "Mode"]).RUNTIME_MS.median().reset_index()

    baselines = runtimes[runtimes.Configuration == "Baseline"].copy()
    baselines.rename(columns={'RUNTIME_MS' : "BASE_MS"}, inplace=True)
    key = "PARTITION_COUNT" if not skewed else "Skew"
    baselines = baselines[["ROW_COUNT", key, "Mode", "BASE_MS"]]
    runtimes = runtimes.merge(baselines, on=["ROW_COUNT", key, "Mode"], how="inner")
    runtimes["Speedup"] = runtimes["BASE_MS"] / runtimes["RUNTIME_MS"]

    row_counts = sorted(runtimes.ROW_COUNT.unique())
    plot_count = len(row_counts) if not skewed else len(runtimes.Mode.unique())
    order = [
        "Baseline",
        "Combined",
        "Filter",
        "EarlyOut",
        "ShrinkRuns",
        "ShrinkPartitions",
        "ShrinkPartitionsAdaptive",
        "ShrinkPartitionsAdaptiveContext"
    ]
    order = [c for c in order if c in configs]
    config_count = len(configs)

    palette = reversed(Safe_10.hex_colors[:config_count])
    markers = ["^", "X", "s", "D", ".", "o", "v", "P"][:config_count]
    runtimes.rename(columns={'ROW_COUNT': 'Rows'}, inplace=True)


    g = None
    if not skewed:
        g = sns.FacetGrid(runtimes, col="Rows", row="Mode", sharey=False, sharex=skewed)
    else:
        g = sns.FacetGrid(runtimes, col="Mode", sharey=False, sharex=False,)
    g.map_dataframe(plot_data, skewed=skewed, order=order, speedup=speedup)

    col_count = len(configs) if not skewed else 3
    bbox = (0.5, 1.07) if not skewed else (0.5, 1.2)
    legend_names = {get_name(c): v  for c, v in g._legend_data.items()}
    legend_order = [c for c in order if c != "Combined"] + ["Combined"]
    l_order = [get_name(c) for c in legend_order]
    g.add_legend(
        ncol=col_count,
        loc="upper center",
        fancybox=False,
        framealpha=1.0,
        edgecolor="black",
        fontsize=7*2,
        bbox_to_anchor=bbox,
        label_order=l_order,
        legend_data=legend_names,
        handlelength=1.5,
        columnspacing=1,
        labelspacing=0.25,
        handletextpad=0.4,
    )

    label = "Speedup" if speedup else "Runtime [ms]"
    for i, ax in enumerate(g.axes.flat):
        if (i == 0 or (i > 1 and plot_count % i == 0)):
            ax.set_ylabel(label, fontsize=8 * 2)
        else:
            ax.set_ylabel("")

    fig = plt.gcf()
    column_width = 3.3374
    page_width = 7.00697
    fig_width = column_width * 2
    fig_width = 2 * (column_width if skewed else page_width)
    fig_height = column_width if skewed else 2 * column_width
    fig.set_size_inches(fig_width, fig_height)

    plt.tight_layout(pad=0)
    if not skewed:
        g.fig.subplots_adjust(hspace=0.35)
    else:
        g.fig.subplots_adjust(wspace=0.2)

    os.makedirs(output_dir, exist_ok=True)
    runtimes.to_csv(os.path.join(output_dir, f"speedups_{'skewed' if skewed else 'unskewed'}.csv"), index=False)
    output_file = f"microbench_all_{'skewed' if skewed else 'unskewed'}{'_speedup' if speedup else ''}.pdf"
    g.savefig(os.path.join(output_dir, output_file), dpi=300, bbox_inches="tight", pad_inches=0.01)


if __name__ == "__main__":
    args = parse_args()
    main(args.data, args.output, args.skewed, args.speedup)
