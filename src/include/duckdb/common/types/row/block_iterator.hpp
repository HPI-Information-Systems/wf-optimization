//===----------------------------------------------------------------------===//
//                         DuckDB
//
// duckdb/common/types/row/block_iterator.hpp
//
//
//===----------------------------------------------------------------------===//

#pragma once

#include "duckdb/common/numeric_utils.hpp"
#include "duckdb/common/types/row/tuple_data_collection.hpp"
#include "duckdb/common/types/row/tuple_data_states.hpp"

#include <iostream>

namespace duckdb {

class TupleDataCollection;

enum class BlockIteratorStateType : int8_t {
	//! For InMemoryBlockIteratorState
	IN_MEMORY,
	//! For ExternalBlockIteratorState
	EXTERNAL,
};

static BlockIteratorStateType GetBlockIteratorStateType(const bool &external) {
	return external ? BlockIteratorStateType::EXTERNAL : BlockIteratorStateType::IN_MEMORY;
}

template <class BLOCK_ITERATOR_STATE>
class BlockIteratorStateBase {
protected:
	friend BLOCK_ITERATOR_STATE;
	explicit BlockIteratorStateBase(const idx_t tuple_count_p) : tuple_count(tuple_count_p) {
	}

public:
	idx_t GetDivisor() const {
		const auto &state = static_cast<const BLOCK_ITERATOR_STATE &>(*this);
		return state.GetDivisor();
	}

	void RandomAccess(idx_t &block_or_chunk_idx, idx_t &tuple_idx, const idx_t &index) const {
		const auto &state = static_cast<const BLOCK_ITERATOR_STATE &>(*this);
		state.RandomAccessInternal(block_or_chunk_idx, tuple_idx, index);
	}

	void Add(idx_t &block_or_chunk_idx, idx_t &tuple_idx, const idx_t &value) const {
		tuple_idx += value;
		if (tuple_idx >= GetDivisor()) {
			RandomAccess(block_or_chunk_idx, tuple_idx, GetIndex(block_or_chunk_idx, tuple_idx));
		}
	}

	void Subtract(idx_t &block_or_chunk_idx, idx_t &tuple_idx, const idx_t &value) const {
		tuple_idx -= value;
		if (tuple_idx >= GetDivisor()) {
			RandomAccess(block_or_chunk_idx, tuple_idx, GetIndex(block_or_chunk_idx, tuple_idx));
		}
	}

	void Increment(idx_t &block_or_chunk_idx, idx_t &tuple_idx) const {
		const auto crossed_boundary = ++tuple_idx == GetDivisor();
		block_or_chunk_idx += crossed_boundary;
		tuple_idx *= !crossed_boundary;
	}

	void Decrement(idx_t &block_or_chunk_idx, idx_t &tuple_idx) const {
		const auto crossed_boundary = tuple_idx-- == 0;
		block_or_chunk_idx -= crossed_boundary;
		tuple_idx += crossed_boundary * GetDivisor();
	}

	idx_t GetIndex(const idx_t &block_or_chunk_idx, const idx_t &tuple_idx) const {
		return block_or_chunk_idx * GetDivisor() + tuple_idx;
	}

protected:
	const idx_t tuple_count;
};

template <BlockIteratorStateType>
class BlockIteratorState;

//! State for iterating over blocks of an in-memory TupleDataCollection
//! Multiple iterators can share the same state, everything is const
template <>
class BlockIteratorState<BlockIteratorStateType::IN_MEMORY>
    : public BlockIteratorStateBase<BlockIteratorState<BlockIteratorStateType::IN_MEMORY>> {
public:
	explicit BlockIteratorState(const TupleDataCollection &key_data)
	    : BlockIteratorStateBase(key_data.Count()), block_ptrs(ConvertBlockPointers(key_data.GetRowBlockPointers())),
	      fast_mod(key_data.TuplesPerBlock()) {
	}

public:
	idx_t GetDivisor() const {
		return fast_mod.GetDivisor();
	}

	void RandomAccessInternal(idx_t &block_idx, idx_t &tuple_idx, const idx_t &index) const {
		block_idx = fast_mod.Div(index);
		tuple_idx = fast_mod.Mod(index, block_idx);
	}

	template <class T>
	T &GetValueAtIndex(const idx_t &block_idx, const idx_t &tuple_idx) const {
		D_ASSERT(GetIndex(block_idx, tuple_idx) < tuple_count);
		return reinterpret_cast<T *const>(block_ptrs[block_idx])[tuple_idx];
	}

	template <class T>
	T &GetValueAtIndex(const idx_t &index) const {
		idx_t block_idx;
		idx_t tuple_idx;
		RandomAccess(block_idx, tuple_idx, index);
		return GetValueAtIndex<T>(block_idx, tuple_idx);
	}

	void SetKeepPinned(const bool &) {
		// NOP
	}

	void SetPinPayload(const bool &) {
		// NOP
	}

private:
	static unsafe_vector<data_ptr_t> ConvertBlockPointers(const vector<data_ptr_t> &block_ptrs) {
		unsafe_vector<data_ptr_t> converted_block_ptrs;
		converted_block_ptrs.reserve(block_ptrs.size());
		for (const auto &block_ptr : block_ptrs) {
			converted_block_ptrs.emplace_back(block_ptr);
		}
		return converted_block_ptrs;
	}

private:
	const unsafe_vector<data_ptr_t> block_ptrs;
	const FastMod<idx_t> fast_mod;
};

using InMemoryBlockIteratorState = BlockIteratorState<BlockIteratorStateType::IN_MEMORY>;

class FilteredBlockIteratorState : public InMemoryBlockIteratorState {
public:
	explicit FilteredBlockIteratorState(const TupleDataCollection &key_data, const unsafe_vector<idx_t>& init_offsets)
	    : InMemoryBlockIteratorState(key_data), offsets(init_offsets) {
	}

	const unsafe_vector<idx_t>& offsets;
};

//! State for iterating over blocks of an external (larger-than-memory) TupleDataCollection
//! This state cannot be shared by multiple iterators, it is stateful
template <>
class BlockIteratorState<BlockIteratorStateType::EXTERNAL>
    : public BlockIteratorStateBase<BlockIteratorState<BlockIteratorStateType::EXTERNAL>> {
public:
	explicit BlockIteratorState(TupleDataCollection &key_data_p, optional_ptr<TupleDataCollection> payload_data_p)
	    : BlockIteratorStateBase(key_data_p.Count()), current_chunk_idx(DConstants::INVALID_INDEX),
	      key_data(key_data_p), key_ptrs(FlatVector::GetData<data_ptr_t>(key_scan_state.chunk_state.row_locations)),
	      payload_data(payload_data_p), keep_pinned(false), pin_payload(false) {
		key_data.InitializeScan(key_scan_state);
		if (payload_data) {
			payload_data->InitializeScan(payload_scan_state);
		}
	}

public:
	static constexpr idx_t GetDivisor() {
		return STANDARD_VECTOR_SIZE;
	}

	static void RandomAccessInternal(idx_t &chunk_idx, idx_t &tuple_idx, const idx_t &index) {
		chunk_idx = index / STANDARD_VECTOR_SIZE;
		tuple_idx = index % STANDARD_VECTOR_SIZE;
	}

	template <class T>
	T &GetValueAtIndex(const idx_t &chunk_idx, const idx_t &tuple_idx) {
		D_ASSERT(GetIndex(chunk_idx, tuple_idx) < tuple_count);
		if (chunk_idx != current_chunk_idx) {
			InitializeChunk<T>(chunk_idx);
		}
		return *reinterpret_cast<T **const>(key_ptrs)[tuple_idx];
	}

	template <class T>
	T &GetValueAtIndex(const idx_t &index) {
		idx_t chunk_idx;
		idx_t tuple_idx;
		RandomAccess(chunk_idx, tuple_idx, index);
		return GetValueAtIndex<T>(chunk_idx, tuple_idx);
	}

	void SetKeepPinned(const bool &enable) {
		keep_pinned = enable;
		// Always start with a clean slate when toggling
		pins.clear();
		key_scan_state.pin_state.row_handles.clear();
		key_scan_state.pin_state.heap_handles.clear();
		payload_scan_state.pin_state.row_handles.clear();
		payload_scan_state.pin_state.heap_handles.clear();
		current_chunk_idx = DConstants::INVALID_INDEX;
	}

	void SetPinPayload(const bool &enable) {
		pin_payload = enable;
	}

private:
	template <class T>
	void InitializeChunk(const idx_t &chunk_idx) {
		current_chunk_idx = chunk_idx;
		if (keep_pinned) {
			key_scan_state.pin_state.row_handles.acquire_handles(pins);
			key_scan_state.pin_state.heap_handles.acquire_handles(pins);
		}
		key_data.FetchChunk(key_scan_state, chunk_idx, false);
		if (pin_payload && payload_data) {
			if (keep_pinned) {
				payload_scan_state.pin_state.row_handles.acquire_handles(pins);
				payload_scan_state.pin_state.heap_handles.acquire_handles(pins);
			}
			const auto chunk_count = payload_data->FetchChunk(payload_scan_state, chunk_idx, false);
			const auto sort_keys = reinterpret_cast<T **const>(key_ptrs);
			const auto payload_ptrs = FlatVector::GetData<data_ptr_t>(payload_scan_state.chunk_state.row_locations);
			for (idx_t i = 0; i < chunk_count; i++) {
				sort_keys[i]->SetPayload(payload_ptrs[i]);
				D_ASSERT(GetValueAtIndex<T>(chunk_idx, i).GetPayload() == payload_ptrs[i]);
			}
		}
	}

private:
	idx_t current_chunk_idx;

	TupleDataCollection &key_data;
	TupleDataScanState key_scan_state;
	data_ptr_t *key_ptrs;

	optional_ptr<TupleDataCollection> payload_data;
	TupleDataScanState payload_scan_state;

	bool keep_pinned;
	bool pin_payload;
	vector<BufferHandle> pins;
};

using ExternalBlockIteratorState = BlockIteratorState<BlockIteratorStateType::EXTERNAL>;

//! Iterator for data spread out over multiple blocks
template <class STATE, class T>
class block_iterator_t { // NOLINT: match stl case
public:
	using iterator_category = std::random_access_iterator_tag;
	using value_type = T;
	using pointer = value_type *;
	using reference = value_type &;
	using difference_type = idx_t;
	using traits = std::iterator_traits<block_iterator_t>;

public:
	explicit block_iterator_t(STATE &state_p) : state(&state_p), block_or_chunk_idx(0), tuple_idx(0) {
	}

	explicit block_iterator_t()
	    : state(nullptr), block_or_chunk_idx(DConstants::INVALID_INDEX), tuple_idx(DConstants::INVALID_INDEX) {
		// Ideally, we wouldn't have a default constructor, because we always need a state.
		// However, some sorting algorithms use the default constructor before initializing, so we need this
	}

	block_iterator_t(STATE &state_p, const idx_t &index) : state(&state_p) { // NOLINT: uninitialized on purpose
		state->RandomAccess(block_or_chunk_idx, tuple_idx, index);
	}

	block_iterator_t(STATE &state_p, const idx_t &block_idx_p, const idx_t &tuple_idx_p)
	    : state(&state_p), block_or_chunk_idx(block_idx_p), tuple_idx(tuple_idx_p) {
	}

	block_iterator_t(const block_iterator_t &other)
	    : state(other.state), block_or_chunk_idx(other.block_or_chunk_idx), tuple_idx(other.tuple_idx) {
	}

	block_iterator_t &operator=(const block_iterator_t &other) {
		D_ASSERT(!state || RefersToSameObject(*state, *other.state));
		if (this != &other) { // This check is needed to shut clang-tidy up
			block_or_chunk_idx = other.block_or_chunk_idx;
			tuple_idx = other.tuple_idx;
		}
		return *this;
	}

public:
	//! (De-)referencing
	reference operator*() const {
		return state->template GetValueAtIndex<T>(block_or_chunk_idx, tuple_idx);
	}
	pointer operator->() const {
		return &operator*();
	}

	//! Prefix and postfix increment and decrement
	block_iterator_t &operator++() {
		state->Increment(block_or_chunk_idx, tuple_idx);
		return *this;
	}
	block_iterator_t operator++(int) {
		block_iterator_t tmp = *this;
		++(*this);
		return tmp;
	}
	block_iterator_t &operator--() {
		state->Decrement(block_or_chunk_idx, tuple_idx);
		return *this;
	}
	block_iterator_t operator--(int) {
		block_iterator_t tmp = *this;
		--(*this);
		return tmp;
	}

	//! Random access
	block_iterator_t &operator+=(const difference_type &n) {
		state->Add(block_or_chunk_idx, tuple_idx, n);
		return *this;
	}
	block_iterator_t &operator-=(const difference_type &n) {
		state->Subtract(block_or_chunk_idx, tuple_idx, n);
		return *this;
	}
	block_iterator_t operator+(const difference_type &n) const {
		idx_t new_block_or_chunk_idx = block_or_chunk_idx;
		idx_t new_tuple_idx = tuple_idx;
		state->Add(new_block_or_chunk_idx, new_tuple_idx, n);
		return block_iterator_t(*state, new_block_or_chunk_idx, new_tuple_idx);
	}
	block_iterator_t operator-(const difference_type &n) const {
		idx_t new_block_or_chunk_idx = block_or_chunk_idx;
		idx_t new_tuple_idx = tuple_idx;
		state->Subtract(new_block_or_chunk_idx, new_tuple_idx, n);
		return block_iterator_t(*state, new_block_or_chunk_idx, new_tuple_idx);
	}

	reference operator[](const difference_type &n) const {
		return state->template GetValueAtIndex<T>(n);
	}

	//! Difference between iterators
	difference_type operator-(const block_iterator_t &other) const {
		return state->GetIndex(block_or_chunk_idx, tuple_idx) -
		       other.state->GetIndex(other.block_or_chunk_idx, other.tuple_idx);
	}

	//! Comparison operators
	bool operator==(const block_iterator_t &other) const {
		return block_or_chunk_idx == other.block_or_chunk_idx && tuple_idx == other.tuple_idx;
	}
	bool operator!=(const block_iterator_t &other) const {
		return block_or_chunk_idx != other.block_or_chunk_idx || tuple_idx != other.tuple_idx;
	}
	bool operator<(const block_iterator_t &other) const {
		return block_or_chunk_idx == other.block_or_chunk_idx ? tuple_idx < other.tuple_idx
		                                                      : block_or_chunk_idx < other.block_or_chunk_idx;
	}
	bool operator>(const block_iterator_t &other) const {
		return block_or_chunk_idx == other.block_or_chunk_idx ? tuple_idx > other.tuple_idx
		                                                      : block_or_chunk_idx > other.block_or_chunk_idx;
	}
	bool operator<=(const block_iterator_t &other) const {
		return block_or_chunk_idx == other.block_or_chunk_idx ? tuple_idx <= other.tuple_idx
		                                                      : block_or_chunk_idx <= other.block_or_chunk_idx;
	}
	bool operator>=(const block_iterator_t &other) const {
		return block_or_chunk_idx == other.block_or_chunk_idx ? tuple_idx >= other.tuple_idx
		                                                      : block_or_chunk_idx >= other.block_or_chunk_idx;
	}

private:
	STATE *state;
	idx_t block_or_chunk_idx;
	idx_t tuple_idx;
};

template <class T>
class filtered_block_iterator_t { // NOLINT: match stl case
public:
	using iterator_category = std::random_access_iterator_tag;
	using value_type = T;
	using pointer = value_type *;
	using reference = value_type &;
	using difference_type = idx_t;
	using traits = std::iterator_traits<filtered_block_iterator_t>;
	using STATE = FilteredBlockIteratorState;

public:
	explicit filtered_block_iterator_t(STATE &state_p) : state(&state_p), block_or_chunk_idx(0), tuple_idx(0), current_pos(0) {
	}

	explicit filtered_block_iterator_t()
	    : state(nullptr), block_or_chunk_idx(DConstants::INVALID_INDEX), tuple_idx(DConstants::INVALID_INDEX), current_pos(DConstants::INVALID_INDEX) {
		// Ideally, we wouldn't have a default constructor, because we always need a state.
		// However, some sorting algorithms use the default constructor before initializing, so we need this
	}

	filtered_block_iterator_t(STATE &state_p, const idx_t &index) : state(&state_p) { // NOLINT: uninitialized on purpose
		current_pos = index;
		state->RandomAccess(block_or_chunk_idx, tuple_idx, state->offsets[current_pos]);
	}

	filtered_block_iterator_t(STATE &state_p, const idx_t &block_idx_p, const idx_t &tuple_idx_p, const idx_t current_pos_p)
	    : state(&state_p), block_or_chunk_idx(block_idx_p), tuple_idx(tuple_idx_p), current_pos(current_pos_p) {
	}

	filtered_block_iterator_t(const filtered_block_iterator_t &other)
	    : state(other.state), block_or_chunk_idx(other.block_or_chunk_idx), tuple_idx(other.tuple_idx), current_pos(other.current_pos) {
	}

	filtered_block_iterator_t &operator=(const filtered_block_iterator_t &other) {
		D_ASSERT(!state || RefersToSameObject(*state, *other.state));
		if (this != &other) { // This check is needed to shut clang-tidy up
			block_or_chunk_idx = other.block_or_chunk_idx;
			tuple_idx = other.tuple_idx;
			current_pos = other.current_pos;
		}
		return *this;
	}

public:
	//! (De-)referencing
	reference operator*() const {
		return state->template GetValueAtIndex<T>(block_or_chunk_idx, tuple_idx);
	}
	pointer operator->() const {
		return &operator*();
	}

	//! Prefix and postfix increment and decrement
	filtered_block_iterator_t &operator++() {
		const auto old = current_pos;
		if (current_pos < state->offsets.size()) {
			++current_pos;
		}
		state->Add(block_or_chunk_idx, tuple_idx, state->offsets[current_pos] - state->offsets[old]);
		return *this;
	}
	filtered_block_iterator_t operator++(int) {
		filtered_block_iterator_t tmp = *this;
		++(*this);
		return tmp;
	}
	filtered_block_iterator_t &operator--() {
		const auto old = current_pos;
		if (current_pos > 0) {
			--current_pos;
		}
		state->Subtract(block_or_chunk_idx, tuple_idx, state->offsets[old] - state->offsets[current_pos]);
		return *this;
	}
	filtered_block_iterator_t operator--(int) {
		block_iterator_t tmp = *this;
		--(*this);
		return tmp;
	}

	//! Random access
	filtered_block_iterator_t &operator+=(const difference_type &n) {
		current_pos = MinValue<idx_t>(current_pos + n, state->offsets.size());
		state->RandomAccess(block_or_chunk_idx, tuple_idx, state->offsets[current_pos]);
		return *this;
	}
	filtered_block_iterator_t &operator-=(const difference_type &n) {
		current_pos = n > current_pos ? 0 : current_pos - n;
		state->RandomAccess(block_or_chunk_idx, tuple_idx, state->offsets[current_pos]);
		return *this;
	}
	filtered_block_iterator_t operator+(const difference_type &n) const {
		idx_t new_block_or_chunk_idx = block_or_chunk_idx;
		idx_t new_tuple_idx = tuple_idx;
		auto new_pos = MinValue<idx_t>(current_pos + n, state->offsets.size());
		state->RandomAccess(new_block_or_chunk_idx, new_tuple_idx, state->offsets[new_pos]);
		return filtered_block_iterator_t(*state, new_block_or_chunk_idx, new_tuple_idx, new_pos);
	}
	filtered_block_iterator_t operator-(const difference_type &n) const {
		idx_t new_block_or_chunk_idx = block_or_chunk_idx;
		idx_t new_tuple_idx = tuple_idx;
		auto new_pos = n > current_pos ? 0 : current_pos - n;
		state->RandomAccess(new_block_or_chunk_idx, new_tuple_idx, state->offsets[new_pos]);
		return filtered_block_iterator_t(*state, new_block_or_chunk_idx, new_tuple_idx, new_pos);
	}

	reference operator[](const difference_type &n) const {
		return state->template GetValueAtIndex<T>(state->offsets[n]);
	}

	//! Difference between iterators
	difference_type operator-(const filtered_block_iterator_t &other) const {
		return static_cast<difference_type>(current_pos - other.current_pos);
		// return state->GetIndex(block_or_chunk_idx, tuple_idx) -
		//        other.state->GetIndex(other.block_or_chunk_idx, other.tuple_idx);
	}

	//! Comparison operators
	bool operator==(const filtered_block_iterator_t &other) const {
		return current_pos == other.current_pos;
		// return block_or_chunk_idx == other.block_or_chunk_idx && tuple_idx == other.tuple_idx;
	}
	bool operator!=(const filtered_block_iterator_t &other) const {
		return current_pos != other.current_pos;
		// return block_or_chunk_idx != other.block_or_chunk_idx || tuple_idx != other.tuple_idx;
	}
	bool operator<(const filtered_block_iterator_t &other) const {
		return current_pos < other.current_pos;
		// return block_or_chunk_idx == other.block_or_chunk_idx ? tuple_idx < other.tuple_idx
		//                                                       : block_or_chunk_idx < other.block_or_chunk_idx;
	}
	bool operator>(const filtered_block_iterator_t &other) const {
		return current_pos > other.current_pos;
		// return block_or_chunk_idx == other.block_or_chunk_idx ? tuple_idx > other.tuple_idx
		//                                                       : block_or_chunk_idx > other.block_or_chunk_idx;
	}
	bool operator<=(const filtered_block_iterator_t &other) const {
		return current_pos <= other.current_pos;
		// return block_or_chunk_idx == other.block_or_chunk_idx ? tuple_idx <= other.tuple_idx
		//                                                       : block_or_chunk_idx <= other.block_or_chunk_idx;
	}
	bool operator>=(const filtered_block_iterator_t &other) const {
		return current_pos >= other.current_pos;
		// return block_or_chunk_idx == other.block_or_chunk_idx ? tuple_idx >= other.tuple_idx
		//                                                       : block_or_chunk_idx >= other.block_or_chunk_idx;
	}

private:
	STATE *state;
	idx_t current_pos;
	idx_t block_or_chunk_idx;
	idx_t tuple_idx;
};


template <class SORT_KEY, class STATE>
struct block_iterator_traits {};

template <class SORT_KEY>
struct block_iterator_traits<SORT_KEY, FilteredBlockIteratorState> {
	typedef filtered_block_iterator_t<SORT_KEY> iterator_type;
};

template <class SORT_KEY>
struct block_iterator_traits<SORT_KEY, ExternalBlockIteratorState> {
	typedef block_iterator_t<ExternalBlockIteratorState, SORT_KEY> iterator_type;
};

template <class SORT_KEY>
struct block_iterator_traits<SORT_KEY, InMemoryBlockIteratorState> {
	typedef block_iterator_t<InMemoryBlockIteratorState, SORT_KEY> iterator_type;
};

} // namespace duckdb
