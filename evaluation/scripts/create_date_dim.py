import pandas as pd
import argparse as ap
import os
import datetime

def parse_args():
    parser = ap.ArgumentParser()
    parser.add_argument("sales_path", type=str)
    parser.add_argument("--output", "-o", type=str, default="./resources/experiment_data")
    return parser.parse_args()

def generate_key(value):
    return datetime.date.fromisoformat(value).toordinal()

def main(sales_path, output_dir):
    sales = pd.read_csv(sales_path)
    sales["sold_date_sk"] = sales.sold_date.apply(generate_key)
    sales_file_name = sales_path.split("/")[-1]
    os.makedirs(output_dir, exist_ok=True)
    sales.to_csv(os.path.join(output_dir, sales_file_name), header=True, index=False)

    min_year = min(sales.sold_date)[:4]
    max_year = max(sales.sold_date)[:4]

    min_date = f"{min_year}-01-01"
    max_date = f"{max_year}-12-31"

    min_ordinal = datetime.date.fromisoformat(min_date).toordinal()
    max_ordinal = datetime.date.fromisoformat(max_date).toordinal()

    with open(os.path.join(output_dir, f"date_dim{sales_file_name[len('sales'):]}"), "w") as f:
        f.write("d_date_sk,d_date,d_year,d_moy,d_dom\n")
        for date_ordinal in range(min_ordinal, max_ordinal + 1):
            date = datetime.date.fromordinal(date_ordinal)
            f.write(f"{date_ordinal},{date.isoformat()},{date.year},{date.month},{date.day}\n")


if __name__ == '__main__':
    args = parse_args()
    main(args.sales_path, args.output)
