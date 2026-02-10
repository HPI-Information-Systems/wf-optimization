#!/usr/bin/env python3.11

import argparse as ap
import json
import os
import re
import colorsys
import math
from collections import defaultdict

import matplotlib as mpl
import matplotlib.pyplot as plt
import numpy as np
import pandas as pd
import seaborn as sns
import scipy
import matplotlib.patches as mpatches
from matplotlib.ticker import FixedLocator, FuncFormatter
from palettable.cartocolors.qualitative import Safe_5


def parse_args():
    parser = ap.ArgumentParser()
    parser.add_argument("--data", "-d", type=str, default="./experiments")
    parser.add_argument("--output", "-o", type=str, default="./figures")
    return parser.parse_args()


def parse_data(data_dir, partitions, config):

    file_name = f"pipeline_durations_{partitions}_partitions_{config.lower()}.txt"
    stages = {
        "PARTITION": "Partition",
        "LOCAL SORT": "Sort",
        "MERGE": "Merge",
        "CREATE CHUNKS FROM GLOBAL RADIX PARTITIONS": "Materialize",
        "FRAMING AND FUNCTION COMPUTATION": "Frame and Aggregate"
    }

    runtimes = defaultdict(list)
    runtimes_re = re.compile(r"(?P<start>\d+)\t(?P<end>\d+)")

    current_stage = None
    min_start = None
    with open(os.path.join(data_dir, file_name)) as f:
        for line in f:
            stripped = line.strip()
            if stripped in stages:
                current_stage = stages[stripped]
                continue
            match = runtimes_re.match(stripped)
            if not match:
                continue

            start = int(match.group("start"))
            end = int(match.group("end"))
            min_start = start if min_start is None else min(start, min_start)
            runtimes[current_stage].append((start, end))


    data = defaultdict(list)
    for stage in runtimes:
        data["start"] += [(start - min_start) / 10**6 for start, _ in runtimes[stage]]
        data["end"] += [(end - min_start) / 10**6 for _, end in runtimes[stage]]
        data["duration"] += [(end - start) / 10**6 for start, end in runtimes[stage]]
        data["stage"] += [stage] * len(runtimes[stage])
        data["partitions"] += [partitions] * len(runtimes[stage])
        data["config"] += [config] * len(runtimes[stage])

    return pd.DataFrame(data=data)


def get_palette():
    return list(reversed(["#08293a", "#12597d", "#1b89c0", "#44afe4", "#88ccee"]))
    return list(reversed(["#1c124a", "#332187", "#4b30c5", "#7c67da", "#b1a5e9"]))
    return list(reversed(["#24175e", "#332187", "#432bb0", "#593ed0", "#9f90e4"]))


def format_number(n):
    return f"$10^{{{int(math.log(n, 10))}}}$"


def get_label(pos, y_labels):
    if pos == min(y_labels):
        return "Combined"
    return "Baseline"


def plot_data(data, **kwargs):
    sns.set_theme(style="white")
    ax = plt.gca()
    palette = get_palette()

    # y_center = data[data.config == "Baseline"].stage.value_counts().max() / 2
    val_count = data[data.config == "Baseline"].stage.value_counts().max() / 2
    y_centers = []
    partitions = data.partitions.unique()[0]
    for stack, config in enumerate(["Combined", "Baseline"]):

        y_center = (val_count * 2 ) * stack + val_count
        y_centers.append(y_center)

        ax.grid(which="major", axis="x", visible=True, zorder=0)
        for stage, col in zip(["Partition", "Sort", "Merge", "Materialize", "Frame and Aggregate"], palette):
            d = data[(data.stage == stage) & (data.config == config)]
            y_positions = np.arange(len(d))
            offset = len(y_positions) / 2
            for r, y in zip(d.itertuples(), y_positions):
                ax.barh(y - offset + y_center, r.duration, color=col, left=r.start, label=stage, zorder=3, linewidth=0)

        ax.text(d.end.max() + 2, y_center,  f"{round(d.end.max(), 1)}", fontsize=7*2, va="center", ha="left")

    ax.set_ylabel(f"{format_number(partitions)} Part.", fontsize=7 * 2, rotation='horizontal', va="center")
    ax.set_xlim((0, 65))
    ax.set_title(None)

    if partitions == 10000:
        ax.tick_params(axis="both", which="major", labelsize=7 * 2, width=1, length=6, bottom=False, left=True)
    else:
        ax.tick_params(axis="both", which="major", labelsize=7 * 2, width=1, length=6, bottom=True, left=True)
        ax.set_xlabel(f"Runtime [ms]", fontsize=7 * 2)

    ax.yaxis.set_major_locator(FixedLocator(y_centers))
    ax.yaxis.set_major_formatter(FuncFormatter(lambda x, _: get_label(x, y_centers)))

    for spine in ax.spines.values():
        spine.set_visible(True)
        spine.set_edgecolor('black')
        spine.set_zorder(4)


def main(data_dir, output_dir):
    data = [
        parse_data(data_dir, 100, "Baseline"),
        parse_data(data_dir, 100, "Combined"),
        parse_data(data_dir, 10000, "Baseline"),
        parse_data(data_dir, 10000, "Combined"),
    ]
    data = pd.concat(data)

    mpl.use("pgf")

    plt.rcParams.update(
        {
            #"legend.loc": "upper center",
            "legend.fancybox": False,
            "font.family": "serif",  # use serif/main font for text elements
            "text.usetex": True,  # use inline math for ticks
            "pgf.rcfonts": False,  # don't setup fonts from rc parameters
            "pgf.texsystem" : "pdflatex",
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

    g = sns.FacetGrid(data, row="partitions", sharey=True, sharex=True, row_order=[10000, 100])

    g.map_dataframe(plot_data)

    palette = get_palette()
    handles = []
    for stage, col in zip(["Partition", "Sort", "Merge", "Materialize", "Frame and Aggregate"], palette):
        handles.append(mpatches.Patch(color=col, label=stage))
    fig = plt.gcf()
    fig.legend(
        handles=handles,
        ncol=5,
        frameon=False,
        bbox_to_anchor=[0.5, 1.03],
        loc="center",
        fontsize=6*2,
        columnspacing=1,
        labelspacing=0.25,
        handlelength=1.5,
        handletextpad=0.4
    )
    column_width = 3.3374
    page_width = 7.00697
    fig_width = column_width * 2 #0.475 * 2 * 3
    fig_height = 1.1 * column_width
    fig.set_size_inches(fig_width, fig_height)

    plt.tight_layout(pad=0)
    g.fig.subplots_adjust(hspace=0.05)
    os.makedirs(output_dir, exist_ok=True)
    plt.savefig(os.path.join(output_dir, "pipeline.pdf"), dpi=300, bbox_inches="tight", pad_inches=0.01)


if __name__ == '__main__':
    args = parse_args()
    main(args.data, args.output)
