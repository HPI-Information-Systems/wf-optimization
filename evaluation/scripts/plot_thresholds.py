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
from palettable.cartocolors.qualitative import Safe_10, Bold_10, Vivid_10
from scipy.stats import gmean


def parse_args():
    parser = ap.ArgumentParser()
    parser.add_argument("--data", "-d", type=str, default="./experiments")
    parser.add_argument("--output", "-o", type=str, default="./figures")
    parser.add_argument("--st", action="store_true", default=False)
    parser.add_argument("--skewed", action="store_true", default=False)
    parser.add_argument("--speedup", action="store_true", default=False)
    return parser.parse_args()


def format_number(n):
    return f"{int(n):,.0f}".replace(",", r"\thinspace") if n % 1 == 0 else f"{round(n, 1):,.1f}".replace(",", r"\thinspace")


def get_name(conf):
    if conf == "Baseline":
        return "Baseline"

    if conf == "ShrinkPartitions":
        return "Always"

    parts = conf.split("_")
    return round(1 / float(parts[1]))
    name = "Adapt. "
    if "Context" in parts[0]:
        name += "(worker) "
    else:
        name += "(chunk) "

    return name + parts[1]

def get_simple_name(confs):
    names = {
        "Baseline": "Baseline",
        "Filter": "Co-Evaluation",
        "EarlyOut": "Co-Evaluation w/ Early Stop",
        "ShrinkRuns": "Prune Merge",
        "ShrinkPartitions": "Prune Partition",
        "ShrinkPartitionsAdaptive": "Adaptive Prune Part. (chunk)",
        "ShrinkPartitionsAdaptiveContext": "Adaptive Prune Part."
    }
    return [names[conf] for conf in confs]


def log_lineplot(data, skewed = False, speedup=False, marks=None, pal=None, order=None, **kwargs):
    sns.set_theme(style="white")

    plot_order = [c for c in order if c in data.Conf.unique()]

    ax = plt.gca()
    x = "PARTITION_COUNT" if not skewed else "Rows"
    y = "RUNTIME_MS" if not speedup else "Speedup"
    sns.lineplot(
        data,
        x=x,
        y=y,
        hue="Conf",
        markers=marks,
        style="Conf",
        palette=pal,
        hue_order=plot_order,
        style_order=plot_order,
        markersize=7,
        dashes=False
    )
    y_ticks = sorted(list(data.PARTITION_COUNT.unique())) if not skewed else sorted(list(data.Rows.unique()))
    ax.set_xscale("log")
    if skewed and not speedup:
        ax.set_yscale("log")

    ax.xaxis.set_major_locator(FixedLocator(y_ticks))
    ax.yaxis.set_major_formatter(FuncFormatter(lambda x, _: format_number(x)))
    #plt.ylabel("Runtime [ms]", fontsize=8 * 2)
    label = r"\# Partitions" if not skewed else r"\# Rows"
    ax.set_xlabel(label, fontsize=7 * 2)
    ax.tick_params(axis="both", which="major", labelsize=7 * 2, width=1, length=6, bottom=True, left=True)
    # ax.tick_params(axis="both", which="minor", labelsize=7 * 2, width=0.5, length=4, bottom=True, left=True)
    # plt.grid(which="minor", axis="y", visible=True, linewidth=0.5, alpha=0.5)
    ax.grid(which="major", axis="y", visible=True)
    for spine in ax.spines.values():
        spine.set_edgecolor('black')
        spine.set_visible(True)
    if not speedup and False:
        y_max = 2 * max(data[data.Configuration == "Baseline"].RUNTIME_MS)
        y_max = min(y_max, ax.get_ylim()[1])
        ax.set_ylim((0, y_max))
    mode = data.Mode.unique()[0]
    rows = data.Rows.unique()[0]
    ax.set_title(f"{format_number(rows)} Rows ({mode})", fontsize=7*2)



def main(input_dir, output_dir, skewed, speedup):
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

    files = [f for f in os.listdir(input_dir) if f.endswith(".csv") and f.startswith("adaptivity_thresholds_")]
    files = [f for f in  files if skewed == ("_skewed" in f)]

    data = []
    for input_file in files:
        raw_data = pd.read_csv(os.path.join(data_dir, input_file))
        raw_data["Mode"] = "MT" if "mt.csv" in input_file else "ST"
        raw_data.rename(columns={'CONFIGURATION': 'Configuration'}, inplace=True)
        raw_data["Ind"] = raw_data["Configuration"].str.contains("Adaptive")

        raw_data["Conf"] = np.where(raw_data["Configuration"].str.contains("Adaptive"), raw_data['Configuration'] + "_" +  raw_data['THRESHOLD'].astype(str), raw_data["Configuration"])
        data.append(raw_data)
    runtimes = pd.concat(data)


    runtimes = runtimes.groupby(by=["ROW_COUNT", "PARTITION_COUNT", "Conf", "THRESHOLD", "Configuration", "Mode"]).RUNTIME_MS.median().reset_index()


    configs = runtimes.Conf.unique()
    for metric in ["geomean", "mean"]:
        if metric == "mean":
            overview = runtimes.groupby(by=["Configuration", "THRESHOLD"]).RUNTIME_MS.mean().reset_index()
        else:
            overview = runtimes.groupby(by=["Configuration", "THRESHOLD"]).RUNTIME_MS.aggregate(gmean).reset_index()
        overview = overview[overview.Configuration == "ShrinkPartitionsAdaptiveContext"]
        overview = overview[overview.THRESHOLD < 1.1]

        print(metric.upper())
        print(min(overview.RUNTIME_MS), max(overview.RUNTIME_MS))

        if metric == "mean":
            overview.sort_values(by="RUNTIME_MS", inplace=True)
            print(overview)

        overview["Configuration"] = get_simple_name(overview["Configuration"])
        overview["Thresh_rev"] = 1 / overview["THRESHOLD"]

        order = sorted(overview.Configuration.unique(), reverse=True)
        markers = list(reversed(["^", "X", "s", "D", "o", "P", "v", "*"][:len(order)]))
        palette = list(reversed(Safe_10.hex_colors[:len(order)]))
        sns.lineplot(
            overview,
            x="Thresh_rev",
            y="RUNTIME_MS",
            hue="Configuration",
            markers=markers,
            style="Configuration",
            palette=palette,
            hue_order=order,
            style_order=order,
            markersize=7,
            dashes=False
        )
        fig = plt.gcf()
        column_width = 3.3374
        page_width = 7.00697
        fig.set_size_inches(2 * column_width * 0.45, 2 * column_width * 0.45)
        ax = plt.gca()
        for spine in ax.spines.values():
            spine.set_edgecolor('black')
            spine.set_visible(True)

        ax.set_xlabel(r"Threshold $\delta$", fontsize=8 * 2)
        label = "Average Runtime [ms]" if metric == "mean" else f"Geometric Mean Runtime [ms]"

        ax.set_ylabel(label, fontsize=8 * 2)
        ax.tick_params(axis="both", which="major", labelsize=7 * 2, width=1, length=6, bottom=True, left=True)
        y_max = overview.RUNTIME_MS.max()
        ax.set_ylim((0, y_max * 1.05))
        plt.tight_layout(pad=0)

        ax.get_legend().remove()
        os.makedirs(output_dir, exist_ok=True)
        output_file = f"thresholds_{metric}_{'skewed' if skewed else 'unskewed'}.pdf"
        plt.savefig(os.path.join(output_dir, output_file), dpi=300, bbox_inches="tight", pad_inches=0.01)
        plt.close()

    runtimes = runtimes[
        (runtimes.Conf.str.contains("AdaptiveContext")  & (runtimes.THRESHOLD < 0.3) & (runtimes.THRESHOLD > 0.125))
    ]

    configs = runtimes.Conf.unique()
    order = list(reversed(sorted(configs)))
    config_count = len(configs)
    palette = Safe_10.hex_colors
    palette.extend(Bold_10.hex_colors)
    palette.extend(Vivid_10.hex_colors)
    palette = list(reversed(palette[:config_count]))
    markers = ["^", "X", "s", "D", "o", "P", "v", "*"] * 2
    markers = list(reversed(markers[:config_count]))
    runtimes.rename(columns={'ROW_COUNT': 'Rows'}, inplace=True)
    markers = {c: m for c, m in zip(order, markers)}
    palette = {c: p for c, p in zip(order, palette)}

    g = None
    if not skewed:
        g = sns.FacetGrid(runtimes, col="Rows", row="Mode", sharey=False, sharex=False)
    else:
        g = sns.FacetGrid(runtimes, col="Mode", sharey=False, sharex=False)
    g.map_dataframe(log_lineplot, skewed=skewed, speedup=speedup, marks=markers, pal=palette, order=order)

    col_count = len(runtimes.Conf.unique()) # 4 if skewed else 7
    bbox = (0.5, 1.075) if not skewed else (0.5, 1.2)
    legend_names = {get_name(c): v  for c, v in g._legend_data.items()}

    g.add_legend(
        legend_data=legend_names,
        ncol=col_count,
        loc="upper center",
        fancybox=False,
        framealpha=1.0,
        edgecolor="black",
        fontsize=7*2,
        bbox_to_anchor=bbox,
        columnspacing=1,
        labelspacing=0.25,
        handletextpad=0.4,
        handlelength=1.4
    )

    fig = plt.gcf()
    fig_width = column_width * 2 #0.475 * 2 * 3
    fig_width = 2 * (column_width if skewed else page_width)
    fig_height = column_width if skewed else 2 * column_width
    fig.set_size_inches(fig_width, fig_height)

    y_label = "Runtime [ms]" if not speedup else "Speedup"
    for i, ax in enumerate(g.axes.flat):
        if (i == 0 or (i > 1 and plot_count % i == 0)):
            ax.set_ylabel(y_label, fontsize=7 * 2)
        else:
            ax.set_ylabel("", fontsize=0)

    plt.tight_layout(pad=0)
    g.fig.subplots_adjust(hspace=0.35)

    os.makedirs(output_dir, exist_ok=True)
    output_file = f"thresholds_{'skewed' if skewed else 'unskewed'}.pdf"
    g.savefig(os.path.join(output_dir, output_file), dpi=300, bbox_inches="tight", pad_inches=0.01)
    plt.close()


if __name__ == "__main__":
    args = parse_args()
    main(args.data, args.output, args.skewed, args.speedup)
