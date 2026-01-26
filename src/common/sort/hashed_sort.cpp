#include "duckdb/common/sorting/hashed_sort.hpp"
#include "duckdb/common/sorting/sorted_run.hpp"
#include "duckdb/common/radix_partitioning.hpp"
#include "duckdb/parallel/base_pipeline_event.hpp"
#include "duckdb/parallel/thread_context.hpp"
#include "duckdb/planner/expression/bound_reference_expression.hpp"
#include "duckdb/execution/operator/aggregate/window_operator_config.hpp"
#include "duckdb/common/types/hyperloglog.hpp"

#include <iostream>

namespace duckdb {

//===--------------------------------------------------------------------===//
// HashedSortGroup
//===--------------------------------------------------------------------===//
// Formerly PartitionGlobalHashGroup
class HashedSortGroup {
public:
	using Orders = vector<BoundOrderByNode>;
	using Types = vector<LogicalType>;

	HashedSortGroup(ClientContext &client, optional_ptr<Sort> sort, idx_t group_idx);

	const idx_t group_idx;

	//	Sink
	optional_ptr<Sort> sort;
	unique_ptr<GlobalSinkState> sort_global;

	//	Source
	atomic<idx_t> tasks_completed;
	unique_ptr<GlobalSourceState> sort_source;

	unique_ptr<ColumnDataCollection> columns;
	unique_ptr<SortedRun> run;
};

HashedSortGroup::HashedSortGroup(ClientContext &client, optional_ptr<Sort> sort, idx_t group_idx)
    : group_idx(group_idx), sort(sort), tasks_completed(0) {
	if (sort) {
		sort_global = sort->GetGlobalSinkState(client);
	}
}

class HashedSortLocalSinkState;

//===--------------------------------------------------------------------===//
// HashedSortGlobalSinkState
//===--------------------------------------------------------------------===//
// Formerly PartitionGlobalSinkState
class HashedSortGlobalSinkState : public GlobalSinkState {
public:
	using HashGroupPtr = unique_ptr<HashedSortGroup>;
	using Orders = vector<BoundOrderByNode>;
	using Types = vector<LogicalType>;

	using GroupingPartition = unique_ptr<RadixPartitionedTupleData>;
	using GroupingAppend = unique_ptr<PartitionedTupleDataAppendState>;

	HashedSortGlobalSinkState(ClientContext &client, const HashedSort &hashed_sort);

	// OVER(PARTITION BY...) (hash grouping)
	unique_ptr<RadixPartitionedTupleData> CreatePartition(idx_t new_bits) const;
	void SyncPartitioning(const HashedSortGlobalSinkState &other);
	void UpdateLocalPartition(GroupingPartition &local_partition, GroupingAppend &partition_append);
	void UpdateLocalPartition(GroupingPartition &local_partition, GroupingAppend &partition_append, HashedSortLocalSinkState& lstate);
	void CombineLocalPartition(GroupingPartition &local_partition, GroupingAppend &local_append);
	ProgressData GetSinkProgress(ClientContext &context, const ProgressData source_progress) const;

	//! System and query state
	const HashedSort &hashed_sort;
	BufferManager &buffer_manager;
	Allocator &allocator;
	mutable mutex lock;

	// OVER(PARTITION BY...) (hash grouping)
	GroupingPartition grouping_data;
	//! Payload plus hash column
	shared_ptr<TupleDataLayout> grouping_types_ptr;
	//! The number of radix bits if this partition is being synced with another
	idx_t fixed_bits;

	// OVER(...) (sorting)
	vector<HashGroupPtr> hash_groups;

	// Threading
	idx_t max_bits;
	atomic<idx_t> count;

private:
	void Rehash(idx_t cardinality);
	void SyncLocalPartition(GroupingPartition &local_partition, GroupingAppend &local_append);
};

HashedSortGlobalSinkState::HashedSortGlobalSinkState(ClientContext &client, const HashedSort &hashed_sort)
    : hashed_sort(hashed_sort), buffer_manager(BufferManager::GetBufferManager(client)),
      allocator(Allocator::Get(client)), fixed_bits(0), max_bits(1), count(0) {
	const auto memory_per_thread = PhysicalOperator::GetMaxThreadMemory(client);
	const auto thread_pages = PreviousPowerOfTwo(memory_per_thread / (4 * buffer_manager.GetBlockAllocSize()));
	while (max_bits < 8 && (thread_pages >> max_bits) > 1) {
		++max_bits;
	}

	grouping_types_ptr = make_shared_ptr<TupleDataLayout>();
	auto &partitions = hashed_sort.partitions;
	auto &orders = hashed_sort.orders;
	auto &payload_types = hashed_sort.payload_types;
	if (!orders.empty()) {
		if (partitions.empty()) {
			//	Sort early into a dedicated hash group if we only sort.
			grouping_types_ptr->Initialize(payload_types, TupleDataValidityType::CAN_HAVE_NULL_VALUES);
			auto new_group = make_uniq<HashedSortGroup>(hashed_sort.client, *hashed_sort.sort, idx_t(0));
			hash_groups.emplace_back(std::move(new_group));
		} else {
			auto types = payload_types;
			types.push_back(LogicalType::HASH);
			grouping_types_ptr->Initialize(types, TupleDataValidityType::CAN_HAVE_NULL_VALUES);
			Rehash(hashed_sort.estimated_cardinality);
		}
	}
}

unique_ptr<RadixPartitionedTupleData> HashedSortGlobalSinkState::CreatePartition(idx_t new_bits) const {
	auto &payload_types = hashed_sort.payload_types;
	const auto hash_col_idx = payload_types.size();
	return make_uniq<RadixPartitionedTupleData>(buffer_manager, grouping_types_ptr, new_bits, hash_col_idx);
}

void HashedSortGlobalSinkState::Rehash(idx_t cardinality) {
	//	Have we started to combine? Then just live with it.
	if (fixed_bits) {
		return;
	}
	//	Is the average partition size too large?
	const idx_t partition_size = DEFAULT_ROW_GROUP_SIZE;
	const auto bits = grouping_data ? grouping_data->GetRadixBits() : 0;
	auto new_bits = bits ? bits : 4;
	while (new_bits < max_bits && (cardinality / RadixPartitioning::NumberOfPartitions(new_bits)) > partition_size) {
		++new_bits;
	}

	// Repartition the grouping data
	if (new_bits != bits) {
		grouping_data = CreatePartition(new_bits);
	}
}

void HashedSortGlobalSinkState::SyncLocalPartition(GroupingPartition &local_partition, GroupingAppend &local_append) {
	// We are done if the local_partition is right sized.
	const auto new_bits = grouping_data->GetRadixBits();
	if (local_partition->GetRadixBits() == new_bits) {
		return;
	}

	// If the local partition is now too small, flush it and reallocate
	auto new_partition = CreatePartition(new_bits);
	local_partition->FlushAppendState(*local_append);
	local_partition->Repartition(hashed_sort.client, *new_partition);

	local_partition = std::move(new_partition);
	local_append = make_uniq<PartitionedTupleDataAppendState>();
	local_partition->InitializeAppendState(*local_append);
}

void HashedSortGlobalSinkState::UpdateLocalPartition(GroupingPartition &local_partition,
                                                     GroupingAppend &partition_append) {
	// Make sure grouping_data doesn't change under us.
	lock_guard<mutex> guard(lock);

	if (!local_partition) {
		local_partition = CreatePartition(grouping_data->GetRadixBits());
		partition_append = make_uniq<PartitionedTupleDataAppendState>();
		local_partition->InitializeAppendState(*partition_append);
		return;
	}

	// 	Grow the groups if they are too big
	Rehash(count);

	//	Sync local partition to have the same bit count
	SyncLocalPartition(local_partition, partition_append);
}

void HashedSortGlobalSinkState::SyncPartitioning(const HashedSortGlobalSinkState &other) {
	fixed_bits = other.grouping_data ? other.grouping_data->GetRadixBits() : 0;

	const auto old_bits = grouping_data ? grouping_data->GetRadixBits() : 0;
	if (fixed_bits != old_bits) {
		grouping_data = CreatePartition(fixed_bits);
	}
}

void HashedSortGlobalSinkState::CombineLocalPartition(GroupingPartition &local_partition,
                                                      GroupingAppend &local_append) {
	if (!local_partition) {
		return;
	}
	local_partition->FlushAppendState(*local_append);

	// Make sure grouping_data doesn't change under us.
	// Combine has an internal mutex, so this is single-threaded anyway.
	lock_guard<mutex> guard(lock);
	SyncLocalPartition(local_partition, local_append);
	fixed_bits = true;

	//	We now know the number of hash_groups (some may be empty)
	auto &groups = local_partition->GetPartitions();
	if (hash_groups.empty()) {
		hash_groups.resize(groups.size());
	}

	//	Create missing HashedSortGroups inside the mutex
	for (idx_t group_idx = 0; group_idx < groups.size(); ++group_idx) {
		auto &hash_group = hash_groups[group_idx];
		if (hash_group) {
			continue;
		}

		auto &group_data = groups[group_idx];
		if (group_data->Count()) {
			hash_group = make_uniq<HashedSortGroup>(hashed_sort.client, *hashed_sort.sort, group_idx);
		}
	}
}

ProgressData HashedSortGlobalSinkState::GetSinkProgress(ClientContext &client, const ProgressData source) const {
	ProgressData result;
	result.done = source.done / 2;
	result.total = source.total;
	result.invalid = source.invalid;

	// Sort::GetSinkProgress assumes that there is only 1 sort.
	// So we just use it to figure out how many rows have been sorted.
	const ProgressData zero_progress;
	lock_guard<mutex> guard(lock);
	const auto &sort = hashed_sort.sort;
	for (auto &hash_group : hash_groups) {
		if (!hash_group || !hash_group->sort_global) {
			continue;
		}

		const auto group_progress = sort->GetSinkProgress(client, *hash_group->sort_global, zero_progress);
		result.done += group_progress.done;
		result.invalid = result.invalid || group_progress.invalid;
	}

	return result;
}

SinkFinalizeType HashedSort::Finalize(ClientContext &client, OperatorSinkFinalizeInput &finalize) const {
	auto &gsink = finalize.global_state.Cast<HashedSortGlobalSinkState>();

	//	Did we get any data?
	if (!gsink.count) {
		return SinkFinalizeType::NO_OUTPUT_POSSIBLE;
	}

	// OVER()
	if (!sort) {
		return SinkFinalizeType::READY;
	}

	// OVER(...)
	D_ASSERT(!gsink.hash_groups.empty());
	for (auto &hash_group : gsink.hash_groups) {
		if (!hash_group) {
			continue;
		}
		OperatorSinkFinalizeInput hfinalize {*hash_group->sort_global, finalize.interrupt_state};
		sort->Finalize(client, hfinalize);
	}

	return SinkFinalizeType::READY;
}

ProgressData HashedSort::GetSinkProgress(ClientContext &client, GlobalSinkState &gstate,
                                         const ProgressData source) const {
	auto &gsink = gstate.Cast<HashedSortGlobalSinkState>();
	return gsink.GetSinkProgress(client, source);
}

//===--------------------------------------------------------------------===//
// HashedSortLocalSinkState
//===--------------------------------------------------------------------===//
// Formerly PartitionLocalSinkState
class HashedSortLocalSinkState : public LocalSinkState {
public:
	using LocalSortStatePtr = unique_ptr<LocalSinkState>;
	using GroupingPartition = unique_ptr<RadixPartitionedTupleData>;
	using GroupingAppend = unique_ptr<PartitionedTupleDataAppendState>;

	HashedSortLocalSinkState(ExecutionContext &context, const HashedSort &hashed_sort);
	~HashedSortLocalSinkState();

	//! Global state
	const HashedSort &hashed_sort;
	Allocator &allocator;

	//! Shared expression evaluation
	ExpressionExecutor hash_exec;
	ExpressionExecutor sort_exec;
	DataChunk group_chunk;
	DataChunk sort_chunk;
	DataChunk payload_chunk;

	//! Compute the hash values
	void Hash(DataChunk &input_chunk, Vector &hash_vector);
	//! Merge the state into the global state.
	void Combine(ExecutionContext &context);

	// OVER(PARTITION BY...) (hash grouping)
	GroupingPartition local_grouping;
	GroupingAppend grouping_append;

	// OVER(ORDER BY...) (only sorting)
	LocalSortStatePtr sort_local;
	InterruptState interrupt;

	// OVER() (no sorting)
	unique_ptr<ColumnDataCollection> unsorted;
	ColumnDataAppendState unsorted_append;

	struct MinContainer {
		std::priority_queue<int32_t> values;
		int32_t max_value{std::numeric_limits<int32_t>::min()};
	};
	std::unordered_map<hash_t, MinContainer> minima;
	idx_t local_partition_count{0};

	HyperLogLog unique_partition_values;
	idx_t hashed_tuples{0};

	idx_t my_id{++WindowOperatorConfig::get().idx};
	std::optional<TimePoint> sink_begin;
	TimePoint sink_end;
	std::optional<TimePoint> sort_begin;
	TimePoint sort_end;
};

HashedSortLocalSinkState::HashedSortLocalSinkState(ExecutionContext &context, const HashedSort &hashed_sort)
    : hashed_sort(hashed_sort), allocator(Allocator::Get(context.client)), hash_exec(context.client),
      sort_exec(context.client) {
	vector<LogicalType> group_types;
	for (idx_t prt_idx = 0; prt_idx < hashed_sort.partitions.size(); prt_idx++) {
		auto &pexpr = *hashed_sort.partitions[prt_idx].expression.get();
		group_types.push_back(pexpr.return_type);
		hash_exec.AddExpression(pexpr);
	}

	vector<LogicalType> sort_types;
	for (const auto &expr : hashed_sort.sort_exprs) {
		sort_types.emplace_back(expr->return_type);
		sort_exec.AddExpression(*expr);
	}
	sort_chunk.Initialize(context.client, sort_types);

	if (hashed_sort.sort_col_count) {
		auto payload_types = hashed_sort.payload_types;
		if (!group_types.empty()) {
			// OVER(PARTITION BY...)
			group_chunk.Initialize(allocator, group_types);
			payload_types.emplace_back(LogicalType::HASH);
		} else {
			// OVER(ORDER BY...)
			for (idx_t ord_idx = 0; ord_idx < hashed_sort.orders.size(); ord_idx++) {
				auto &pexpr = *hashed_sort.orders[ord_idx].expression.get();
				group_types.push_back(pexpr.return_type);
				hash_exec.AddExpression(pexpr);
			}
			group_chunk.Initialize(allocator, group_types);

			//	Single partition
			auto &sort = *hashed_sort.sort;
			sort_local = sort.GetLocalSinkState(context);
		}
		// OVER(...)
		payload_chunk.Initialize(allocator, payload_types);
	} else {
		unsorted = make_uniq<ColumnDataCollection>(context.client, hashed_sort.payload_types);
		unsorted->InitializeAppend(unsorted_append);
	}
}

HashedSortLocalSinkState::~HashedSortLocalSinkState() {
	auto& conf = WindowOperatorConfig::get();
	const auto pos = my_id % conf.threads;
	conf.partition_times[pos] = std::make_pair(*sink_begin, sink_end);
	conf.local_sort_times[pos] = std::make_pair(*sort_begin, sort_end);
}

void HashedSort::Synchronize(const GlobalSinkState &source, GlobalSinkState &target) const {
	auto &src = source.Cast<HashedSortGlobalSinkState>();
	auto &tgt = target.Cast<HashedSortGlobalSinkState>();
	tgt.SyncPartitioning(src);
}

void HashedSortLocalSinkState::Hash(DataChunk &input_chunk, Vector &hash_vector) {
	const auto count = input_chunk.size();
	D_ASSERT(group_chunk.ColumnCount() > 0);

	// OVER(PARTITION BY...) (hash grouping)
	group_chunk.Reset();
	hash_exec.Execute(input_chunk, group_chunk);
	VectorOperations::Hash(group_chunk.data[0], hash_vector, count);
	if (WindowOperatorConfig::get().shrink_partitions_adaptive) {
		if (WindowOperatorConfig::get().use_adaptivity_context) {
			VectorOperations::HashAndCount(group_chunk.data[0], hash_vector, count, unique_partition_values);
			local_partition_count = unique_partition_values.Count();
			hashed_tuples += input_chunk.size();
			// std::cout << std::to_string(local_partition_count) + "\n";
		} else {
			auto current_unique_values = HyperLogLog{};
			VectorOperations::HashAndCount(group_chunk.data[0], hash_vector, count, current_unique_values);
			local_partition_count = current_unique_values.Count();
		}
	} else {
		VectorOperations::Hash(group_chunk.data[0], hash_vector, count);
	}
	for (idx_t prt_idx = 1; prt_idx < group_chunk.ColumnCount(); ++prt_idx) {
		VectorOperations::CombineHash(hash_vector, group_chunk.data[prt_idx], count);
	}
}

void HashedSortGlobalSinkState::UpdateLocalPartition(GroupingPartition &local_partition,
                                                     GroupingAppend &partition_append,
                                                     HashedSortLocalSinkState& lstate) {
	// Make sure grouping_data doesn't change under us.
	lock_guard<mutex> guard(lock);

	if (!local_partition) {
		local_partition = CreatePartition(grouping_data->GetRadixBits());
		partition_append = make_uniq<PartitionedTupleDataAppendState>();
		local_partition->InitializeAppendState(*partition_append);
		return;
	}

	// 	Grow the groups if they are too big
	Rehash(count);

	//	Sync local partition to have the same bit count
	SyncLocalPartition(local_partition, partition_append);
}


//! Templated radix partitioning constants, can be templated to the number of radix bits
template <idx_t radix_bits>
struct RadixPartitioningConstants {
public:
	//! Bitmask of the upper bits starting at the 5th byte
	static constexpr idx_t NUM_PARTITIONS = RadixPartitioning::NumberOfPartitions(radix_bits);
	static constexpr idx_t SHIFT = RadixPartitioning::Shift(radix_bits);
	static constexpr hash_t MASK = RadixPartitioning::Mask(radix_bits);

public:
	//! Apply bitmask and right shift to get a number between 0 and NUM_PARTITIONS
	static hash_t ApplyMask(const hash_t hash) {
		D_ASSERT((hash & MASK) >> SHIFT < NUM_PARTITIONS);
		return (hash & MASK) >> SHIFT;
	}
};

template <typename Functor>
void resolve_radix_bits(idx_t radix_bits, const Functor& fn) {
	switch (radix_bits) {
		case 4:
			fn(RadixPartitioningConstants<4>{});
			break;
		case 5:
			fn(RadixPartitioningConstants<5>{});
			break;
		case 6:
			fn(RadixPartitioningConstants<6>{});
			break;
		case 7:
			fn(RadixPartitioningConstants<7>{});
			break;
		case 8:
			fn(RadixPartitioningConstants<8>{});
			break;
		default:
			throw std::runtime_error("Unexpected number of radix bits: " + std::to_string(radix_bits));
	}
}

SinkResultType HashedSort::Sink(ExecutionContext &context, DataChunk &input_chunk, OperatorSinkInput &sink) const {
	auto &gstate = sink.global_state.Cast<HashedSortGlobalSinkState>();
	auto &lstate = sink.local_state.Cast<HashedSortLocalSinkState>();
	gstate.count += input_chunk.size();

	if (!lstate.sink_begin) {
		lstate.sink_begin = SteadyClock::now();
	}

	// Window::Sink:
	// PartitionedTupleData::Append
	// Sort::Sink
	// ColumnDataCollection::Append

	// OVER()
	if (gstate.hashed_sort.sort_col_count == 0) {
		lstate.unsorted->Append(lstate.unsorted_append, input_chunk);
		return SinkResultType::NEED_MORE_INPUT;
	}

	//	Payload prefix is the input data
	auto &payload_chunk = lstate.payload_chunk;
	payload_chunk.Reset();
	for (column_t i = 0; i < input_chunk.ColumnCount(); ++i) {
		payload_chunk.data[i].Reference(input_chunk.data[i]);
	}

	//	Compute any sort columns that are not references and append them to the end of the payload
	auto &sort_chunk = lstate.sort_chunk;
	auto &sort_exec = lstate.sort_exec;
	if (!sort_exprs.empty()) {
		sort_chunk.Reset();
		sort_exec.Execute(input_chunk, sort_chunk);
		for (column_t i = 0; i < sort_chunk.ColumnCount(); ++i) {
			payload_chunk.data[input_chunk.ColumnCount() + i].Reference(sort_chunk.data[i]);
		}
	}

	//	Append a forced payload column
	if (force_payload) {
		auto &vec = payload_chunk.data[input_chunk.ColumnCount() + sort_chunk.ColumnCount()];
		D_ASSERT(vec.GetType().id() == LogicalTypeId::BOOLEAN);
		vec.SetVectorType(VectorType::CONSTANT_VECTOR);
		ConstantVector::SetNull(vec, true);
	}

	payload_chunk.SetCardinality(input_chunk);

	//	OVER(ORDER BY...)
	auto &sort_local = lstate.sort_local;
	if (sort_local) {
		auto &hash_group = *gstate.hash_groups[0];
		OperatorSinkInput input {*hash_group.sort_global, *sort_local, sink.interrupt_state};
		sort->Sink(context, payload_chunk, input);
		return SinkResultType::NEED_MORE_INPUT;
	}

	// OVER(PARTITION BY...)
	auto &hash_vector = payload_chunk.data.back();
	lstate.Hash(input_chunk, hash_vector);
	for (idx_t col_idx = 0; col_idx < input_chunk.ColumnCount(); ++col_idx) {
		payload_chunk.data[col_idx].Reference(input_chunk.data[col_idx]);
	}

	auto &local_grouping = lstate.local_grouping;
	auto &grouping_append = lstate.grouping_append;

	auto adaptivity_skip = true;
	const auto& config = WindowOperatorConfig::get();
	if (config.shrink_partitions_adaptive) {
		const auto k = static_cast<idx_t>(config.predicate_value);
		const auto required_tuples = static_cast<double>(lstate.local_partition_count * k);
		const auto threshold = config.shrink_partitions_threshold;
		if (config.use_adaptivity_context) {
			adaptivity_skip = required_tuples / static_cast<double>(lstate.hashed_tuples) > threshold;
		} else {
			adaptivity_skip = static_cast<double>(required_tuples) / static_cast<double>(input_chunk.size()) >
			   threshold;
		}
	}
	if ((!config.simulate_shrink_partitions && !config.shrink_partitions) || (config.shrink_partitions_adaptive && adaptivity_skip)) {
		gstate.UpdateLocalPartition(local_grouping, grouping_append);
		local_grouping->Append(*grouping_append, payload_chunk);

		lstate.sink_end = SteadyClock::now();
		return SinkResultType::NEED_MORE_INPUT;
	}

	auto selection = SelectionVector{};
	auto result_count = idx_t{0};
	if (config.simulate_shrink_partitions) {
		gstate.UpdateLocalPartition(local_grouping, grouping_append);
		const auto required_tuples_per_partition = static_cast<uint64_t>(config.predicate_value) * config.expected_partitions;
		result_count = MinValue(input_chunk.size(), required_tuples_per_partition);
		selection.Initialize(result_count);
		for (idx_t i = 0; i < result_count; ++i) {
			selection.set_index(i, i);
		}
	} else {
		gstate.UpdateLocalPartition(local_grouping, grouping_append);
		const auto sort_column = sort_ids[partitions.size()];
		const auto value_count = static_cast<uint64_t>(config.predicate_value);
		const auto chunk_size = input_chunk.size();
		const auto hashes = FlatVector::GetData<const hash_t>(hash_vector);
		const auto sort_values = FlatVector::GetData<const int32_t>(input_chunk.data[sort_column]);

		selection.Initialize(chunk_size);
		for (auto i = idx_t{0}; i < chunk_size; ++i) {
			auto& local_minima = lstate.minima[hashes[i]];
			if (local_minima.values.size() < value_count) {
				local_minima.values.push(sort_values[i]);
				local_minima.max_value = MaxValue(local_minima.max_value, sort_values[i]);
				selection.set_index(result_count++, i);
				continue;
			}

			if (sort_values[i] <= local_minima.max_value) {
				selection.set_index(result_count++, i);
				local_minima.values.push(sort_values[i]);
				if (local_minima.values.size() > value_count) {
					local_minima.values.pop();
					local_minima.max_value = local_minima.values.top();
				}
			}
		}
	}

	local_grouping->Append(*grouping_append, payload_chunk, selection, result_count);
	lstate.sink_end = SteadyClock::now();

	return SinkResultType::NEED_MORE_INPUT;
}

SinkCombineResultType HashedSort::Combine(ExecutionContext &context, OperatorSinkCombineInput &combine) const {
	auto &gstate = combine.global_state.Cast<HashedSortGlobalSinkState>();
	auto &lstate = combine.local_state.Cast<HashedSortLocalSinkState>();
	const auto begin = SteadyClock::now();

	// Window::Combine:
	// Sort::Sink then Sort::Combine (per hash partition)
	// Sort::Combine
	// ColumnDataCollection::Combine

	// OVER()
	if (gstate.hashed_sort.sort_col_count == 0) {
		// Only one partition again, so need a global lock.
		lock_guard<mutex> glock(gstate.lock);
		auto &hash_groups = gstate.hash_groups;
		if (!hash_groups.empty()) {
			D_ASSERT(hash_groups.size() == 1);
			auto &unsorted = *hash_groups[0]->columns;
			if (lstate.unsorted) {
				unsorted.Combine(*lstate.unsorted);
				lstate.unsorted.reset();
			}
		} else {
			auto new_group = make_uniq<HashedSortGroup>(context.client, sort, idx_t(0));
			new_group->columns = std::move(lstate.unsorted);
			hash_groups.emplace_back(std::move(new_group));
		}
		return SinkCombineResultType::FINISHED;
	}

	//	OVER(ORDER BY...)
	if (lstate.sort_local) {
		auto &hash_group = *gstate.hash_groups[0];
		OperatorSinkCombineInput input {*hash_group.sort_global, *lstate.sort_local, combine.interrupt_state};
		sort->Combine(context, input);
		lstate.sort_local.reset();
		return SinkCombineResultType::FINISHED;
	}

	// OVER(PARTITION BY...)
	auto &local_grouping = lstate.local_grouping;
	if (!local_grouping) {
		return SinkCombineResultType::FINISHED;
	}

	// Flush our data and lock the bit count
	auto &grouping_append = lstate.grouping_append;
	gstate.CombineLocalPartition(local_grouping, grouping_append);

	//	Don't scan the hash column
	vector<column_t> column_ids;
	for (column_t i = 0; i < payload_types.size(); ++i) {
		column_ids.emplace_back(i);
	}

	//	Loop over the partitions and add them to each hash group's global sort state
	TupleDataScanState scan_state;
	DataChunk chunk;
	auto &partitions = local_grouping->GetPartitions();
	for (hash_t hash_bin = 0; hash_bin < partitions.size(); ++hash_bin) {
		auto &partition = *partitions[hash_bin];
		if (!partition.Count()) {
			continue;
		}

		partition.InitializeScan(scan_state, column_ids, TupleDataPinProperties::DESTROY_AFTER_DONE);
		if (chunk.data.empty()) {
			partition.InitializeScanChunk(scan_state, chunk);
		}

		auto &hash_group = *gstate.hash_groups[hash_bin];
		lstate.sort_local = sort->GetLocalSinkState(context);
		OperatorSinkInput sink {*hash_group.sort_global, *lstate.sort_local, combine.interrupt_state};
		while (partition.Scan(scan_state, chunk)) {
			sort->Sink(context, chunk, sink);
		}

		OperatorSinkCombineInput lcombine {*hash_group.sort_global, *lstate.sort_local, combine.interrupt_state};
		sort->Combine(context, lcombine);
	}

	const auto end = SteadyClock::now();
	if (!lstate.sort_begin) {
		lstate.sort_begin = begin;
	}
	lstate.sort_end = end;

	return SinkCombineResultType::FINISHED;
}

//===--------------------------------------------------------------------===//
// HashedSortMaterializeTask
//===--------------------------------------------------------------------===//
class HashedSortMaterializeTask : public ExecutorTask {
public:
	HashedSortMaterializeTask(Pipeline &pipeline, shared_ptr<Event> event, const PhysicalOperator &op,
	                          HashedSortGroup &hash_group, idx_t tasks_scheduled, bool build_runs);

	TaskExecutionResult ExecuteTask(TaskExecutionMode mode) override;

	string TaskType() const override {
		return "HashedSortMaterializeTask";
	}

private:
	Pipeline &pipeline;
	HashedSortGroup &hash_group;
	const idx_t tasks_scheduled;
	const bool build_runs;
};

HashedSortMaterializeTask::HashedSortMaterializeTask(Pipeline &pipeline, shared_ptr<Event> event,
                                                     const PhysicalOperator &op, HashedSortGroup &hash_group,
                                                     idx_t tasks_scheduled, bool build_runs)
    : ExecutorTask(pipeline.GetClientContext(), std::move(event), op), pipeline(pipeline), hash_group(hash_group),
      tasks_scheduled(tasks_scheduled), build_runs(build_runs) {
}

TaskExecutionResult HashedSortMaterializeTask::ExecuteTask(TaskExecutionMode mode) {
	ExecutionContext execution(pipeline.GetClientContext(), *thread_context, &pipeline);
	auto &sort = *hash_group.sort;
	auto &sort_global = *hash_group.sort_source;
	auto sort_local = sort.GetLocalSourceState(execution, sort_global);
	InterruptState interrupt((weak_ptr<Task>(shared_from_this())));
	OperatorSourceInput input {sort_global, *sort_local, interrupt};
	if (build_runs) {
		sort.MaterializeSortedRun(execution, input);
	} else {
		sort.MaterializeColumnData(execution, input);
	}
	if (++hash_group.tasks_completed == tasks_scheduled) {
		if (build_runs) {
			hash_group.run = sort.GetSortedRun(sort_global);
			if (!hash_group.run) {
				hash_group.run = make_uniq<SortedRun>(execution.client, sort, false);
			}
		} else {
			hash_group.columns = sort.GetColumnData(input);
		}
	}

	event->FinishTask();
	return TaskExecutionResult::TASK_FINISHED;
}

//===--------------------------------------------------------------------===//
// HashedSortMaterializeEvent
//===--------------------------------------------------------------------===//
// Formerly PartitionMergeEvent
class HashedSortMaterializeEvent : public BasePipelineEvent {
public:
	HashedSortMaterializeEvent(HashedSortGlobalSinkState &gstate, Pipeline &pipeline, const PhysicalOperator &op,
	                           bool build_runs);

	HashedSortGlobalSinkState &gstate;
	const PhysicalOperator &op;
	const bool build_runs;

public:
	void Schedule() override;
};

HashedSortMaterializeEvent::HashedSortMaterializeEvent(HashedSortGlobalSinkState &gstate, Pipeline &pipeline,
                                                       const PhysicalOperator &op, bool build_runs)
    : BasePipelineEvent(pipeline), gstate(gstate), op(op), build_runs(build_runs) {
}

void HashedSortMaterializeEvent::Schedule() {
	auto &client = pipeline->GetClientContext();

	// Schedule as many tasks per hash group as the sort will allow
	auto &ts = TaskScheduler::GetScheduler(client);
	const auto num_threads = NumericCast<idx_t>(ts.NumberOfThreads());
	auto &sort = *gstate.hashed_sort.sort;

	vector<shared_ptr<Task>> tasks;
	for (auto &hash_group : gstate.hash_groups) {
		if (!hash_group) {
			continue;
		}
		auto &global_sink = *hash_group->sort_global;
		hash_group->sort_source = sort.GetGlobalSourceState(client, global_sink);
		const auto tasks_scheduled = MinValue<idx_t>(num_threads, hash_group->sort_source->MaxThreads());
		for (idx_t t = 0; t < tasks_scheduled; ++t) {
			tasks.emplace_back(make_uniq<HashedSortMaterializeTask>(*pipeline, shared_from_this(), op, *hash_group,
			                                                        tasks_scheduled, build_runs));
		}
	}

	SetTasks(std::move(tasks));
}

//===--------------------------------------------------------------------===//
// HashedSortGlobalSourceState
//===--------------------------------------------------------------------===//
class HashedSortGlobalSourceState : public GlobalSourceState {
public:
	using HashGroupPtr = unique_ptr<ColumnDataCollection>;
	using SortedRunPtr = unique_ptr<SortedRun>;

	HashedSortGlobalSourceState(ClientContext &client, HashedSortGlobalSinkState &gsink) {
		if (!gsink.count) {
			return;
		}
		columns.resize(gsink.hash_groups.size());
		runs.resize(gsink.hash_groups.size());
		for (auto &hash_group : gsink.hash_groups) {
			if (!hash_group) {
				continue;
			}
			const auto group_idx = hash_group->group_idx;
			columns[group_idx] = std::move(hash_group->columns);
			runs[group_idx] = std::move(hash_group->run);
		}
	}

	vector<HashGroupPtr> columns;
	vector<SortedRunPtr> runs;
};

//===--------------------------------------------------------------------===//
// HashedSort
//===--------------------------------------------------------------------===//
void HashedSort::GenerateOrderings(Orders &partitions, Orders &orders,
                                   const vector<unique_ptr<Expression>> &partition_bys, const Orders &order_bys,
                                   const vector<unique_ptr<BaseStatistics>> &partition_stats) {
	// we sort by both 1) partition by expression list and 2) order by expressions
	const auto partition_cols = partition_bys.size();
	for (idx_t prt_idx = 0; prt_idx < partition_cols; prt_idx++) {
		auto &pexpr = partition_bys[prt_idx];

		if (partition_stats.empty() || !partition_stats[prt_idx]) {
			orders.emplace_back(OrderType::ASCENDING, OrderByNullType::NULLS_FIRST, pexpr->Copy(), nullptr);
		} else {
			orders.emplace_back(OrderType::ASCENDING, OrderByNullType::NULLS_FIRST, pexpr->Copy(),
			                    partition_stats[prt_idx]->ToUnique());
		}
		partitions.emplace_back(orders.back().Copy());
	}

	for (const auto &order : order_bys) {
		orders.emplace_back(order.Copy());
	}
}

HashedSort::HashedSort(ClientContext &client, const vector<unique_ptr<Expression>> &partition_bys,
                       const vector<BoundOrderByNode> &order_bys, const Types &input_types,
                       const vector<unique_ptr<BaseStatistics>> &partition_stats, idx_t estimated_cardinality,
                       bool require_payload)
    : client(client), estimated_cardinality(estimated_cardinality), payload_types(input_types) {
	GenerateOrderings(partitions, orders, partition_bys, order_bys, partition_stats);

	// The payload prefix is the same as the input schema
	for (column_t i = 0; i < payload_types.size(); ++i) {
		scan_ids.emplace_back(i);
	}

	//	We have to compute ordering expressions ourselves and materialise them.
	//	To do this, we scan the orders and add generate extra payload columns that we can reference.
	for (auto &order : orders) {
		auto &expr = *order.expression;
		if (expr.GetExpressionClass() == ExpressionClass::BOUND_REF) {
			auto &ref = expr.Cast<BoundReferenceExpression>();
			sort_ids.emplace_back(ref.index);
			continue;
		}

		//	Real expression - replace with a ref and save the expression
		auto saved = std::move(order.expression);
		const auto type = saved->return_type;
		const auto idx = payload_types.size();
		order.expression = make_uniq<BoundReferenceExpression>(type, idx);
		sort_ids.emplace_back(idx);
		payload_types.emplace_back(type);
		sort_exprs.emplace_back(std::move(saved));
	}

	sort_col_count = orders.size() + partitions.size();
	if (sort_col_count) {
		// If a payload column is required, check whether there is one already
		if (require_payload) {
			//	Watch out for duplicate sort keys!
			unordered_set<column_t> sort_set(sort_ids.begin(), sort_ids.end());
			force_payload = (sort_set.size() >= payload_types.size());
			if (force_payload) {
				payload_types.emplace_back(LogicalType::BOOLEAN);
			}
		}
		vector<idx_t> projection_map;
		sort = make_uniq<Sort>(client, orders, payload_types, projection_map);
	}
}

unique_ptr<GlobalSinkState> HashedSort::GetGlobalSinkState(ClientContext &client) const {
	return make_uniq<HashedSortGlobalSinkState>(client, *this);
}

unique_ptr<LocalSinkState> HashedSort::GetLocalSinkState(ExecutionContext &context) const {
	return make_uniq<HashedSortLocalSinkState>(context, *this);
}

unique_ptr<GlobalSourceState> HashedSort::GetGlobalSourceState(ClientContext &context, GlobalSinkState &sink) const {
	return make_uniq<HashedSortGlobalSourceState>(client, sink.Cast<HashedSortGlobalSinkState>());
}

unique_ptr<LocalSourceState> HashedSort::GetLocalSourceState(ExecutionContext &context,
                                                             GlobalSourceState &gstate) const {
	return make_uniq<LocalSourceState>();
}

vector<HashedSort::HashGroupPtr> &HashedSort::GetHashGroups(GlobalSourceState &gstate) const {
	auto &gsource = gstate.Cast<HashedSortGlobalSourceState>();
	return gsource.columns;
}

SinkFinalizeType HashedSort::MaterializeHashGroups(Pipeline &pipeline, Event &event, const PhysicalOperator &op,
                                                   OperatorSinkFinalizeInput &finalize) const {
	auto &gsink = finalize.global_state.Cast<HashedSortGlobalSinkState>();

	// OVER()
	if (sort_col_count == 0) {
		auto &hash_group = *gsink.hash_groups[0];
		auto &unsorted = *hash_group.columns;
		if (!unsorted.Count()) {
			return SinkFinalizeType::NO_OUTPUT_POSSIBLE;
		}
		return SinkFinalizeType::READY;
	}

	// Schedule all the sorts for maximum thread utilisation
	auto sort_event = make_shared_ptr<HashedSortMaterializeEvent>(gsink, pipeline, op, false);
	event.InsertEvent(std::move(sort_event));

	return SinkFinalizeType::READY;
}

SinkFinalizeType HashedSort::MaterializeSortedRuns(Pipeline &pipeline, Event &event, const PhysicalOperator &op,
                                                   OperatorSinkFinalizeInput &finalize) const {
	auto &gsink = finalize.global_state.Cast<HashedSortGlobalSinkState>();

	// OVER()
	if (sort_col_count == 0) {
		auto &hash_group = *gsink.hash_groups[0];
		auto &unsorted = *hash_group.columns;
		if (!unsorted.Count()) {
			return SinkFinalizeType::NO_OUTPUT_POSSIBLE;
		}
		return SinkFinalizeType::READY;
	}

	// Schedule all the sorts for maximum thread utilisation
	auto sort_event = make_shared_ptr<HashedSortMaterializeEvent>(gsink, pipeline, op, true);
	event.InsertEvent(std::move(sort_event));

	return SinkFinalizeType::READY;
}

vector<HashedSort::SortedRunPtr> &HashedSort::GetSortedRuns(GlobalSourceState &gstate) const {
	auto &gsource = gstate.Cast<HashedSortGlobalSourceState>();
	return gsource.runs;
}

} // namespace duckdb
