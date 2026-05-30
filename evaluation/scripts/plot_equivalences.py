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
    parser.add_argument("--data", "-d", type=str, default="./db_comparison_results")
    parser.add_argument("--output", "-o", type=str, default="./figures")
    parser.add_argument("--cold", action="store_true")
    return parser.parse_args()


# def parse_data(data_dir, file_name):
#     data = pd.read_csv(os.path.join(data_dir, file_name))
#     meta_info = re.match(r"paper(?P<query>q\d)_by_(?P<param>\w+)_(?P<version>\w+)\.data", file_name)
#     data["query"] = meta_info.group("query")
#     data["param"] = "rows" if meta_info.group("param") == "size" else meta_info.group("param")
#     data["version"] = meta_info.group("version")
#     data["runtime"] = data["time"] * 1000
#     return data


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
    return [Safe_5.hex_colors[0], Safe_5.hex_colors[4]]


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


def plot_data(data, overall_max=None, **kwargs):
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
    assert len(data.CONFIGURATION.unique()) == 1
    assert len(data.EQUIVALENCE.unique()) == 1
    experiment = data.CONFIGURATION.unique()[0]
    experiment_ = data.EXPERIMENT.unique()[0]

    # y_limit = 60 if query == "q1" else 200


    system = data.SYSTEM.unique()[0]
    equivalence = data.EQUIVALENCE.unique()[0]
    # print(experiment, system, param, overall_max[experiment], max(data.RUNTIME_MS), len(data), equivalence)

    c_i = data.columns.get_loc(param.upper())
    lim = overall_max[experiment_] * 1.05
    y_max = max(data.RUNTIME_MS)
    lim = y_max
    if len(data) > 0:
        vals = defaultdict(list)
        for version, offset, t_offset, col in zip(["before", "after"], offsets, t_offsets, get_palette()):
            positions = [x + offset for x in x_centers]
            t_positions = [x + t_offset for x in x_centers]
            d = data[data.ITEM_NAME.str.contains(version)].sort_values(by=param.upper())
            # print(d)
            ax.bar(positions, d.RUNTIME_MS, zorder=3, width=bar_width, linewidth=0, color=col)
            # for r in d.itertuples():
            #     vals[config].append(r.runtime)
            for r in d.itertuples():
                val = r.RUNTIME_MS
                y_val = val # min(r.RUNTIME_MS, lim)
                too_small = y_val <= lim * 0.15
                y_pos = y_val  + (lim * 0.01) if too_small else y_val - (lim * 0.01)
                color = "black" if too_small or version == "before" else "white"
                va = "bottom" if too_small else "top"
                x = r[c_i + 1]
                x_pos = positions[param_values.index(x)]
                vals[version].append(val)
                # val = round(val, 0 if val >= 10 else 1)
                ax.text(x_pos, y_pos, format_number(val), rotation=90, va=va, ha="center", fontsize=4.5*2, color=color)

        # print(equivalence, param, max([round(old / new, 1) for old, new in zip(vals["before"], vals["after"])]))
        xes = list(sorted(data[param.upper()]))
        # print(xes)
        speedups = [old / new for old, new in  zip(vals["before"], vals["after"])]
        max_speedup = max(speedups)
        for old, new, x_center, speedup in zip(vals["before"], vals["after"], x_centers, speedups):
            if speedup != max_speedup or speedup < 1.1:
                continue

            center = new + (old - new) / 2
            x = x_center + offsets[1]
            factor = f"{float(speedup):,.1f}".replace(",", r"\thinspace")
            print(equivalence, param, speedup, xes[x_center], old, new)
            too_small = new <= lim * 0.15

            s = math.ceil(math.log(round(new), 10))
            of = ax.get_ylim()[1] / 10

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
                xy=(x, new + (of if too_small else of * 0.25)),
                xycoords="data",
                xytext=(x, center - (of * 1.25)),
                textcoords="data",
                arrowprops=dict(arrowstyle="-", fc="0.6", ec="black", shrinkA=0, shrinkB=0),
                ha="center",
                va="center",
                size=4.5 * 2,
                color="black",
            )

            line_offset = bar_width * 0.45
            ax.plot([x - line_offset, x + line_offset], [old, old], color="black", lw=1)




        # for config, offset, col in zip(["before", "after"], offsets, get_palette()):
        #     d = data[data.ITEM_NAME.str.contains(config)]
        #     ax.bar(offset, d.RUNTIME_MS, zorder=3, width=0.4, linewidth=0, color=col)

    # ax.set_ylim((0, lim))
    # ax.set_ylim((0, None))

    for spine in ax.spines.values():
        spine.set_visible(True)
        spine.set_edgecolor('black')
        spine.set_zorder(4)

    # ax.set_xlim()
    if equivalence == 1:
        # ax.set_ylabel(f"{experiment}\nRuntime [ms]", fontsize=8*2)
        ax.set_ylabel("Runtime [ms]", fontsize=7*2)
        # ax.yaxis.set_major_formatter(FuncFormatter(lambda x, _: format_number(x)))
    # else:
        # ax.set_yticklabels([])
    ax.yaxis.set_major_formatter(FuncFormatter(lambda x, _: format_number(x)))
    ax.set_title(None)
    ax.grid(which="major", axis="y", visible=True, zorder=0)


    # if partitions == 10000:
    #     ax.tick_params(axis="both", which="major", labelsize=7 * 2, width=1, length=6, bottom=False, left=True)
    # else:
    ax.tick_params(axis="both", which="major", labelsize=6 * 2, width=1, length=6, bottom=True, left=True)
    ax.set_xlabel(get_xlabel(system, param, equivalence), fontsize=7 * 2)

    # ax.set_xticks([])
    ax.xaxis.set_major_locator(FixedLocator(x_centers))
    ax.xaxis.set_major_formatter(FuncFormatter(lambda x, _: get_tick_label(x, param_values, param, equivalence)))

    # if ax.get_ylim()[1] > 1000:
    #     ax.set_yticklabels(ax.get_yticklabels(), rotation=25, ha='right')


def extract_facet_key(value):
    systems_order = ["DuckDB", "Umbra"]
    experiments_order = ["rows", "partitions", "alpha"]
    parts = value.split("_")

    return 10 * experiments_order.index(parts[1]) + systems_order.index(parts[0])

def main(data_dir, output_dir, cold):

    print("Load data")

    databases = ["Umbra", "DuckDB"][1:]
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

    print(data.head())

    configs = list(data.CONFIGURATION.unique())
    order = list(sorted(configs, key=extract_facet_key))
    order = ["rows", "partitions", "alpha"]
    max_values = defaultdict(int)
    for config in configs:
        max_values[config] = max(data[data.CONFIGURATION == config].RUNTIME_MS)

    print(max_values)

    for system in databases:
    # overall_max = data.groupby("SYSTEM").RUNTIME_MS.max().to_dict()
        system_data = data[data.SYSTEM == system]
        percentiles = defaultdict(float)

        for experiment in system_data.EXPERIMENT.unique():
            values = system_data[(system_data.EXPERIMENT == experiment) & (system_data.ITEM_NAME.str.contains("after"))].sort_values(by="RUNTIME_MS").RUNTIME_MS
            experiment_percentiles = np.percentile(values, [50, 60, 2/3, 70, 75, 80, 90])
            percentiles[experiment] = experiment_percentiles[-1]
            experiment_percentiles = [values.iat[math.ceil(len(values) * p) - 1] for p in [0.5, 0.6, 2/3, 0.7, 0.9]]
            percentiles[experiment] = max(percentiles[experiment], experiment_percentiles[-1])

            # print(system, experiment, max(values), percentiles)

        print(percentiles)

        g = sns.FacetGrid(system_data, row="EXPERIMENT", col="EQUIVALENCE", sharey=False, sharex=False, row_order=order) #, row_order=["DuckDB", "Umbra"]) #, col_order=["rows", "partitions", "alpha"])

        g.map_dataframe(plot_data, overall_max=percentiles)
        #plt.gcf().subplots_adjust()
        handles = []
        for stage, col in zip(["Left-Hand Side", "Right-Hand Side"], get_palette()):
            handles.append(mpatches.Patch(color=col, label=stage))
        g.add_legend(
            handles=handles,
            ncol=2,
            loc="center",
            fancybox=False,
            fontsize=7*2,
            bbox_to_anchor=[0.5, 1.02],
        )

        # palette = get_palette()
        # handles = []
        # for stage, col in zip(["Partition", "Sort", "Merge", "Materialize", "Frame and Aggregate"], palette):
        #     handles.append(mpatches.Patch(color=col, label=stage))


        fig = plt.gcf()
        # fig.legend(
        #     handles=handles,
        #     ncol=5,
        #     frameon=False,
        #     bbox_to_anchor=[0.5, 1.03],
        #     loc="center",
        #     fontsize=6*2,
        #     columnspacing=1,
        #     labelspacing=0.25,
        #     handlelength=1.5,
        #     handletextpad=0.4
        # )
        column_width = 3.3374
        page_width = 7.00697
        fig_width = page_width * 2 #0.475 * 2 * 3
        fig_height = 2 * column_width
        fig.set_size_inches(fig_width, fig_height)

        plt.tight_layout(pad=0)
        g.fig.subplots_adjust(hspace=0.4)
        g.fig.subplots_adjust(wspace=0.35)

        os.makedirs(output_dir, exist_ok=True)
        g.savefig(os.path.join(output_dir, f"equivalences_{system}{'_cold' if cold else ''}.pdf"), dpi=300, bbox_inches="tight", pad_inches=0.01)


if __name__ == '__main__':
    args = parse_args()
    main(args.data, args.output, args.cold)
