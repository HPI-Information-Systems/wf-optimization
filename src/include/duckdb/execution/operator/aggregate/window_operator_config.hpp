#pragma once

#include <string>
#include <sstream>

namespace duckdb {

class WindowOperatorConfig {
 public:
  enum class OptimizationLevel : uint8_t { None, Filter, EarlyOut, ShrinkRuns, ShrinkPartitionsSimulated, ShrinkPartitions, ShrinkPartitionsAdaptive, ShrinkPartitionsAdaptiveContext, Combined };

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
  double shrink_partitions_threshold{1.0};

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

 static std::string optimization_level_to_str(WindowOperatorConfig::OptimizationLevel level) {
  auto stream = std::stringstream{};
  stream << level;
  return stream.str();
}

 protected:
  WindowOperatorConfig() = default;
  WindowOperatorConfig(WindowOperatorConfig&&) noexcept = default;
  WindowOperatorConfig& operator=(WindowOperatorConfig&&) noexcept = default;
  ~WindowOperatorConfig() = default;
};

}  // namespace duckdb
