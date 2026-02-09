#!/bin/bash

cd evaluation
python3 scripts/plot_microbench_all.py
python3 scripts/plot_microbench_all.py --skewed
python3 scripts/plot_thresholds.py
python3 scripts/plot_pipeline.py
python3 scripts/plot_rewrites.py
cd ..
