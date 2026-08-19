#include "platform/macos/mac_platform.h"

#include <AppKit/AppKit.h>
#include <Carbon/Carbon.h>
#include <CoreGraphics/CoreGraphics.h>
#include <QuartzCore/QuartzCore.h>

#include <string>

namespace imeaura {
namespace {

class MacEdges {
 public:
  void ensure(const Rect& monitor, int thickness, const Rgba& color, float opacity) {
    const CGFloat t = thickness;
    ensure_one(0, NSRect{{static_cast<CGFloat>(monitor.x), static_cast<CGFloat>(monitor.y + monitor.height - thickness)},
                         {static_cast<CGFloat>(monitor.width), t}},
               color, opacity, true);
    ensure_one(1, NSRect{{static_cast<CGFloat>(monitor.x), static_cast<CGFloat>(monitor.y)},
                         {static_cast<CGFloat>(monitor.width), t}},
               color, opacity, false);
    ensure_one(2, NSRect{{static_cast<CGFloat>(monitor.x), static_cast<CGFloat>(monitor.y)},
                         {t, static_cast<CGFloat>(monitor.height)}},
               color, opacity, true);
    ensure_one(3, NSRect{{static_cast<CGFloat>(monitor.x + monitor.width - thickness), static_cast<CGFloat>(monitor.y)},
                         {t, static_cast<CGFloat>(monitor.height)}},
               color, opacity, false);
  }

 private:
  void ensure_one(int idx, NSRect frame, const Rgba& color, float opacity, bool vertical) {
    if (!windows_[idx]) {
      windows_[idx] = [[NSWindow alloc] initWithContentRect:frame
                                                  styleMask:NSWindowStyleMaskBorderless
                                                    backing:NSBackingStoreBuffered
                                                      defer:NO];
      [windows_[idx] setIgnoresMouseEvents:YES];
      [windows_[idx] setLevel:NSStatusWindowLevel];
      [windows_[idx] setCollectionBehavior:NSWindowCollectionBehaviorCanJoinAllSpaces |
                                             NSWindowCollectionBehaviorStationary |
                                             NSWindowCollectionBehaviorIgnoresCycle];
      layers_[idx] = [CAGradientLayer layer];
      [[windows_[idx] contentView] setWantsLayer:YES];
      [[windows_[idx] contentView] setLayer:layers_[idx]];
    }
    [windows_[idx] setFrame:frame display:YES];
    layers_[idx].opacity = opacity;
    if (vertical) {
      layers_[idx].startPoint = CGPointMake(0.5, 1.0);
      layers_[idx].endPoint = CGPointMake(0.5, 0.0);
    } else {
      layers_[idx].startPoint = CGPointMake(1.0, 0.5);
      layers_[idx].endPoint = CGPointMake(0.0, 0.5);
    }
    const CGFloat a = color.a / 255.0 * opacity;
    layers_[idx].colors = @[
      (id)[NSColor colorWithRed:color.r / 255.0 green:color.g / 255.0 blue:color.b / 255.0 alpha:a].CGColor,
      (id)[NSColor colorWithRed:color.r / 255.0 green:color.g / 255.0 blue:color.b / 255.0 alpha:a * 0.35].CGColor,
      (id)[NSColor colorWithRed:color.r / 255.0 green:color.g / 255.0 blue:color.b / 255.0 alpha:0].CGColor
    ];
    layers_[idx].locations = @[ @0.0, @0.55, @1.0 ];
    [windows_[idx] orderFrontRegardless];
  }

  NSWindow* windows_[4]{nil, nil, nil, nil};
  CAGradientLayer* layers_[4]{nil, nil, nil, nil};
};

MacEdges g_edges;
Settings g_settings;
NSStatusItem* g_status = nil;

bool tis_japanese() {
  TISInputSourceRef src = TISCopyCurrentKeyboardInputSource();
  if (!src) return false;
  CFStringRef langs = static_cast<CFStringRef>(TISGetInputSourceProperty(src, kTISPropertyInputSourceLanguages));
  bool jp = false;
  if (langs && CFGetTypeID(langs) == CFStringGetTypeID()) {
    char buf[256];
    if (CFStringGetCString(langs, buf, sizeof(buf), kCFStringEncodingUTF8)) {
      std::string s(buf);
      jp = s.find("ja") != std::string::npos || s.find("Japanese") != std::string::npos;
    }
  }
  CFRelease(src);
  return jp;
}

Rect primary_monitor_rect() {
  NSScreen* screen = [NSScreen mainScreen];
  if (!screen) return {};
  NSRect f = [screen frame];
  return Rect{static_cast<int>(f.origin.x), static_cast<int>(f.origin.y), static_cast<int>(f.size.width),
              static_cast<int>(f.size.height)};
}

}  // namespace

bool MacPlatformBackend::init() {
  load_settings(g_settings);
  g_status = [[NSStatusBar systemStatusBar] statusItemWithLength:NSVariableStatusItemLength];
  g_status.button.title = @"IME";
  return true;
}

void MacPlatformBackend::shutdown() {
  if (g_status) {
    [[NSStatusBar systemStatusBar] removeStatusItem:g_status];
    g_status = nil;
  }
}

int MacPlatformBackend::run() {
  [NSApplication sharedApplication];
  PolicyInput in{};
  in.ime_japanese = is_japanese_input();
  in.reduce_motion = prefers_reduced_motion();
  const auto policy = evaluate_policy(g_settings, in);
  apply_policy(g_settings, policy);
  [NSApp run];
  return 0;
}

bool MacPlatformBackend::prefers_reduced_motion() {
  return [[NSWorkspace sharedWorkspace] accessibilityDisplayShouldReduceMotion];
}

bool MacPlatformBackend::is_japanese_input() { return tis_japanese(); }
bool MacPlatformBackend::is_text_input_focused() { return false; }
bool MacPlatformBackend::is_text_input_hovered() { return false; }
Rect MacPlatformBackend::get_active_monitor_rect() { return primary_monitor_rect(); }
Rect MacPlatformBackend::get_cursor_monitor_rect() { return primary_monitor_rect(); }

void MacPlatformBackend::apply_policy(const Settings& settings, const PolicyOutput& policy) {
  g_settings = settings;
  const Rect mon = get_active_monitor_rect();
  const float opacity = policy.visible ? 1.f : 0.f;
  g_edges.ensure(mon, settings.gradient_width, policy.target_color, opacity);
}

void MacPlatformBackend::show_settings_window() {}
void MacPlatformBackend::hide_settings_window() {}
bool MacPlatformBackend::settings_visible() const { return false; }

ProbeState MacPlatformBackend::probe_state(const Settings& settings) {
  PolicyInput in{};
  in.ime_japanese = is_japanese_input();
  in.reduce_motion = prefers_reduced_motion();
  const auto p = evaluate_policy(settings, in);
  ProbeState st{};
  st.ime_japanese = in.ime_japanese;
  st.visible = p.visible;
  st.monitor_rect = get_active_monitor_rect();
  return st;
}

}  // namespace imeaura
