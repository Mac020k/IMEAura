#pragma once

#include "core/policy.h"
#include "core/settings.h"

#include <atomic>
#include <functional>
#include <string>

namespace imeaura {

struct AppOptions {
  bool probe_mode = false;
  bool probe_json = false;
};

class App {
 public:
  explicit App(AppOptions options);
  ~App();

  int run();

 private:
  void refresh();
  void on_settings_changed(const Settings& settings);
  void emit_probe_if_needed();

  AppOptions options_;
  Settings settings_;
  std::atomic<bool> running_{true};
  PolicyInput last_input_{};
  PolicyOutput last_policy_{};
};

}  // namespace imeaura
