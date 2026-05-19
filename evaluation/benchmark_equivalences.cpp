#include "duckdb.hpp"
#ifndef DUCKDB_AMALGAMATION
#include "duckdb/execution/operator/aggregate/physical_window.hpp"
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
#include <filesystem>
#include <cstdlib>
#include <stdexcept>
#include <cstdio>
#include <memory>
#include <string>
#include <array>
#include <tuple>
#include <regex>
#include <optional>
#include <map>

using namespace duckdb;

std::string execute_command(const std::string& command) {
	std::array<char, 128> buffer;
	std::string result;
	auto* pipe = popen(command.c_str(), "r");
	if (!pipe) {
		throw std::runtime_error("popen() failed!");
	}
	while (fgets(buffer.data(), buffer.size(), pipe) != nullptr) {
		result += buffer.data();
	}
	pclose(pipe);
	return result;
}

std::string read_file(const std::filesystem::path::value_type* file_name){
	auto in = std::ifstream{file_name};
	if (!in.is_open()) {
		throw std::runtime_error("Could not read from file " + std::string{file_name});
	}
	std::ostringstream sstr;
    sstr << in.rdbuf();
    return sstr.str();
}

int main(int argc, char* argv[]) {
	auto row_count = size_t{1'000'000};
	auto core_count = std::stoul(execute_command("nproc"));
	auto skewed = false;
	auto test = false;
	auto timeout = size_t{60};
	auto explain = false;
	auto show = false;

	for (auto arg_id = 1; arg_id < argc; ++arg_id) {
		const auto argument = std::string{argv[arg_id]};
		if (argument == "--st") {
			core_count = 1;
		} else if (argument == "--time" || argument == "-t") {
			timeout = std::stoul(argv[++arg_id]);
		} else if (argument == "--explain") {
			explain = true;
		} else if (argument == "--show") {
			show = true;
		} else {
			throw std::runtime_error("Unrecognized option: '" + argument + "'.");
		}
	}


	auto* config = new DBConfig();
	config->options.maximum_threads = core_count;
	DuckDB db(nullptr, config);
	Connection con(db);
	const auto runtime_ns = std::chrono::nanoseconds{timeout * std::nano::den};
	std::cout << "Using " << db.NumberOfThreads() << " core(s); " << runtime_ns.count() << "ns\n";

	con.Query("select * from duckdb_optimizers()")->Print();


	auto queries = std::map<std::string, std::string, std::greater<std::string>>{};
	for (const auto& entry : std::filesystem::directory_iterator{"resources"}) {
		if(!std::filesystem::is_regular_file(entry) || entry.path().extension() != ".sql" ||  entry.path().stem() == "schema") {
			continue;
		}

		queries[entry.path().stem()] = read_file(entry.path().c_str());
	}

	con.BeginTransaction();
	con.Query("CREATE TABLE employees (employee_id int, dept varchar(20), salary float);");
	con.Query("CREATE TABLE sales (sale_id int, sold_date date, price float, sold_date_sk int);");
	con.Query("CREATE TABLE date_dim (d_date_sk int primary key, d_date date, d_year int, d_moy int, d_dom int);");

	for (const auto& table_name : std::vector<std::string>{"employees", "sales", "date_dim"}) {
		const auto result = con.Query("COPY " + table_name + " FROM 'resources/experiment_data/" + table_name + ".csv' WITH (FORMAT CSV, DELIMITER ',', NULL '', QUOTE '\"');");

		if(result->HasError()) {
			throw std::runtime_error(result->GetError());
		}
	}

	con.Commit();
	con.Query("SET disabled_optimizers = 'top_n_window_elimination';");

	if (explain) {
		for (const auto& [query_name, query] : queries) {
			const auto result = con.Query("EXPLAIN ANALYZE " + query)->ToString();
			auto stream = std::ofstream{"db_comparison_results/plans/duckdb/plan_" + query_name + ".txt"};
			stream << result;
		}

		return 0;
	}

	if (show) {
		for (const auto& [query_name, query] : queries) {
			const auto result = con.Query(query);
			if (result->RowCount() <= 100) {
				result->Print();
			}

			auto stream = std::ofstream{"db_comparison_results/plans/duckdb/result_" + query_name + ".txt"};
			stream << result->ToString();
		}

		return 0;
	}


	auto ofstream = std::ofstream{};
	auto result_filename = std::stringstream{};
	result_filename << "db_comparison_results/database_comparison__duckdb.csv";
	ofstream.open(result_filename.str());
	ofstream << "DATABASE_SYSTEM,CORES,CLIENTS,ITEM_NAME,RUNTIME_MS\n";
	ofstream << std::fixed;


	for (const auto& [query_name, query] : queries) {
		std::cout << "Execute " << query_name << "\n";

		// One warm-up run.
		const auto initial_result_count = con.Query(query)->RowCount();
		auto result_count = initial_result_count;
		auto run_durations = std::vector<std::chrono::nanoseconds>{};
		run_durations.reserve(1000);
		const auto start = std::chrono::steady_clock::now();
		while (std::chrono::steady_clock::now() - start < runtime_ns) {
			const auto run_start = std::chrono::steady_clock::now();
			result_count += con.Query(query)->RowCount();
			const auto run_end = std::chrono::steady_clock::now();
			if (run_end - start < runtime_ns) {
				run_durations.push_back(run_end - run_start);
			}

		}
		const auto duration = std::chrono::steady_clock::now() - start;
		std::cout << "\t" << run_durations.size() << " runs\t" << result_count << "\t" << initial_result_count << "\t" << std::chrono::duration<double, std::milli>{duration}.count() / static_cast<double>(run_durations.size()) << " ms/run\n";
		for (const auto& run_duration : run_durations) {
			ofstream << "duckdb," << core_count << "," << 1 << "," << query_name << "," << "," << std::chrono::duration<double, std::milli>{run_duration}.count() << "\n";
		}
	}

	ofstream.close();

}
