#! /usr/bin/bash

cd experiments_analyses
ninja datagen

cd ../evaluation
mkdir -p resources/experiment_data
mv ../experiments_analyses/data/employees* resources/experiment_data/

python3 scripts/create_date_dim.py ../experiments_analyses/data/sales_p100_1m_a0.0_s42.csv
python3 scripts/create_date_dim.py ../experiments_analyses/data/sales_p10_1m_a0.0_s42.csv
python3 scripts/create_date_dim.py ../experiments_analyses/data/sales_p200_1m_a0.0_s42.csv
python3 scripts/create_date_dim.py ../experiments_analyses/data/sales_p400_1m_a0.0_s42.csv
python3 scripts/create_date_dim.py ../experiments_analyses/data/sales_p50_100k_a0.0_s42.csv
python3 scripts/create_date_dim.py ../experiments_analyses/data/sales_p50_100m_a0.0_s42.csv
python3 scripts/create_date_dim.py ../experiments_analyses/data/sales_p50_10k_a0.0_s42.csv
python3 scripts/create_date_dim.py ../experiments_analyses/data/sales_p50_10m_a0.0_s42.csv
python3 scripts/create_date_dim.py ../experiments_analyses/data/sales_p50_1m_a0.0_s42.csv
python3 scripts/create_date_dim.py ../experiments_analyses/data/sales_p50_1m_a0.5_s42.csv
python3 scripts/create_date_dim.py ../experiments_analyses/data/sales_p50_1m_a1.0_s42.csv
python3 scripts/create_date_dim.py ../experiments_analyses/data/sales_p50_1m_a1.5_s42.csv
python3 scripts/create_date_dim.py ../experiments_analyses/data/sales_p50_1m_a2.0_s42.csv

cd ..
