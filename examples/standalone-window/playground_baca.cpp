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
    auto row_count = size_t{100};
    auto partition_count = size_t{10};
    auto core_count = std::stoul(execute_command("nproc"));
    auto randomized = false;
    auto verbose = false;
    auto num_runs = size_t{1};
    auto requested_level = std::optional<WindowOperatorConfig::OptimizationLevel>{};
    auto warmup = false;

    for (auto arg_id = 1; arg_id < argc; ++arg_id) {
        const auto argument = std::string{argv[arg_id]};
        if (argument == "--st") {
            core_count = 1;
        } else if (argument == "--randomized") {
            randomized = true;
        } else if (argument == "--verbose") {
            verbose = true;
        } else if (argument == "--runs" or argument == "-r") {
            num_runs = std::stoul(std::string{argv[++arg_id]});
        } else if (argument == "--level" or argument == "-l") {
            requested_level = str_to_optimization_level(std::string{argv[++arg_id]});
        } else if (argument == "--warmup" or argument == "-w") {
            warmup = true;
        } else if (arg_id == 1) {
            row_count = std::stoul(argument);
        } else if (arg_id == 2) {
            partition_count = std::stoul(argument);
        } else {
            throw std::runtime_error("Unrecognized option: '" + argument + "'.");
        }
    }

    auto* config = new DBConfig();
    config->options.maximum_threads = core_count;
    DuckDB db(nullptr, config);
    // DuckDB db(nullptr);
    Connection con(db);
    std::cout << "Using " << db.NumberOfThreads() << " core(s)\n";
    // con.SetAutoCommit(true);


    const auto table_file_name = [&](auto p, auto r){
        auto filename = std::stringstream{};
        filename << "data/synthetic_" << r << "-rows_" << p << "-partitions" << (randomized ? "_randomized" : "") << ".csv";
        return filename.str();
    };


    const auto filename = table_file_name(partition_count, row_count);

    if (!fileExists(filename)) {
        throw std::runtime_error("File '" + filename + "'does not exist!");
    }

    // std::cout << "== Import data ==\n";

    const auto predicate_value = uint64_t{3};

    con.BeginTransaction();
    // con.Query("create table store_sales ( ss_sold_date_sk integer , ss_sold_time_sk integer , ss_item_sk integer not null, ss_customer_sk integer , ss_cdemo_sk integer , ss_hdemo_sk integer , ss_addr_sk integer , ss_store_sk integer , ss_promo_sk integer , ss_ticket_number integer not null, ss_quantity integer , ss_wholesale_cost decimal(7,2) , ss_list_price decimal(7,2) , ss_sales_price decimal(7,2) , ss_ext_discount_amt decimal(7,2) , ss_ext_sales_price decimal(7,2) , ss_ext_wholesale_cost decimal(7,2) , ss_ext_list_price decimal(7,2) , ss_ext_tax decimal(7,2) , ss_coupon_amt decimal(7,2) , ss_net_paid decimal(7,2) , ss_net_paid_inc_tax decimal(7,2) , ss_net_profit decimal(7,2) );");

    // con.Query("COPY store_sales FROM 'store_sales_s1_ordered_100.csv' WITH (FORMAT CSV, DELIMITER ',', NULL '', QUOTE '\"');"); //->Print();
    con.Query("create table eval (a int, b int, c int);");
    con.Query("COPY eval FROM '" + filename + "' WITH (FORMAT CSV, DELIMITER ',', NULL '', QUOTE '\"');");
    con.Commit();


    std::cout << row_count << " rows, " << partition_count << " partitions\n";


    string query = "select t1.a, t1.b from eval t1 join lateral (select a, b from eval e2 where e2.a = t1.a order by b limit 3) t2 ON t1.a = t2.a and t1.b = t2.b;";


    // std::cout << "== Run ==\n";

    // auto result_count = con.Query(query)->RowCount();
    auto result_count = 0;
    if (warmup) {
        con.Query(query);
    }

    auto result = unique_ptr<MaterializedQueryResult>{};
    const auto start = std::chrono::steady_clock::now();
    for (auto run = 0; run < num_runs; ++run) {
        result = con.Query(query);
        result_count += result->RowCount();
        // result->Print();
    }

    const auto duration = std::chrono::steady_clock::now() - start;
    const auto expected = num_runs * partition_count * static_cast<idx_t>(predicate_value);
    const auto is_smaller = result_count < expected;
    const auto diff = is_smaller ? expected - result_count : result_count - expected;
    std::cout << "\t" << "Baca" << "\t" << result_count  << "\t" << (is_smaller ? "-" : "+") << diff << "\t" <<  std::chrono::duration<double, std::milli>{duration}.count() << " ms\n";


    if (verbose) {
        result->Print();
    }
 

    std::cout << "\n";
    con.BeginTransaction();
    con.Query("drop table eval;");
    con.Commit();
}
