#include "platform/macos/mac_platform.h"

#include "platform/firefly_host.h"
#include "platform/macos/mac_ime.h"
#include "platform/macos/mac_settings.h"

#include <AppKit/AppKit.h>
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

}  // namespace
}  // namespace imeaura

@interface IMEAuraStatusTarget : NSObject
- (void)openSettings:(id)sender;
- (void)quitApp:(id)sender;
@end
@implementation IMEAuraStatusTarget
- (void)openSettings:(id)sender {
  (void)sender;
  imeaura::mac_settings::show();
}
- (void)quitApp:(id)sender {
  (void)sender;
  [NSApp terminate:nil];
}
@end

namespace imeaura {
namespace {

MacEdges g_edges;
Settings g_settings;
NSStatusItem* g_status = nil;
IMEAuraStatusTarget* g_status_target = nil;
MacPlatformBackend* g_self = nullptr;
FireflyHost g_firefly;

Rect primary_monitor_rect() {
  NSScreen* screen = [NSScreen mainScreen];
  if (!screen) return {};
  NSRect f = [screen frame];
  return Rect{static_cast<int>(f.origin.x), static_cast<int>(f.origin.y), static_cast<int>(f.size.width),
              static_cast<int>(f.size.height)};
}

void refresh_policy() {
  if (!g_self) return;
  PolicyInput in{};
  in.ime_lang = mac_active_input_language();
  in.reduce_motion = g_self->prefers_reduced_motion();
  const auto policy = evaluate_policy(g_settings, in);
  g_self->apply_policy(g_settings, policy);
}

}  // namespace

bool MacPlatformBackend::init() {
  g_self = this;
  load_settings(g_settings);
  g_firefly.set_on_toggle([] {
    refresh_policy();
    mac_settings::set_firefly_active(g_firefly.is_active());
  });
  if (g_settings.firefly_enabled) {
    if (!g_firefly.apply(g_settings, g_settings)) {
      save_settings(g_settings);
    }
  }
  mac_settings::create(g_settings, [this](const Settings& s) {
    const bool was = g_settings.firefly_enabled;
    g_settings = s;
    save_settings(g_settings);
    if (was != g_settings.firefly_enabled) {
      g_firefly.apply(g_settings, g_settings);
    }
    mac_settings::set_firefly_active(g_firefly.is_active());
    refresh_policy();
  });
  g_status_target = [[IMEAuraStatusTarget alloc] init];
  g_status = [[NSStatusBar systemStatusBar] statusItemWithLength:NSVariableStatusItemLength];
  g_status.button.title = @"IME";
  NSMenu* menu = [[NSMenu alloc] init];
  [menu addItemWithTitle:@"Open Settings" action:@selector(openSettings:) keyEquivalent:@""];
  menu.itemArray.firstObject.target = g_status_target;
  [menu addItemWithTitle:@"Quit" action:@selector(quitApp:) keyEquivalent:@"q"];
  menu.itemArray.lastObject.target = g_status_target;
  g_status.menu = menu;
  return true;
}

void MacPlatformBackend::shutdown() {
  g_firefly.shutdown();
  mac_settings::destroy();
  if (g_status) {
    [[NSStatusBar systemStatusBar] removeStatusItem:g_status];
    g_status = nil;
  }
  g_status_target = nil;
  g_self = nullptr;
}

int MacPlatformBackend::run() {
  [NSApplication sharedApplication];
  refresh_policy();
  [NSTimer scheduledTimerWithTimeInterval:0.1 repeats:YES block:^(NSTimer*) { refresh_policy(); }];
  [NSApp run];
  return 0;
}

bool MacPlatformBackend::prefers_reduced_motion() {
  return [[NSWorkspace sharedWorkspace] accessibilityDisplayShouldReduceMotion];
}

std::string MacPlatformBackend::active_input_language() { return mac_active_input_language(); }
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

void MacPlatformBackend::show_settings_window() { mac_settings::show(); }
void MacPlatformBackend::hide_settings_window() { mac_settings::hide(); }
bool MacPlatformBackend::settings_visible() const { return mac_settings::visible(); }

ProbeState MacPlatformBackend::probe_state(const Settings& settings) {
  PolicyInput in{};
  in.ime_lang = active_input_language();
  in.reduce_motion = prefers_reduced_motion();
  const auto p = evaluate_policy(settings, in);
  ProbeState st{};
  st.ime_lang = in.ime_lang;
  st.ime_japanese = (in.ime_lang == "ja");
  st.visible = p.visible;
  st.monitor_rect = get_active_monitor_rect();
  return st;
}

}  // namespace imeaura
