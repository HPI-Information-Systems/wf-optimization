#include "duckdb.hpp"
#ifndef DUCKDB_AMALGAMATION
#include "duckdb/execution/operator/aggregate/physical_window.hpp"
#include "duckdb/execution/operator/aggregate/window_operator_config.hpp"
#include "duckdb/planner/expression.hpp"
#include "duckdb/planner/expression/bound_window_expression.hpp"
#include "duckdb/planner/expression/bound_constant_expression.hpp"
#include "duckdb/planner/expression/bound_reference_expression.hpp"
#include "duckdb/parallel/thread_context.hpp"
#include "duckdb/function/aggregate/distributive_functions.hpp"
#include "include/core_functions/aggregate/distributive_functions.hpp"
#include "duckdb/parallel/pipeline_event.hpp"
#include "duckdb/execution/operator/scan/physical_dummy_scan.hpp"
#endif

#include <iostream>
#include <chrono>
#include <ratio>
#include <sstream>
#include <fstream>
#include <cstdlib>
#include <stdexcept>
#include <cstdio>
#include <memory>
#include <string>
#include <array>

using namespace duckdb;

enum class OptimizationLevel : uint8_t { None, Filter, EarlyOut, SkipSort };


bool fileExists(const std::string& filename) {
    std::ifstream file(filename);
    return file.good();  // Returns true if the file exists and is accessible
}

std::string execute_command(const std::string& command) {
    std::array<char, 128> buffer;
    std::string result;
    std::unique_ptr<FILE, decltype(&pclose)> pipe(popen(command.c_str(), "r"), pclose);
    if (!pipe) {
        throw std::runtime_error("popen() failed!");
    }
    while (fgets(buffer.data(), buffer.size(), pipe.get()) != nullptr) {
        result += buffer.data();
    }
    return result;
}


int main(int argc, char* argv[]) {
	auto row_count = 1'000'000;
	auto core_count = std::stoul(execute_command("nproc"));
	auto skewed = false;

	if (argc >= 2) {
		row_count = std::atoi(argv[1]);
	}

	for (auto arg_id = 2; arg_id < argc; ++arg_id) {
		const auto argument = std::string{argv[arg_id]};
		if (argument == "--st") {
			core_count = 1;
		} else if (argument == "--skewed") {
			skewed = true;
		} else {
			throw std::runtime_error("Unrecognized option: '" + argument + "'.");
		}
	}

	std::cout << "Using " << core_count << " core(s); with" << (skewed ? "" : "out") << " skew\n";

	auto* config = new DBConfig();
	config->options.maximum_threads = core_count;
	DuckDB db(nullptr, config);
	// std::cout << db.NumberOfThreads() << "\n";
	Connection con(db);
	// con.SetAutoCommit(true);

	const auto partition_counts_base = size_t{10};
	auto partition_counts = std::vector<size_t>{partition_counts_base};
	while (partition_counts.back() < row_count) {
		partition_counts.push_back(partition_counts.back() * partition_counts_base);
	}

	const auto level_to_str = [](const auto level) {
		switch (level) {
			case OptimizationLevel::None:
				return "Default";
			case OptimizationLevel::Filter:
				return "Filter";
			case OptimizationLevel::EarlyOut:
				return "EarlyOut";
			case OptimizationLevel::SkipSort:
				return "SkipSort";
		}
		throw std::runtime_error("GCC thinks this is reachable.");
	};

	const auto table_file_name = [&](auto p, auto r){
		auto filename = std::stringstream{};
		filename << "data/synthetic_" << r << "-rows_" << p << "-partitions" << (skewed ? "_skewed" : "" ) << ".csv";
		return filename.str();
	};

	for (const auto partition_count : partition_counts) {
		if (partition_count >= row_count) {
			break;
		}

		const auto filename = table_file_name(partition_count, row_count);

		if (!fileExists(filename)) {
			throw std::runtime_error("File '" + filename + "' does not exist!");
		}
	}

	// std::cout << "== Import data ==\n";
	auto ofstream = std::ofstream{};
	const auto result_filename = "microbenchmark_" + std::to_string(row_count) + "-rows_" + (core_count == 1 ? "st" : "mt") + (skewed ? "_skewed" : "" ) + ".csv";
	ofstream.open(result_filename);
	ofstream << "CONFIGURATION,ROW_COUNT,PARTITION_COUNT,RESULTS_PER_PARTITION,RESULT_COUNT,RUNTIME_NS\n";
	ofstream << std::fixed;

	const auto predicate_value = uint64_t{3};
	for (const auto partition_count : partition_counts) {
		if (partition_count >= row_count) {
			break;
		}

		const auto num_runs = (core_count == 1 || row_count >= 1'000'000) ? 100 : 1'000;

		std::cout << row_count << " rows, " << partition_count << " partitions, " << num_runs << " runs\n";
		for (auto level_int = uint8_t{0}; level_int < 3; ++level_int) {
			const auto level = static_cast<OptimizationLevel>(level_int);
			const auto level_str = level_to_str(level);
			con.BeginTransaction();
			// con.Query("create table store_sales ( ss_sold_date_sk integer , ss_sold_time_sk integer , ss_item_sk integer not null, ss_customer_sk integer , ss_cdemo_sk integer , ss_hdemo_sk integer , ss_addr_sk integer , ss_store_sk integer , ss_promo_sk integer , ss_ticket_number integer not null, ss_quantity integer , ss_wholesale_cost decimal(7,2) , ss_list_price decimal(7,2) , ss_sales_price decimal(7,2) , ss_ext_discount_amt decimal(7,2) , ss_ext_sales_price decimal(7,2) , ss_ext_wholesale_cost decimal(7,2) , ss_ext_list_price decimal(7,2) , ss_ext_tax decimal(7,2) , ss_coupon_amt decimal(7,2) , ss_net_paid decimal(7,2) , ss_net_paid_inc_tax decimal(7,2) , ss_net_profit decimal(7,2) );");

			// con.Query("COPY store_sales FROM 'store_sales_s1_ordered_100.csv' WITH (FORMAT CSV, DELIMITER ',', NULL '', QUOTE '\"');"); //->Print();
			con.Query("create table eval (a int, b int, c int);");
			const auto filename = table_file_name(partition_count, row_count);
			con.Query("COPY eval FROM '" + filename + "' WITH (FORMAT CSV, DELIMITER ',', NULL '', QUOTE '\"');");
			con.Commit();


			// con.Query("select ss_store_sk, count(*), count(distinct ss_sold_date_sk) from store_sales group by ss_store_sk")->Print();

			// std::cout << "== Row count ==\n";

			// con.Query("SELECT count(*) from eval")->Print();
			// con.Query("SELECT min(ss_item_sk), max(ss_item_sk) from store_sales")->Print();
			// con.Query("SELECT count(distinct ss_sold_date_sk) from store_sales")->Print();
			// con.Query("SELECT count(distinct ss_item_sk) from store_sales")->Print();

			// string query = use_filter ?
			//   "SELECT ss_item_sk, ss_sold_date_sk, rank() OVER (PARTITION BY ss_store_sk ORDER BY ss_sold_date_sk) rnk FROM store_sales where ss_item_sk < 4" :
			//  "SELECT * FROM (SELECT ss_item_sk, ss_sold_date_sk, rank() OVER (PARTITION BY ss_store_sk ORDER BY ss_sold_date_sk) rnk FROM store_sales where ss_item_sk < 4) t WHERE rnk < 20";

			//con.Query("select * from store_sales order by ss_store_sk, ss_sold_date_sk;")->Print();

			// con.Query("select min(ss_item_sk), max(ss_item_sk), count(distinct ss_item_sk) from store_sales group by ss_store_sk;")->Print();
			// con.Query("select min(ss_item_sk), max(ss_item_sk), count(distinct ss_item_sk) from store_sales where ss_item_sk < 10000  group by ss_store_sk;")->Print();

			// std::exit(0);

			// string query = WindowOperatorConfig::get().do_filter ?
			//   "SELECT * from (select ss_store_sk, ss_sold_date_sk, rank() OVER (PARTITION BY ss_store_sk ORDER BY ss_sold_date_sk) rnk FROM store_sales where ss_item_sk < 10000) t" :
			//  "SELECT * FROM (SELECT ss_store_sk, ss_sold_date_sk, rank() OVER (PARTITION BY ss_store_sk ORDER BY ss_sold_date_sk) rnk FROM store_sales where ss_item_sk < 10000) t";

			const auto predicate_value = uint64_t{3};
			WindowOperatorConfig::get().predicate_value = predicate_value;
			WindowOperatorConfig::get().do_filter = level != OptimizationLevel::None;
			WindowOperatorConfig::get().do_early_out = level == OptimizationLevel::EarlyOut;
			WindowOperatorConfig::get().skip_sort = level == OptimizationLevel::SkipSort;

			string query = WindowOperatorConfig::get().do_filter ?
			 "SELECT * from (select a, b, rank() OVER (PARTITION BY a ORDER BY b) rnk FROM eval) t" :
			  "SELECT * from (select a, b, rank() OVER (PARTITION BY a ORDER BY b) rnk FROM eval) t WHERE rnk <= " + std::to_string(predicate_value);

			// std::cout << "== Run ==\n";

			// auto result_count = con.Query(query)->RowCount();
			const auto init_result_count = con.Query(query)->RowCount();
			auto result_count = init_result_count;
			auto run_durations = std::vector<std::chrono::nanoseconds>(num_runs);
			const auto start = std::chrono::steady_clock::now();
			for (auto run = 0; run < num_runs; ++run) {
				const auto run_start = std::chrono::steady_clock::now();
				result_count += con.Query(query)->RowCount();
				run_durations[run] = std::chrono::steady_clock::now() - run_start;
			}


			const auto duration = std::chrono::steady_clock::now() - start;
			std::cout << "\t" << level_str << "\t" << result_count << "\t" <<  std::chrono::duration<double, std::milli>{duration}.count() << " ms\n";



			for (const auto& duration : run_durations) {
				//ofstream << "CONFIGURATION,ROW_COUNT,PARTITION_COUNT,RESULT_COUNT,RUNTIME_NS\n";
				ofstream << level_str << "," << row_count << "," << partition_count << "," << predicate_value << "," << init_result_count << "," << duration.count() << "\n";
			}

			con.BeginTransaction();
			con.Query("drop table eval;");
			con.Commit();

			// std::cout << "== Print ==\n";
			// con.Query(query)->Print();
			// con.Query("select min(cnt), max(cnt), avg(cnt), min(o_cnt), max(o_cnt), avg(o_cnt) from (SELECT count(*) cnt, count(distinct ss_sold_date_sk) o_cnt from store_sales group BY ss_item_sk) t")->Print();
			//con.Query("SELECT * FROM (SELECT ss_item_sk, ss_sold_date_sk, rank() OVER (PARTITION BY ss_item_sk ORDER BY ss_sold_date_sk) rnk FROM store_sales) t WHERE rnk < 20")->Print();

			// con.Query("select rnk, count(rnk) cnt from (SELECT ss_store_sk, ss_sold_date_sk, rank() OVER (PARTITION BY ss_store_sk ORDER BY ss_sold_date_sk) rnk FROM store_sales) t group by rnk order by cnt desc limit 20")->Print();

			// con.Query("select ss_store_sk, max(rnk) from (SELECT ss_store_sk, ss_sold_date_sk, rank() OVER (PARTITION BY ss_store_sk ORDER BY ss_sold_date_sk) rnk FROM store_sales) t group by ss_store_sk order by ss_store_sk")->Print();

			// con.Query("select ss_store_sk, count(*) cnt from store_sales group by ss_store_sk order by ss_store_sk")->Print();

			//con.Query("select count(distinct rnk) from (SELECT ss_item_sk, ss_sold_date_sk, rank() OVER (PARTITION BY ss_store_sk ORDER BY ss_sold_date_sk) rnk FROM store_sales) t")->Print();

			// con.Query("EXPLAIN ANALYZE " + query)->Print();
		}
		std::cout << "\n";
	}

	ofstream.close();
}
