#!/usr/bin/env python3

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
    parser.add_argument("--data", "-d", type=str, default="./db_comparison_results")
    parser.add_argument("--output", "-o", type=str, default="./figures")
    parser.add_argument("--cold", action="store_true")
    return parser.parse_args()

def extract_equivalence(value):
    regex = re.compile(r"(?<=equiv)\d+(?=_)")
    return int(regex.search(value).group(0))

def parse_data(data_dir, systems, cold):
    data = []

    for system in systems:
        file_name = os.path.join(data_dir, f"database_comparison__{system.lower()}{'_cold' if cold else ''}.csv")
        print(f'Load {file_name}')
        df = pd.read_csv(file_name)
        group_columns = [c for c in df.columns if c != "RUNTIME_MS"]
        print(f'Process: Group')
        df = df.groupby(group_columns).RUNTIME_MS.median().reset_index()
        print(f'Process: Equivalence')
        df["EQUIVALENCE"] = df.ITEM_NAME.apply(extract_equivalence)
        print(f'Process: System')
        df["SYSTEM"] = system
        print(f'Process: Config')
        df["CONFIGURATION"] = df.SYSTEM + "_" + df.EXPERIMENT
        data.append(df)

    return pd.concat(data)


def get_palette():
    return [Safe_5.hex_colors[0], Safe_5.hex_colors[3]]


def format_number(n):
    if n >= 10 or n == 0:
        return f"{int(n):,.0f}".replace(",", r"\thinspace")
    return f"{float(n):,.1f}".replace(",", r"\thinspace")



def get_tick_label(pos, x_labels, param, query):
    value = x_labels[pos]
    on_employees = query in [2, 6]
    default_values = {
        ("employees", "rows"): 10**7,
        ("sales", "rows"): 10**6,
        ("employees", "partitions"): 10,
        ("sales", "partitions"): 50,
        ("employees", "alpha"): 0.0,
        ("sales", "alpha"): 0.0,
    }
    is_default = default_values[("employees" if on_employees else "sales", param)] == value
    if param == "alpha" or param == "partitions" and not on_employees:
        return f"$\\mathbf{{{value}}}$" if is_default else f"${value}$"
    l = math.log(value, 10)

    label = f"10^{{{round(l)}}}"
    return f"$\\mathbf{{{label}}}$" if is_default else f"${label}$"


def get_xlabel(system, config, query):
    if config == "rows":
        return r"\# Rows"

    if config == "alpha":
        return r"Zipf $\alpha$" + f"\nEquivalence {query}"

    return r"\# Partitions"


def plot_data(data, **kwargs):
    sns.set_theme(style="white")
    ax = plt.gca()
    param = data.EXPERIMENT.unique()[0]
    # query = data["query"].unique()[0]
    param_values = list(data[param.upper()].unique())
    x_count = len(param_values)
    x_centers = np.arange(x_count)
    offsets = [-0.25, 0.25]
    # offsets = [0, 1]
    t_offsets = [-0.4, 0.025]
    bar_width = 0.4
    assert len(data.EXPERIMENT.unique()) == 1
    assert len(data.EQUIVALENCE.unique()) == 1
    system = data.SYSTEM.unique()[0]
    equivalence = data.EQUIVALENCE.unique()[0]

    c_i = data.columns.get_loc(param.upper())
    y_max = max(data.RUNTIME_MS)
    lim = y_max
    runner_up = data.sort_values(by="RUNTIME_MS").RUNTIME_MS.iat[-2]
    if runner_up * 3 < lim:
        lim = runner_up * 1.5
    lim *= 1.05

    if len(data) > 0:
        vals = defaultdict(list)
        for version, offset, t_offset, col in zip(["DuckDB", "Umbra"], offsets, t_offsets, get_palette()):
            positions = [x + offset for x in x_centers]
            t_positions = [x + t_offset for x in x_centers]
            d = data[data.SYSTEM == version].sort_values(by=param.upper())
            ax.bar(positions, d.RUNTIME_MS, zorder=3, width=bar_width, linewidth=0, color=col)
            for r in d.itertuples():
                val = r.RUNTIME_MS
                y_val = min(val, lim)
                too_small = y_val <= lim * 0.15
                y_pos = y_val  + (lim * 0.01) if too_small else y_val - (lim * 0.01)
                color = "black" if too_small or version == "DuckDB" else "white"
                va = "bottom" if too_small else "top"
                x = r[c_i + 1]
                x_pos = positions[param_values.index(x)]
                vals[version].append(val)
                # val = round(val, 0 if val >= 10 else 1)
                label = format_number(val)
                if val > lim:
                    label += r"\thinspace$\ast$"
                ax.text(x_pos, y_pos, label, rotation=90, va=va, ha="center", fontsize=4.5*2, color=color)

        xes = list(sorted(data[param.upper()]))
        for old, new, x_center in zip(vals["before"], vals["after"], x_centers):
            speedup = old / new
            if round(speedup) < 30:
                continue

            center = new + (old - new) / 2
            x = x_center + offsets[1]
            factor = format_number(round(speedup))
            print(equivalence, param, speedup, xes[x_center])

            ax.annotate(
                f"$\\times$\\thinspace{factor}",
                xy=(x, old),
                xycoords="data",
                xytext=(x, center),
                textcoords="data",
                arrowprops=dict(arrowstyle="->", fc="0.6", ec="black", shrinkA=0),
                ha="center",
                va="center",
                size=4.5 * 2,
                rotation=90,
                color="black",
            )
            ax.annotate(
                "",
                xy=(x, new),
                xycoords="data",
                xytext=(x, center),
                textcoords="data",
                arrowprops=dict(arrowstyle="-", fc="0.6", ec="black", shrinkA=10, shrinkB=15),
                ha="center",
                va="center",
                size=4.5 * 2,
                color="black",
            )

            line_offset = bar_width * 0.45
            ax.plot([x - line_offset, x + line_offset], [old, old], color="black", lw=1)

    ax.set_ylim((0, lim))

    for spine in ax.spines.values():
        spine.set_visible(True)
        spine.set_edgecolor('black')
        spine.set_zorder(4)

    if equivalence == 1:
        ax.set_ylabel("Runtime [ms]", fontsize=7*2)

    ax.yaxis.set_major_formatter(FuncFormatter(lambda x, _: format_number(x)))
    ax.set_title(None)
    ax.grid(which="major", axis="y", visible=True, zorder=0)
    ax.tick_params(axis="both", which="major", labelsize=6 * 2, width=1, length=6, bottom=True, left=True)
    ax.set_xlabel(get_xlabel(system, param, equivalence), fontsize=7 * 2)
    ax.xaxis.set_major_locator(FixedLocator(x_centers))
    ax.xaxis.set_major_formatter(FuncFormatter(lambda x, _: get_tick_label(x, param_values, param, equivalence)))


def main(data_dir, output_dir, cold):

    print("Load data")

    databases = ["Umbra", "DuckDB"]
    data = parse_data(data_dir, databases, cold)

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

    configs = list(data.EXPERIMENT.unique())
    order = ["rows", "partitions", "alpha"]
    data = data[data.ITEM_NAME.str.contains("_before")]
    g = sns.FacetGrid(data, row="EXPERIMENT", col="EQUIVALENCE", sharey=False, sharex=False, row_order=order)
    g.map_dataframe(plot_data)

    handles = []
    for stage, col in zip(["DuckDB", "Umbra"], get_palette()):
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
    fig_width = page_width * 2 #0.475 * 2 * 3
    fig_height = 2 * column_width
    fig.set_size_inches(fig_width, fig_height)

    plt.tight_layout(pad=0)
    g.fig.subplots_adjust(hspace=0.4)
    g.fig.subplots_adjust(wspace=0.4)

    os.makedirs(output_dir, exist_ok=True)
    g.savefig(os.path.join(output_dir, f"equivalences_comparison{'_cold' if cold else ''}.pdf"), dpi=300, bbox_inches="tight", pad_inches=0.01)


if __name__ == '__main__':
    args = parse_args()
    main(args.data, args.output, args.cold)
