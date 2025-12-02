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
    return f"{int(n):,.0f}".replace(",", r"\thinspace") if n % 1 == 0 else f"{round(n, 1):,.1f}".replace(",", r"\thinspace")

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

    st = "_st" in input_file
    assert "_skewed" in input_file
    runtimes = pd.read_csv(input_file)


    base_palette = Safe_10.hex_colors
    runtimes["RUNTIME_MS"] = runtimes["RUNTIME_NS"] / 10**6
    runtimes.rename(columns={'CONFIGURATION': 'Configuration'}, inplace=True)
    print("MIN")
    print(runtimes.groupby(by=["ROW_COUNT", "Configuration"]).RUNTIME_MS.min())
    print("MEAN")
    print(runtimes.groupby(by=["ROW_COUNT", "Configuration"]).RUNTIME_MS.mean())
    print("MEDIAN")
    print(runtimes.groupby(by=["ROW_COUNT", "Configuration"]).RUNTIME_MS.median())

    runtimes = runtimes.groupby(by=["ROW_COUNT", "Configuration"]).RUNTIME_MS.median().reset_index()

    print(runtimes)

    # print(runtimes.describe())
    print(runtimes.ROW_COUNT.unique())
    order = ["Default", "Filter", "EarlyOut", "SkipSort"][:len(runtimes.Configuration.unique())]
    sns.lineplot(data=runtimes, x="ROW_COUNT", y="RUNTIME_MS", hue="Configuration", style="Configuration", palette=base_palette[:3], markers=["^", "X", "s"], hue_order=order, style_order=order, markersize=8)


    #sns.scatterplot(data=values, x="old", y="new", palette=colors, hue="trend", s=80, legend=False)

    ax = plt.gca()

    ax.set_xscale("log")
    ax.set_yscale("log")
    x_ticks = sorted(list(runtimes.ROW_COUNT.unique()))
    # ax.xaxis.set_major_locator(FixedLocator(ticks))
    ax.xaxis.set_major_locator(FixedLocator(x_ticks))
    #ax.xaxis.set_major_formatter(FuncFormatter(lambda x, _: format_number(x)))
    # ax.yaxis.set_major_formatter(FuncFormatter(lambda x, _: format_number(x)))
    # ax.xaxis.set_minor_locator(FixedLocator(minor_ticks))
    # ax.yaxis.set_minor_locator(FixedLocator(minor_ticks))
    plt.ylabel("Runtime [ms]", fontsize=8 * 2)
    plt.xlabel(r"\# Rows", fontsize=8 * 2)
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

    output_file = f"microbench_skewed_{'st' if st else 'mt'}.pdf"
    plt.savefig(os.path.join(".", output_file), dpi=300, bbox_inches="tight", pad_inches=0.01)
    plt.close()


if __name__ == "__main__":
    args = parse_args()
    main(args.input_file)
