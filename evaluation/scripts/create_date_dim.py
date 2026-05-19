import pandas as pd
import argparse as ap
import os

def parse_args():
    parser = ap.ArgumentParser()
    parser.add_argument("sales_path", type=str)
    parser.add_argument("--output", "-o", type=str, default="./data")
    return parser.parse_args()


def main(sales_path, output_dir):
    dates = dict()

    with open(os.path.join(output_dir, "date_dim.csv"), "w") as f:
        # f.write("d_date_sk,d_date,d_year,d_moy,d_dom\n")
        for date in pd.date_range(start="1970-01-01", end="2025-12-31"):
            f.write(f"{date.date().toordinal()},{date.date().isoformat()},{date.year},{date.month},{date.day}\n")
            dates[date.date().isoformat()] = date.date().toordinal()

    sales = pd.read_csv(sales_path)
    sold_date_sk = [dates[date] for date in sales.sold_date]
    sales["sold_date_sk"] = sold_date_sk
    sales.to_csv(sales_path.replace(".csv", "_sk.csv"), header=False, index=False)


if __name__ == '__main__':
    args = parse_args()
    main(args.sales_path, args.output)
