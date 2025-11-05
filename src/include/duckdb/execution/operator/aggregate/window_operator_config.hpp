#pragma once

namespace duckdb {

class WindowOperatorConfig {
 public:
  inline static WindowOperatorConfig& get() {
    static auto instance = WindowOperatorConfig{};
    return instance;
  }

  WindowOperatorConfig(const WindowOperatorConfig&) = delete;
  const WindowOperatorConfig& operator=(const WindowOperatorConfig&) = delete;

  bool do_filter{false};
  bool do_early_out{false};
  bool skip_sort{false};
  uint64_t predicate_value{0};

 protected:
  WindowOperatorConfig() = default;
  WindowOperatorConfig(WindowOperatorConfig&&) noexcept = default;
  WindowOperatorConfig& operator=(WindowOperatorConfig&&) noexcept = default;
  ~WindowOperatorConfig() = default;
};

}  // namespace duckdb
