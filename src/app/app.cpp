#include "app/app.h"

#include "platform/backend.h"

#include <iostream>

namespace imeaura {

App::App(AppOptions options) : options_(options) { load_settings(settings_); }

App::~App() = default;

int App::run() {
  auto backend = create_platform_backend();
  if (!backend) {
    std::cerr << "Failed to create platform backend\n";
    return 1;
  }
  if (options_.probe_mode) {
    if (!backend->init_probe()) {
      std::cerr << "Probe init failed\n";
      return 1;
    }
    const auto state = backend->probe_state(settings_);
    if (options_.probe_json) {
      std::cout << "{"
                << "\"ime_lang\":\"" << state.ime_lang << "\","
                << "\"ime_japanese\":" << (state.ime_japanese ? "true" : "false") << ","
                << "\"focused\":" << (state.text_focused ? "true" : "false") << ","
                << "\"hovered\":" << (state.text_hovered ? "true" : "false") << ","
                << "\"visible\":" << (state.visible ? "true" : "false") << ","
                << "\"monitor_rect\":{"
                << "\"x\":" << state.monitor_rect.x << ","
                << "\"y\":" << state.monitor_rect.y << ","
                << "\"width\":" << state.monitor_rect.width << ","
                << "\"height\":" << state.monitor_rect.height << "}"
                << "}\n" << std::flush;
    }
    return 0;
  }

  if (!backend->init()) {
    std::cerr << "Platform init failed\n";
    return 1;
  }

  backend->set_settings_changed_callback([this](const Settings& s) {
    settings_ = s;
    save_settings(settings_);
  });

  refresh();
  backend->show_settings_window();
  const int code = backend->run();
  backend->shutdown();
  return code;
}

void App::refresh() {
  // Platform run loop calls refresh via events; stub for probe path.
}

void App::on_settings_changed(const Settings& settings) {
  settings_ = settings;
  save_settings(settings_);
}

void App::emit_probe_if_needed() {
  // handled in run() for one-shot probe
}

}  // namespace imeaura
