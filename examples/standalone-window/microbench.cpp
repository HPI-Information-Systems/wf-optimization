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

using namespace duckdb;

int main() {
	auto* config = new DBConfig();
	config->options.maximum_threads = idx_t{1};;
	DuckDB db(nullptr, config);
	std::cout << db.NumberOfThreads() << "\n";
	Connection con(db);
	// con.SetAutoCommit(true);

	const auto do_early_out = std::getenv("EARLY_OUT");
	const auto use_winop = std::getenv("USE_FILTER");
	if (do_early_out && !std::strcmp(do_early_out, "1")) {
		WindowOperatorConfig::get().do_filter = true;
  		WindowOperatorConfig::get().do_early_out = true;
  	// std::cout << "EARLY_OUT\n";
	} else if (use_winop && !std::strcmp(use_winop, "1")) {
  		WindowOperatorConfig::get().do_filter = true;
  	// std::cout << "FILTER\n";
	}

	std::cout << "== Import data ==\n";
	constexpr auto num_runs = 1;
	con.BeginTransaction();
	con.Query("create table store_sales ( ss_sold_date_sk integer , ss_sold_time_sk integer , ss_item_sk integer not null, ss_customer_sk integer , ss_cdemo_sk integer , ss_hdemo_sk integer , ss_addr_sk integer , ss_store_sk integer , ss_promo_sk integer , ss_ticket_number integer not null, ss_quantity integer , ss_wholesale_cost decimal(7,2) , ss_list_price decimal(7,2) , ss_sales_price decimal(7,2) , ss_ext_discount_amt decimal(7,2) , ss_ext_sales_price decimal(7,2) , ss_ext_wholesale_cost decimal(7,2) , ss_ext_list_price decimal(7,2) , ss_ext_tax decimal(7,2) , ss_coupon_amt decimal(7,2) , ss_net_paid decimal(7,2) , ss_net_paid_inc_tax decimal(7,2) , ss_net_profit decimal(7,2) );");

	con.Query("COPY store_sales FROM 'store_sales_s1_ordered_100.csv' WITH (FORMAT CSV, DELIMITER ',', NULL '', QUOTE '\"');"); //->Print();
	con.Commit();


	con.Query("select ss_store_sk, count(*), count(distinct ss_sold_date_sk) from store_sales group by ss_store_sk")->Print();

	std::cout << "== Row count ==\n";

	// con.Query("SELECT count(*) from store_sales")->Print();
	// con.Query("SELECT min(ss_item_sk), max(ss_item_sk) from store_sales")->Print();
	// con.Query("SELECT count(distinct ss_sold_date_sk) from store_sales")->Print();
	// con.Query("SELECT count(distinct ss_item_sk) from store_sales")->Print();

	// string query = use_filter ?
	//   "SELECT ss_item_sk, ss_sold_date_sk, rank() OVER (PARTITION BY ss_store_sk ORDER BY ss_sold_date_sk) rnk FROM store_sales where ss_item_sk < 4" :
	//  "SELECT * FROM (SELECT ss_item_sk, ss_sold_date_sk, rank() OVER (PARTITION BY ss_store_sk ORDER BY ss_sold_date_sk) rnk FROM store_sales where ss_item_sk < 4) t WHERE rnk < 20";

	//con.Query("select * from store_sales order by ss_store_sk, ss_sold_date_sk;")->Print();

	con.Query("select min(ss_item_sk), max(ss_item_sk), count(distinct ss_item_sk) from store_sales group by ss_store_sk;")->Print();
	con.Query("select min(ss_item_sk), max(ss_item_sk), count(distinct ss_item_sk) from store_sales where ss_item_sk < 10000  group by ss_store_sk;")->Print();

	// std::exit(0);

	string query = WindowOperatorConfig::get().do_filter ?
	  "SELECT * from (select ss_store_sk, ss_sold_date_sk, rank() OVER (PARTITION BY ss_store_sk ORDER BY ss_sold_date_sk) rnk FROM store_sales where ss_item_sk < 10000) t" :
	 "SELECT * FROM (SELECT ss_store_sk, ss_sold_date_sk, rank() OVER (PARTITION BY ss_store_sk ORDER BY ss_sold_date_sk) rnk FROM store_sales where ss_item_sk < 10000) t";

	std::cout << "== Run ==\n";

	// auto result_count = con.Query(query)->RowCount();
	auto result_count = idx_t{0};
	const auto start = std::chrono::steady_clock::now();
	for (auto run = 0; run < num_runs; ++run) {
		result_count += con.Query(query)->RowCount();
	}


	const auto duration = std::chrono::steady_clock::now() - start;
	std::cout << result_count << "\t" <<  std::chrono::duration<double, std::milli>{duration}.count() << " ms\n";
	std::cout << "== Print ==\n";
	con.Query(query)->Print();
	// con.Query("select min(cnt), max(cnt), avg(cnt), min(o_cnt), max(o_cnt), avg(o_cnt) from (SELECT count(*) cnt, count(distinct ss_sold_date_sk) o_cnt from store_sales group BY ss_item_sk) t")->Print();
	//con.Query("SELECT * FROM (SELECT ss_item_sk, ss_sold_date_sk, rank() OVER (PARTITION BY ss_item_sk ORDER BY ss_sold_date_sk) rnk FROM store_sales) t WHERE rnk < 20")->Print();

	// con.Query("select rnk, count(rnk) cnt from (SELECT ss_store_sk, ss_sold_date_sk, rank() OVER (PARTITION BY ss_store_sk ORDER BY ss_sold_date_sk) rnk FROM store_sales) t group by rnk order by cnt desc limit 20")->Print();

	// con.Query("select ss_store_sk, max(rnk) from (SELECT ss_store_sk, ss_sold_date_sk, rank() OVER (PARTITION BY ss_store_sk ORDER BY ss_sold_date_sk) rnk FROM store_sales) t group by ss_store_sk order by ss_store_sk")->Print();

	// con.Query("select ss_store_sk, count(*) cnt from store_sales group by ss_store_sk order by ss_store_sk")->Print();

	//con.Query("select count(distinct rnk) from (SELECT ss_item_sk, ss_sold_date_sk, rank() OVER (PARTITION BY ss_store_sk ORDER BY ss_sold_date_sk) rnk FROM store_sales) t")->Print();

	// con.Query("EXPLAIN ANALYZE " + query)->Print();
}
