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
#include <filesystem>
#include <cstdlib>
#include <stdexcept>
#include <cstdio>
#include <memory>
#include <string>
#include <array>
#include <tuple>
#include <regex>

using namespace duckdb;


bool fileExists(const std::string& filename) {
	std::ifstream file(filename);
	return file.good();  // Returns true if the file exists and is accessible
}

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


int main(int argc, char* argv[]) {
	auto row_count = size_t{1'000'000};
	auto core_count = std::stoul(execute_command("nproc"));
	auto skewed = false;
	auto test = false;
	auto distinct = false;

	for (auto arg_id = 1; arg_id < argc; ++arg_id) {
		const auto argument = std::string{argv[arg_id]};
		if (argument == "--st") {
			core_count = 1;
		} else if (argument == "--skewed") {
			skewed = true;
		} else if (argument == "--test") {
			test = true;
		} else if (argument == "--distinct" or argument == "-d") {
            distinct = true;
        } else if (arg_id == 1) {
			row_count = std::stoi(argument);
		} else {
			throw std::runtime_error("Unrecognized option: '" + argument + "'.");
		}
	}


	auto* config = new DBConfig();
	config->options.maximum_threads = core_count;
	DuckDB db(nullptr, config);
	// std::cout << db.NumberOfThreads() << "\n";
	Connection con(db);
	std::cout << "Using " << db.NumberOfThreads() << " core(s); with" << (skewed ? "" : "out") << " skew\n";
	// con.SetAutoCommit(true);

	const auto table_file_name = [&](auto p, auto r){
		auto filename = std::stringstream{};
		filename << "data/synthetic_" << r << "-rows_" << p << "-partitions" << (skewed ? "_skewed" : "" ) << ".csv";
		return filename.str();
	};

	auto file_names = std::vector<std::tuple<size_t, size_t, std::string>>{};
	if (!skewed) {
		const auto partition_counts_base = size_t{10};
		while (file_names.empty() || std::get<1>(file_names.back()) < row_count / partition_counts_base) {
			const auto partition_count = file_names.empty() ? partition_counts_base : std::get<1>(file_names.back()) * partition_counts_base;
			const auto filename = table_file_name(partition_count, row_count);

			if (!fileExists(filename)) {
				throw std::runtime_error("File '" + filename + "' does not exist!");
			}

			file_names.emplace_back(row_count, partition_count, filename);
		}
	} else {
		const auto path = std::filesystem::path{"data"};
		for (const auto& entry : std::filesystem::directory_iterator(path)) {
			if(!std::filesystem::is_regular_file(entry)) {
				continue;
			}
			const auto filename = entry.path().filename().string();
			if (filename.find("_skewed") == std::string::npos) {
				continue;
			}

			auto row_count_match = std::smatch{};
			if (!std::regex_search(filename, row_count_match, std::regex{"\\d+-rows"})) {
				continue;
			}
			const auto file_row_count = std::stoul(row_count_match[0].str().substr(0, row_count_match[0].length() - 5));

			auto partition_count_match = std::smatch{};
			if (!std::regex_search(filename, partition_count_match, std::regex{"\\d+-partitions"})) {
				continue;
			}
			const auto partition_count = std::stoul(partition_count_match[0].str().substr(0, partition_count_match[0].length() - 11));

			file_names.emplace_back(file_row_count, partition_count, "data/" + filename);
		}

		std::sort(file_names.begin(), file_names.end(), [](const auto& lhs, const auto& rhs){
			return std::get<0>(lhs) != std::get<0>(rhs) ? std::get<0>(lhs) < std::get<0>(rhs) : std::get<1>(lhs) < std::get<1>(rhs);
		});
	}

	auto ofstream = std::ofstream{};
	if (!test) {
		auto result_filename = std::stringstream{};
		result_filename << "microbenchmark_baca_";
		result_filename << (distinct ? "distinct_" : "") << (skewed ? "skewed_" : std::to_string(row_count) + "-rows_") << (core_count == 1 ? "st" : "mt") << ".csv";
		ofstream.open(result_filename.str());
		ofstream << "CONFIGURATION,ROW_COUNT,PARTITION_COUNT,RESULTS_PER_PARTITION,RESULT_COUNT,RUNTIME_NS\n";
		ofstream << std::fixed;
	}

	const auto predicate_value = int64_t{3};
	for (const auto& [run_row_count, partition_count, filename] : file_names) {
		auto num_runs = 1'000;
		if ((core_count == 1 && run_row_count > 100'000) || run_row_count > 1'000'000) {
			num_runs = 100;
		}
		if (core_count == 1 && run_row_count > 10'000'000) {
			num_runs = 10;
		}
		if (test) {
			num_runs = 1;
		}

		std::cout << run_row_count << " rows, " << partition_count << " partitions, " << num_runs << " executions\n";
		con.BeginTransaction();
		con.Query("create table eval (a int, b int, c int);");
		con.Query("COPY eval FROM '" + filename + "' WITH (FORMAT CSV, DELIMITER ',', NULL '', QUOTE '\"');");
		con.Commit();

		string query = distinct ?
        "select e.* from eval e join (select t2.a, t2.b from (select distinct a from eval) t1 join lateral (select a, b from eval e2 where e2.a = t1.a order by b limit 3) t2 ON t1.a = t2.a) t3 on e.b = t3.b and e.a = t3.a;"
        : "select t1.a, t1.b from eval t1 join lateral (select a, b from eval e2 where e2.a = t1.a order by b limit 3) t2 ON t1.a = t2.a and t1.b = t2.b;";

		// One warm-up run.
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
		std::cout << "\t" << "Baca" << "\t" << result_count << "\t" <<  std::chrono::duration<double, std::milli>{duration}.count() << " ms\n";
		if (!test) {
			for (const auto& run_duration : run_durations) {
				//ofstream << "CONFIGURATION,ROW_COUNT,PARTITION_COUNT,RESULT_COUNT,RUNTIME_NS\n";
				ofstream << "Baca" << "," << run_row_count << "," << partition_count << "," << predicate_value << "," << init_result_count << "," << run_duration.count() << "\n";
			}
		}

		con.BeginTransaction();
		con.Query("drop table eval;");
		con.Commit();
		std::cout << "\n";
	}

	if (!test) {
		ofstream.close();
	}

}
