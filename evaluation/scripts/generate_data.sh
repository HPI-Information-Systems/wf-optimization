#!/bin/bash

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
python3 scripts/generate_synthetic_data.py --skewed -c 10000
python3 scripts/generate_synthetic_data.py --skewed -c 100000
python3 scripts/generate_synthetic_data.py --skewed -c 1000000
python3 scripts/generate_synthetic_data.py --skewed -c 10000000
python3 scripts/generate_synthetic_data.py --skewed -c 100000000
