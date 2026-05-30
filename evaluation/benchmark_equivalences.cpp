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
#include <unordered_map>
#include <string>
#include <algorithm>

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

void import_data(size_t row_count_e, size_t row_count_s, size_t partitions_e, size_t partitions_s, double alpha,
                 Connection& con, bool cold = false) {
  const auto size_prefixes = std::unordered_map<size_t, std::string>{{10'000, "10k"},
                                                                     {100'000, "100k"},
                                                                     {1'000'000, "1m"},
                                                                     {10'000'000, "10m"},
                                                                     {100'000'000, "100m"}};

  con.BeginTransaction();
  con.Query("DROP TABLE IF EXISTS employees;");
  con.Query("DROP TABLE IF EXISTS sales;");
  con.Query("DROP TABLE IF EXISTS date_dim;");
  con.Query("CREATE TABLE employees (employee_id int, dept varchar(20), salary float);");
  con.Query("CREATE TABLE sales (sale_id int, sold_date date, price float, sold_date_sk int);");
  con.Query("CREATE TABLE date_dim (d_date_sk int primary key, d_date date, d_year int, d_moy int, d_dom int);");

  if (!cold) {
    std::cout << "- Import data\n";
  }
  for (const auto& table_name : std::vector<std::string>{"employees", "sales", "date_dim"}) {
    const auto num_rows =  table_name == "employees" ? row_count_e : row_count_s;
    const auto num_partitions = table_name == "employees" ?  partitions_e : partitions_s;
    const auto num_rows_p = size_prefixes.at(num_rows);
    auto file_name = std::stringstream{};
    file_name << "resources/experiment_data/" << table_name << "_p" << num_partitions << "_" << num_rows_p << "_a"
              << std::fixed << std::setprecision(1) << alpha << "_s42.csv";
    if (!cold) {
      std::cout << " - " << table_name << " (" << file_name.str() << ") ..." << std::flush;
    }
    const auto start = std::chrono::steady_clock::now();
    const auto result = con.Query("COPY " + table_name + " FROM '" + file_name.str() + "' WITH (FORMAT CSV, DELIMITER ',', NULL '', QUOTE '\"', HEADER true);");
    const auto end = std::chrono::steady_clock::now();
    if (!cold) {
      std::cout << " (" << std::chrono::duration<double>{end - start}.count() << " s)\n";
    }

    if(result->HasError()) {
      throw std::runtime_error(result->GetError());
    }
  }
  con.Commit();
}

void explain(const std::map<std::string, std::string, std::greater<std::string>>& queries,
             const std::string& experiment, size_t run_id, Connection& con) {
  for (const auto& [query_name, query] : queries) {
    const auto result = con.Query("EXPLAIN ANALYZE " + query)->ToString();
    auto file_name = std::stringstream{};
    file_name << "plan_" << query_name << "_" << experiment << "_" << run_id << ".txt";
    auto stream = std::ofstream{"db_comparison_results/plans/duckdb/" + file_name.str()};
    stream << result;
  }
}

template <bool cold = false>
void run_experiment(const std::map<std::string, std::string, std::greater<std::string>> queries, size_t core_count,
                  const std::chrono::nanoseconds timeout, const std::string& experiment, size_t run_id,
                  const bool force_new_file, const std::unordered_map<std::string, size_t>& benchmark_rows,
                  const std::unordered_map<std::string, size_t>& benchmark_partitions, double alpha,
                  Connection& con) {

  if (!cold) {
    explain(queries, experiment, run_id, con);
  }

  auto runtime_ns = timeout;
  const auto max_rows = std::max(benchmark_rows.at("employees"), benchmark_rows.at("sales"));
  if (max_rows > 10'000'000) {
    runtime_ns = std::max(timeout, std::chrono::nanoseconds{120 * std::nano::den});
  }

  auto create_file = force_new_file;
  for (const auto& [query_name, query] : queries) {
    std::cout << "- Execute " << query_name << " ..." << std::flush;

    // One warm-up run.
    auto initial_result_count = idx_t{0};
    if constexpr (!cold) {
      initial_result_count = con.Query(query)->RowCount();
    } else {
      import_data(benchmark_rows.at("employees"), benchmark_rows.at("sales"), benchmark_partitions.at("employees"), benchmark_partitions.at("sales"), alpha, con, cold);
    }
    auto result_count = initial_result_count;
    auto run_durations = std::vector<std::chrono::nanoseconds>{};
    run_durations.reserve(1000);
    const auto start = std::chrono::steady_clock::now();
    while (std::chrono::steady_clock::now() - start < runtime_ns) {
      const auto run_start = std::chrono::steady_clock::now();
      result_count += con.Query(query)->RowCount();
      const auto run_end = std::chrono::steady_clock::now();
      if (run_end - start < runtime_ns || run_durations.empty()) {
        run_durations.push_back(run_end - run_start);
        if constexpr (cold) {
          break;
        }
      }
    }

    const auto duration = std::chrono::steady_clock::now() - start;
    std::cout << "\t" << run_durations.size() << " runs\t" << result_count << "\t" << initial_result_count << "\t"
              << std::chrono::duration<double, std::milli>{duration}.count() / static_cast<double>(run_durations.size()) << " ms/run\n";

    const auto on_employees = query_name.find("equiv6") != std::string::npos || query_name.find("equiv2") != std::string::npos;
    const auto table = on_employees ? "employees" : "sales";
    const auto rows = benchmark_rows.at(table);
    const auto partitions = benchmark_partitions.at(table);

    const auto file_mode = create_file ? std::ios::out : std::ios::app;
    // std::cout << experiment << " " << run_id << " " << std::boolalpha <<  create_file << " " << file_mode << "\n";
    auto file_name = cold ? "db_comparison_results/database_comparison__duckdb_cold.csv" : "db_comparison_results/database_comparison__duckdb.csv";
    auto ofstream = std::ofstream{file_name, file_mode};
    if (create_file) {
      ofstream << "DATABASE_SYSTEM,CORES,ITEM_NAME,ROWS,PARTITIONS,ALPHA,EXPERIMENT,RUNTIME_MS\n";
      create_file = false;
    }
    for (const auto& run_duration : run_durations) {
      ofstream << "duckdb," << core_count << "," << query_name << "," << rows << "," << partitions << ","
               << std::fixed << alpha << "," << experiment << ","
               << std::chrono::duration<double, std::milli>{run_duration}.count() << "\n";
    }
    ofstream.close();
  }
}

int main(int argc, char* argv[]) {
  auto row_count = size_t{1'000'000};
  auto core_count = std::stoul(execute_command("nproc"));
  auto skewed = false;
  auto test = false;
  auto timeout = 60.0;
  auto show = false;
  auto cold = false;
  auto runs = size_t{5};

  for (auto arg_id = 1; arg_id < argc; ++arg_id) {
    const auto argument = std::string{argv[arg_id]};
    if (argument == "--st") {
      core_count = 1;
    } else if (argument == "--time" || argument == "-t") {
      timeout = std::stod(argv[++arg_id]);
    } else if (argument == "--show") {
      show = true;
    } else if (argument == "--cold") {
      cold = true;
    } else if (argument == "--runs" || argument == "-r") {
      runs = std::stoul(argv[++arg_id]);
    } else {
      throw std::runtime_error("Unrecognized option: '" + argument + "'.");
    }
  }


  auto* config = new DBConfig();
  config->options.maximum_threads = core_count;
  // const auto db_file = std::string{"db_duckdb"};
  DuckDB db(nullptr, config);
  Connection con(db);
  const auto runtime_ns = std::chrono::nanoseconds{static_cast<int64_t>(timeout * static_cast<double>(std::nano::den))};
  std::cout << "Using " << db.NumberOfThreads() << " core(s); " << runtime_ns.count() << "ns\n";

  // con.Query("select * from duckdb_optimizers()")->Print();

  auto queries = std::map<std::string, std::string, std::greater<std::string>>{};
  for (const auto& entry : std::filesystem::directory_iterator{"resources"}) {
    if(!std::filesystem::is_regular_file(entry) || entry.path().extension() != ".sql" ||  entry.path().stem() == "schema") {
      continue;
    }

    queries[entry.path().stem()] = read_file(entry.path().c_str());
  }
  con.Query("SET disabled_optimizers = 'top_n_window_elimination';");

  const auto alphas = std::vector<double>{0.0, 0.5, 1.0, 1.5, 2.0};
  const auto base_rows = std::unordered_map<std::string, size_t>{{"employees", 10'000'000}, {"sales", 1'000'000}};
  const auto partitions = std::unordered_map<std::string, std::vector<size_t>>{
    {"employees", {10, 100, 1000, 10000, 100000}}, {"sales", {10, 50, 100, 200, 400}}};
  const auto base_partitions = std::unordered_map<std::string, size_t>{{"employees", 10}, {"sales", 50}};
  const auto rows = std::vector<size_t>{10'000, 100'000, 1'000'000, 10'000'000, 100'000'000};

  // First experiment: Number of rows
  std::cout << "EXPERIMENT 1: ROW COUNT\n";
  for (auto experiment_id = size_t{0}; experiment_id < rows.size(); ++experiment_id) {
    const auto row_count = rows[experiment_id];
    std::cout << "\n- " << row_count << " rows\n";
    if (!cold) {
      import_data(row_count, row_count, base_partitions.at("employees"), base_partitions.at("sales"), 0.0, con);
      const auto create_new_file = experiment_id == 0;
      const auto experiment_rows = std::unordered_map<std::string, size_t>{{"employees", row_count}, {"sales", row_count}};
      run_experiment(queries, core_count, runtime_ns, "rows", experiment_id, create_new_file, experiment_rows, base_partitions, 0.0, con);
    } else {
      const auto start = std::chrono::steady_clock::now();
      auto run = size_t{0};
      while (std::chrono::steady_clock::now() - start < runtime_ns || run < runs) {
        ++run;
        const auto create_new_file = experiment_id == 0 && run == 1;
        const auto experiment_rows = std::unordered_map<std::string, size_t>{{"employees", row_count}, {"sales", row_count}};
        run_experiment<true>(queries, core_count, runtime_ns, "rows", experiment_id, create_new_file, experiment_rows, base_partitions, 0.0, con);
      }
    }
  }

  // Second experiment: Number of partitions
  std::cout << "\n\nEXPERIMENT 2: PARTITION COUNT\n";
  const auto num_partitions = partitions.at("employees").size();
  if (num_partitions != partitions.at("sales").size()) {
    throw std::runtime_error("Invalid configuration.");
  }
  for (auto experiment_id = size_t{0}; experiment_id < num_partitions; ++experiment_id) {
    const auto partitions_e = partitions.at("employees")[experiment_id];
    const auto partitions_s = partitions.at("sales")[experiment_id];
    std::cout << "\n- " << partitions_e << "/" << partitions_s << " partitions\n";
    if (!cold) {
      import_data(base_rows.at("employees"), base_rows.at("sales"), partitions_e, partitions_s, 0.0, con);
      const auto experiment_partitions = std::unordered_map<std::string, size_t>{{"employees", partitions_e}, {"sales", partitions_s}};
      run_experiment(queries, core_count, runtime_ns, "partitions", experiment_id, false, base_rows, experiment_partitions, 0.0, con);
    } else {
      const auto start = std::chrono::steady_clock::now();
      auto run = size_t{0};
      while (std::chrono::steady_clock::now() - start < runtime_ns || run < runs) {
        ++run;
        const auto experiment_partitions = std::unordered_map<std::string, size_t>{{"employees", partitions_e}, {"sales", partitions_s}};
        run_experiment<true>(queries, core_count, runtime_ns, "partitions", experiment_id, false, base_rows, experiment_partitions, 0.0, con);
      }
    }
  }

  // Third experiment: Skewness
  std::cout << "\n\nEXPERIMENT 3: SKEWNESS\n";
  for (auto experiment_id = size_t{0}; experiment_id < alphas.size(); ++experiment_id) {
    const auto alpha = alphas[experiment_id];
    std::cout << "\n- Alpha " << alpha << "\n";
    if (!cold) {
      import_data(base_rows.at("employees"), base_rows.at("sales"), 1000, 50, alpha, con);
      const auto experiment_partitions = std::unordered_map<std::string, size_t>{{"employees", 1000}, {"sales", 50}};
      run_experiment(queries, core_count, runtime_ns, "alpha", experiment_id, false, base_rows, experiment_partitions, alpha, con);
    } else {
      const auto start = std::chrono::steady_clock::now();
      auto run = size_t{0};
      while (std::chrono::steady_clock::now() - start < runtime_ns || run < runs) {
        ++run;
        const auto experiment_partitions = std::unordered_map<std::string, size_t>{{"employees", 1000}, {"sales", 50}};
        run_experiment<true>(queries, core_count, runtime_ns, "alpha", experiment_id, false, base_rows, experiment_partitions, alpha, con);
      }
    }
  }
}
