#!/bin/bash

cd evaluation

echo "Generate data: 10000 rows"
python3 scripts/generate_synthetic_data.py -c 10000 -p 10
python3 scripts/generate_synthetic_data.py -c 10000 -p 100
python3 scripts/generate_synthetic_data.py -c 10000 -p 1000

echo "Generate data: 100000 rows"
python3 scripts/generate_synthetic_data.py -c 100000 -p 10
python3 scripts/generate_synthetic_data.py -c 100000 -p 100
python3 scripts/generate_synthetic_data.py -c 100000 -p 1000
python3 scripts/generate_synthetic_data.py -c 100000 -p 10000

echo "Generate data: 1M rows"
python3 scripts/generate_synthetic_data.py -c 1000000 -p 10
python3 scripts/generate_synthetic_data.py -c 1000000 -p 100
python3 scripts/generate_synthetic_data.py -c 1000000 -p 1000
python3 scripts/generate_synthetic_data.py -c 1000000 -p 10000
python3 scripts/generate_synthetic_data.py -c 1000000 -p 100000

echo "Generate data: 10M rows"
python3 scripts/generate_synthetic_data.py -c 10000000 -p 10
python3 scripts/generate_synthetic_data.py -c 10000000 -p 100
python3 scripts/generate_synthetic_data.py -c 10000000 -p 1000
python3 scripts/generate_synthetic_data.py -c 10000000 -p 10000
python3 scripts/generate_synthetic_data.py -c 10000000 -p 100000
python3 scripts/generate_synthetic_data.py -c 10000000 -p 1000000

echo "Generate data: 100M rows"
python3 scripts/generate_synthetic_data.py -c 100000000 -p 10
python3 scripts/generate_synthetic_data.py -c 100000000 -p 100
python3 scripts/generate_synthetic_data.py -c 100000000 -p 1000
python3 scripts/generate_synthetic_data.py -c 100000000 -p 10000
python3 scripts/generate_synthetic_data.py -c 100000000 -p 100000
python3 scripts/generate_synthetic_data.py -c 100000000 -p 1000000
python3 scripts/generate_synthetic_data.py -c 100000000 -p 10000000

echo "Generate data: Skewed"
python3 scripts/generate_synthetic_data.py --skewed -c 1000000 --skewness 1.1
python3 scripts/generate_synthetic_data.py --skewed -c 1000000 --skewness 1.2
python3 scripts/generate_synthetic_data.py --skewed -c 1000000 --skewness 1.4
python3 scripts/generate_synthetic_data.py --skewed -c 1000000 --skewness 1.6
python3 scripts/generate_synthetic_data.py --skewed -c 1000000 --skewness 1.8
python3 scripts/generate_synthetic_data.py --skewed -c 1000000 --skewness 2.0
python3 scripts/generate_synthetic_data.py --skewed -c 1000000 --skewness 2.2
python3 scripts/generate_synthetic_data.py --skewed -c 1000000 --skewness 2.4
python3 scripts/generate_synthetic_data.py --skewed -c 1000000 --skewness 2.6
python3 scripts/generate_synthetic_data.py --skewed -c 1000000 --skewness 2.8
python3 scripts/generate_synthetic_data.py --skewed -c 1000000 --skewness 3.0
python3 scripts/generate_synthetic_data.py --skewed -c 1000000 --skewness 3.2
python3 scripts/generate_synthetic_data.py --skewed -c 1000000 --skewness 3.4
python3 scripts/generate_synthetic_data.py --skewed -c 1000000 --skewness 3.6
python3 scripts/generate_synthetic_data.py --skewed -c 1000000 --skewness 3.8
python3 scripts/generate_synthetic_data.py --skewed -c 1000000 --skewness 4.0

cd ..
mkdir -p evaluation/experiments
ln -sf $(pwd)/evaluation/data $(pwd)/evaluation/experiments/data
