#pragma once

#include <string>
#include <sstream>
#include <stdexcept>
#include <atomic>
#include <chrono>

namespace duckdb {

using SteadyClock = std::chrono::steady_clock;
using TimePoint = std::chrono::time_point<SteadyClock>;

class WindowOperatorConfig {
 public:
  enum class OptimizationLevel : uint8_t {
    None,
    Filter,
    EarlyOut,
    ShrinkRuns,
    ShrinkPartitionsSimulated,
    ShrinkPartitions,
    ShrinkPartitionsAdaptive,
    ShrinkPartitionsAdaptiveContext,
    Combined
  };

  inline static WindowOperatorConfig& get() {
    static auto instance = WindowOperatorConfig{};
    return instance;
  }

  WindowOperatorConfig(const WindowOperatorConfig&) = delete;
  const WindowOperatorConfig& operator=(const WindowOperatorConfig&) = delete;

  bool do_filter{false};
  bool do_early_out{false};
  bool skip_sort{false};
  bool shrink_runs{false};
  bool shrink_partitions{false};
  bool simulate_shrink_partitions{false};
  bool shrink_partitions_adaptive{false};
  bool use_adaptivity_context{false};
  int64_t predicate_value{0};
  uint64_t expected_partitions{0};
  double shrink_partitions_threshold{0.2};
  std::atomic_uint32_t idx{0};

  friend std::ostream& operator<<(std::ostream& stream, WindowOperatorConfig::OptimizationLevel level) {
  switch (level) {
    case WindowOperatorConfig::OptimizationLevel::None:
      stream << "Baseline";
      break;
    case WindowOperatorConfig::OptimizationLevel::Filter:
      stream << "Filter";
      break;
    case WindowOperatorConfig::OptimizationLevel::EarlyOut:
      stream << "EarlyOut";
      break;
    case WindowOperatorConfig::OptimizationLevel::ShrinkRuns:
      stream << "ShrinkRuns";
      break;
    case WindowOperatorConfig::OptimizationLevel::ShrinkPartitionsSimulated:
      stream << "ShrinkPartitionsSimulated";
      break;
    case WindowOperatorConfig::OptimizationLevel::ShrinkPartitions:
      stream << "ShrinkPartitions";
      break;
    case WindowOperatorConfig::OptimizationLevel::ShrinkPartitionsAdaptive:
      stream << "ShrinkPartitionsAdaptive";
      break;
    case WindowOperatorConfig::OptimizationLevel::ShrinkPartitionsAdaptiveContext:
      stream << "ShrinkPartitionsAdaptiveContext";
      break;
    case WindowOperatorConfig::OptimizationLevel::Combined:
      stream << "Combined";
      break;
  }
  return stream;
}

 protected:
  WindowOperatorConfig() = default;
  WindowOperatorConfig(WindowOperatorConfig&& rhs) noexcept {
    do_filter = rhs.do_filter;
    do_early_out = rhs.do_early_out;
    skip_sort = rhs.skip_sort;
    shrink_runs = rhs.shrink_runs;
    shrink_partitions = rhs.shrink_partitions;
    simulate_shrink_partitions = rhs.simulate_shrink_partitions;
    shrink_partitions_adaptive = rhs.shrink_partitions_adaptive;
    use_adaptivity_context = rhs.use_adaptivity_context;
    predicate_value = rhs.predicate_value;
    expected_partitions = rhs.expected_partitions;
    shrink_partitions_threshold = rhs.shrink_partitions_threshold;
    idx = rhs.idx.load();
  };
  WindowOperatorConfig& operator=(WindowOperatorConfig&& rhs) noexcept {
    do_filter = rhs.do_filter;
    do_early_out = rhs.do_early_out;
    skip_sort = rhs.skip_sort;
    shrink_runs = rhs.shrink_runs;
    shrink_partitions = rhs.shrink_partitions;
    simulate_shrink_partitions = rhs.simulate_shrink_partitions;
    shrink_partitions_adaptive = rhs.shrink_partitions_adaptive;
    use_adaptivity_context = rhs.use_adaptivity_context;
    predicate_value = rhs.predicate_value;
    expected_partitions = rhs.expected_partitions;
    shrink_partitions_threshold = rhs.shrink_partitions_threshold;
    idx = rhs.idx.load();
    return *this;
  };
  ~WindowOperatorConfig() = default;
};

inline std::string optimization_level_to_str(WindowOperatorConfig::OptimizationLevel level) {
  auto stream = std::stringstream{};
  stream << level;
  return stream.str();
}

inline WindowOperatorConfig::OptimizationLevel str_to_optimization_level(const std::string& level_str) {
  for (auto i = uint8_t{0}; i <= static_cast<uint8_t>(WindowOperatorConfig::OptimizationLevel::Combined); ++i) {
    const auto level = static_cast<WindowOperatorConfig::OptimizationLevel>(i);
    if (optimization_level_to_str(level) == level_str) {
      return level;
    }
  }

  throw std::runtime_error("Unknown optimization level: '" + level_str + "'.");
}

template <typename Functor>
void resolve_bool(bool val, const Functor& fn) {
  if (val) {
    fn(std::true_type{});
  } else {
    fn(std::false_type{});
  }
}

}  // namespace duckdb
