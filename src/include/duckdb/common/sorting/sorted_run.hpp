//===----------------------------------------------------------------------===//
//                         DuckDB
//
// duckdb/common/sorting/sorted_run.hpp
//
//
//===----------------------------------------------------------------------===//

#pragma once

#include "duckdb/common/types/row/tuple_data_states.hpp"
#include "duckdb/execution/expression_executor.hpp"
#include "duckdb/common/sorting/sort_key.hpp"

#include <type_traits>

namespace duckdb {

class Sort;
class SortedRun;
class BufferManager;
class DataChunk;
class TupleDataCollection;
class TupleDataLayout;

class SortedRunScanState {
public:
	SortedRunScanState(ClientContext &context, const Sort &sort);

public:
	void Scan(const SortedRun &sorted_run, const Vector &sort_key_pointers, const idx_t &count, DataChunk &chunk);

private:
	template <SortKeyType sort_key_type, bool HAS_POS_LIST>
	void TemplatedScan(const SortedRun &sorted_run, const Vector &sort_key_pointers, const idx_t &count,
	                   DataChunk &chunk);

private:
	const Sort &sort;
	ExpressionExecutor key_executor;
	DataChunk key;
	DataChunk decoded_key;
	TupleDataScanState payload_state;
};

// Constants for column value extraction from SortKey
constexpr auto ORDER_BY_MASK = uint64_t{0b00000000'00000000'00000000'11111111'11111111'11111111'11111111'11111111};
constexpr auto VALUE_MASK = uint64_t{0b00000000'00000000'00000000'00000000'01111111'11111111'11111111'11111111};
constexpr auto PARTITION_BITS = 24;
constexpr auto FIRST_ORDER_BITS = 16;
constexpr auto SECOND_ORDER_BITS = 48;

inline uint64_t extract_partition(const SortKey<SortKeyType::NO_PAYLOAD_FIXED_16>& key) {
	return key.part0 >> PARTITION_BITS;
}

inline uint64_t extract_order_by(const SortKey<SortKeyType::NO_PAYLOAD_FIXED_16>& key) {
	D_ASSERT(!(key.part1 << FIRST_ORDER_BITS));
	return (ORDER_BY_MASK & (key.part0 << FIRST_ORDER_BITS)) | (key.part1 >> SECOND_ORDER_BITS);
}

inline std::string print_bits(const uint64_t value) {
	auto msg = std::stringstream{};
	msg << std::bitset<64>(value);
	return msg.str();
}

class SortedRun {
public:
	SortedRun(ClientContext &context, const Sort &sort, bool is_index_sort);
	unique_ptr<SortedRun> CreateRunForMaterialization() const;
	~SortedRun();

public:
	//! Appends data to key/data collections
	void Sink(DataChunk &key, DataChunk &payload);
	//! Sorts the data (physically reorder data if external)
	void Finalize(bool external);
	//! Destroy data between these tuple indices
	void DestroyData(idx_t tuple_idx_begin, idx_t tuple_idx_end);
	//! Number of tuples
	idx_t Count() const;
	//! Size of this sorted run
	idx_t SizeInBytes() const;

private:
	mutex merger_global_state_lock;
	unique_ptr<GlobalSourceState> merge_global_state;

public:
	ClientContext &context;
	const Sort &sort;

	//! Key and payload collections (and associated append states)
	unique_ptr<TupleDataCollection> key_data;
	unique_ptr<TupleDataCollection> payload_data;
	TupleDataAppendState key_append_state;
	TupleDataAppendState payload_append_state;

	//! Whether this is an (approximate) index sort
	const bool is_index_sort;

	//! Whether this run has been finalized
	bool finalized;

	std::optional<unsafe_vector<idx_t>> pos_list;
};

} // namespace duckdb
