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


def parse_data(data_dir, file_name):
    data = pd.read_csv(os.path.join(data_dir, file_name))
    meta_info = re.match(r"paper(?P<query>q\d)_by_(?P<param>\w+)_(?P<version>\w+)\.data", file_name)
    data["query"] = meta_info.group("query")
    data["param"] = "rows" if meta_info.group("param") == "size" else meta_info.group("param")
    data["version"] = meta_info.group("version")
    data["runtime"] = data["time"] * 1000
    return data


def get_palette():
    return [Safe_5.hex_colors[0], Safe_5.hex_colors[4]]


def format_number(n):
    return f"{int(n):,.0f}".replace(",", r"\thinspace") if n % 1 == 0 else f"{round(n, 1):,.1f}".replace(",", r"\thinspace")


def get_label(pos, x_labels, param, query):
    value = x_labels[pos]
    if param == "alpha" or param == "partitions" and query == "q2":
        return value
    l = math.log(value, 10)

    return f"$10^{{{int(l)}}}$"

def get_xlabel(config, query):
    if config == "rows":
        return r"\# Rows"

    if config == "alpha":
        return r"Zipf $\alpha$"

    return r"\# Partitions" + "\n" + query.upper() + f" (Equivalence {6 if query == 'q1' else 8})"


def plot_data(data, **kwargs):
    sns.set_theme(style="white")
    ax = plt.gca()
    param = data.param.unique()[0]
    query = data["query"].unique()[0]
    param_values = list(data[param].unique())
    x_count = len(param_values)
    x_centers = np.arange(x_count)
    offsets = [-0.2, 0.2]
    t_offsets = [-0.4, 0.025]

    y_limit = 60 if query == "q1" else 200
    ax.set_ylim((0, y_limit))

    c_i = data.columns.get_loc(param)
    vals = defaultdict(list)
    for config, offset, t_offset, col in zip(["before", "after"], offsets, t_offsets, get_palette()):
        positions = [x + offset for x in x_centers]
        t_positions = [x + t_offset for x in x_centers]
        d = data[data.version == config].sort_values(by=param)
        ax.bar(positions, d.runtime, zorder=3, width=0.35, linewidth=0, color=col)
        for r in d.itertuples():
            vals[config].append(r.runtime)
        for r in d[d.runtime >= y_limit].itertuples():
            x = r[c_i + 1]
            x_pos = t_positions[param_values.index(x)]
            ax.text(x_pos, y_limit * 1.001, format_number(round(r.runtime)), rotation=60, va="bottom", ha="left", fontsize=6*2)
    print(query, param, [round(old / new, 1) for old, new in zip(vals["before"], vals["after"])])


    for spine in ax.spines.values():
        spine.set_visible(True)
        spine.set_edgecolor('black')
        spine.set_zorder(4)

    if param == "rows":
        ax.set_ylabel("Runtime [ms]", fontsize=8*2)
    ax.set_title(None)
    ax.grid(which="major", axis="y", visible=True, zorder=0)
    ax.tick_params(axis="both", which="major", labelsize=7 * 2, width=1, length=6, bottom=True, left=True)
    ax.set_xlabel(get_xlabel(param, query), fontsize=8 * 2)
    ax.xaxis.set_major_locator(FixedLocator(x_centers))
    ax.xaxis.set_major_formatter(FuncFormatter(lambda x, _: get_label(x, data[param].unique(), param, query)))


def main(data_dir, output_dir):
    data = [parse_data(data_dir, f) for f in os.listdir(data_dir) if f.endswith(".data")]
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

    g = sns.FacetGrid(data, row="query", col="param", sharey=False, sharex=False, row_order=["q1", "q2"], col_order=["rows", "partitions", "alpha"])

    g.map_dataframe(plot_data)
    #plt.gcf().subplots_adjust()
    handles = []
    for stage, col in zip(["Native optimizations", "With our optimizations"], get_palette()):
        handles.append(mpatches.Patch(color=col, label=stage))
    g.add_legend(
        handles=handles,
        ncol=2,
        loc="center",
        fancybox=False,
        fontsize=7*2,
        bbox_to_anchor=[0.5, 1.02],
    )

    fig = plt.gcf()
    column_width = 3.3374
    page_width = 7.00697
    fig_width = column_width * 2 #0.475 * 2 * 3
    fig_height = 2 * column_width
    fig.set_size_inches(fig_width, fig_height)

    plt.tight_layout(pad=0)
    g.fig.subplots_adjust(hspace=0.6)
    g.fig.subplots_adjust(wspace=0.3)

    os.makedirs(output_dir, exist_ok=True)
    g.savefig(os.path.join(output_dir, "rewrites.pdf"), dpi=300, bbox_inches="tight", pad_inches=0.01)


if __name__ == '__main__':
    args = parse_args()
    main(args.data, args.output)
