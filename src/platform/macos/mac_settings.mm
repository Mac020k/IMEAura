#include "platform/macos/mac_settings.h"

#include "core/i18n.h"
#include "core/input_languages.h"
#include "core/tokens.h"

#import <AppKit/AppKit.h>

#include <cmath>
#include <cstring>
#include <cwchar>
#include <filesystem>
#include <functional>
#include <string>
#include <vector>

namespace imeaura {
namespace mac_settings_state {

Settings settings = default_settings();
std::function<void(const Settings&)> callback;
bool firefly_active = false;

NSString* WtoNS(const wchar_t* s) {
  if (!s) return @"";
  return [[NSString alloc] initWithBytes:s length:wcslen(s) * sizeof(wchar_t)
                                encoding:NSUTF32LittleEndianStringEncoding];
}

NSColor* RgbaToNS(const Rgba& c) {
  return [NSColor colorWithCalibratedRed:c.r / 255.0 green:c.g / 255.0 blue:c.b / 255.0 alpha:c.a / 255.0];
}

Rgba NSToRgba(NSColor* color) {
  NSColor* rgb = [color colorUsingColorSpace:NSColorSpace.sRGBColorSpace];
  if (!rgb) rgb = color;
  Rgba out{};
  out.r = static_cast<uint8_t>(std::lround(rgb.redComponent * 255.0));
  out.g = static_cast<uint8_t>(std::lround(rgb.greenComponent * 255.0));
  out.b = static_cast<uint8_t>(std::lround(rgb.blueComponent * 255.0));
  out.a = static_cast<uint8_t>(std::lround(rgb.alphaComponent * 255.0));
  return out;
}

void Emit() {
  settings = normalize_settings(settings);
  if (callback) callback(settings);
}

NSString* FindAssetPath(const char* relative) {
  namespace fs = std::filesystem;
  @autoreleasepool {
    NSString* exe = [[NSProcessInfo processInfo].arguments.firstObject stringByStandardizingPath];
    if (!exe) return nil;
    fs::path dir = fs::path([exe fileSystemRepresentation]).parent_path();
    for (int i = 0; i < 6; ++i) {
      const auto candidate = dir / relative;
      if (fs::exists(candidate)) {
        return [NSString stringWithUTF8String:candidate.string().c_str()];
      }
      if (!dir.has_parent_path()) break;
      dir = dir.parent_path();
    }
  }
  return nil;
}

NSImage* LoadUiIcon(const char* relative, NSString* accessibilityLabel) {
  NSString* path = FindAssetPath(relative);
  NSImage* image = nil;
  if (path) image = [[NSImage alloc] initWithContentsOfFile:path];
  if (!image) {
    // Fallback: SF Symbol when SVG asset is missing from the run tree.
    if (@available(macOS 11.0, *)) {
      const char* symbol = "plus";
      if (strstr(relative, "trash")) symbol = "trash";
      else if (strstr(relative, "back")) symbol = "chevron.backward";
      image = [NSImage imageWithSystemSymbolName:[NSString stringWithUTF8String:symbol]
                        accessibilityDescription:accessibilityLabel];
    }
  }
  (void)accessibilityLabel;
  return image;
}

}  // namespace mac_settings_state
}  // namespace imeaura

@interface IMEAuraSettingsController : NSObject
@property(nonatomic, strong) NSWindow* window;
@property(nonatomic, strong) NSTabView* tabs;
@property(nonatomic, strong) NSStackView* auraStack;
@property(nonatomic, strong) NSPopUpButton* langButton;
@property(nonatomic, strong) NSButton* fireflyToggle;
@property(nonatomic, strong) NSTextField* fireflyStatus;
- (void)rebuildAuraRows;
- (void)showWindow;
- (void)hideWindow;
- (void)syncSettings;
@end

@implementation IMEAuraSettingsController

- (instancetype)init {
  self = [super init];
  if (!self) return self;
  using namespace imeaura::mac_settings_state;
  NSRect frame = NSMakeRect(0, 0, 420, 520);
  self.window = [[NSWindow alloc] initWithContentRect:frame
                                            styleMask:(NSWindowStyleMaskTitled | NSWindowStyleMaskClosable |
                                                       NSWindowStyleMaskMiniaturizable | NSWindowStyleMaskResizable)
                                              backing:NSBackingStoreBuffered
                                                defer:NO];
  self.window.title = @"IME Aura";
  self.window.releasedWhenClosed = NO;
  self.tabs = [[NSTabView alloc] initWithFrame:NSMakeRect(12, 12, 396, 496)];
  [self.window.contentView addSubview:self.tabs];

  NSTabViewItem* aura = [[NSTabViewItem alloc] initWithIdentifier:@"aura"];
  aura.label = @"Aura";
  NSView* auraView = [[NSView alloc] initWithFrame:NSMakeRect(0, 0, 380, 450)];
  self.auraStack = [NSStackView stackViewWithViews:@[]];
  self.auraStack.orientation = NSUserInterfaceLayoutOrientationVertical;
  self.auraStack.alignment = NSLayoutAttributeLeading;
  self.auraStack.translatesAutoresizingMaskIntoConstraints = NO;
  [auraView addSubview:self.auraStack];
  [NSLayoutConstraint activateConstraints:@[
    [self.auraStack.topAnchor constraintEqualToAnchor:auraView.topAnchor constant:8],
    [self.auraStack.leadingAnchor constraintEqualToAnchor:auraView.leadingAnchor constant:8],
    [self.auraStack.trailingAnchor constraintEqualToAnchor:auraView.trailingAnchor constant:-8],
  ]];
  aura.view = auraView;
  [self.tabs addTabViewItem:aura];

  NSTabViewItem* firefly = [[NSTabViewItem alloc] initWithIdentifier:@"firefly"];
  firefly.label = @"Firefly";
  NSView* ffView = [[NSView alloc] initWithFrame:NSMakeRect(0, 0, 380, 200)];
  self.fireflyToggle = [NSButton checkboxWithTitle:@"Enable Firefly" target:self action:@selector(onFireflyToggle:)];
  self.fireflyToggle.frame = NSMakeRect(12, 150, 300, 24);
  self.fireflyStatus = [NSTextField labelWithString:@""];
  self.fireflyStatus.frame = NSMakeRect(12, 120, 300, 24);
  [ffView addSubview:self.fireflyToggle];
  [ffView addSubview:self.fireflyStatus];
  firefly.view = ffView;
  [self.tabs addTabViewItem:firefly];

  NSTabViewItem* general = [[NSTabViewItem alloc] initWithIdentifier:@"general"];
  general.label = @"General";
  NSView* genView = [[NSView alloc] initWithFrame:NSMakeRect(0, 0, 380, 200)];
  NSTextField* langLabel = [NSTextField labelWithString:@"Language"];
  langLabel.frame = NSMakeRect(12, 150, 200, 20);
  self.langButton = [[NSPopUpButton alloc] initWithFrame:NSMakeRect(12, 120, 240, 28) pullsDown:NO];
  size_t n = 0;
  const auto* cat = imeaura::ui_language_catalog(n);
  for (size_t i = 0; i < n; ++i) {
    [self.langButton addItemWithTitle:WtoNS(cat[i].native_name)];
    self.langButton.lastItem.representedObject = [NSString stringWithUTF8String:cat[i].id];
  }
  self.langButton.target = self;
  self.langButton.action = @selector(onLangChanged:);
  [genView addSubview:langLabel];
  [genView addSubview:self.langButton];
  general.view = genView;
  [self.tabs addTabViewItem:general];

  [self rebuildAuraRows];
  [self syncSettings];
  return self;
}

- (void)rebuildAuraRows {
  using namespace imeaura;
  using namespace imeaura::mac_settings_state;
  for (NSView* v in [self.auraStack.views copy]) [self.auraStack removeView:v];
  for (size_t i = 0; i < settings.aura_slots.size(); ++i) {
    NSStackView* row = [NSStackView stackViewWithViews:@[]];
    row.orientation = NSUserInterfaceLayoutOrientationHorizontal;
    NSPopUpButton* pop = [[NSPopUpButton alloc] initWithFrame:NSMakeRect(0, 0, 160, 28) pullsDown:NO];
    std::vector<std::string> used;
    for (size_t j = 0; j < settings.aura_slots.size(); ++j) {
      if (j != i) used.push_back(settings.aura_slots[j].lang_id);
    }
    auto choices = aura_slot_language_choices(used, settings.aura_slots[i].lang_id);
    NSInteger selectIndex = 0;
    for (size_t ci = 0; ci < choices.size(); ++ci) {
      const auto& id = choices[ci];
      [pop addItemWithTitle:WtoNS(input_language_display_name(id, true))];
      pop.lastItem.representedObject = [NSString stringWithUTF8String:id.c_str()];
      if (id == settings.aura_slots[i].lang_id) selectIndex = static_cast<NSInteger>(ci);
    }
    [pop selectItemAtIndex:selectIndex];
    pop.tag = static_cast<NSInteger>(i);
    pop.target = self;
    pop.action = @selector(onSlotLang:);
    NSColorWell* well = [[NSColorWell alloc] initWithFrame:NSMakeRect(0, 0, 44, 28)];
    well.color = RgbaToNS(settings.aura_slots[i].color);
    well.tag = static_cast<NSInteger>(i);
    well.target = self;
    well.action = @selector(onSlotColor:);
    NSString* removeLabel = WtoNS(tr(lang_from_key(settings.language), StringId::kRemoveColorSlot));
    NSButton* remove = [NSButton buttonWithTitle:@"" target:self action:@selector(onRemoveSlot:)];
    remove.image = LoadUiIcon("img/icon_trash.svg", removeLabel);
    remove.imagePosition = NSImageOnly;
    remove.bezelStyle = NSBezelStyleRounded;
    remove.bordered = YES;
    remove.toolTip = removeLabel;
    remove.tag = static_cast<NSInteger>(i);
    remove.enabled = settings.aura_slots.size() > static_cast<size_t>(kMinAuraSlots);
    [remove setAccessibilityLabel:removeLabel];
    [row addArrangedSubview:pop];
    NSView* spacer = [[NSView alloc] initWithFrame:NSMakeRect(0, 0, 8, 28)];
    [spacer setContentHuggingPriority:NSLayoutPriorityDefaultLow
                       forOrientation:NSLayoutConstraintOrientationHorizontal];
    [spacer setContentCompressionResistancePriority:NSLayoutPriorityDefaultLow
                                     forOrientation:NSLayoutConstraintOrientationHorizontal];
    [row addArrangedSubview:spacer];
    [row addArrangedSubview:well];
    [row addArrangedSubview:remove];
    row.distribution = NSStackViewDistributionFill;
    [pop setContentHuggingPriority:NSLayoutPriorityDefaultHigh
                    forOrientation:NSLayoutConstraintOrientationHorizontal];
    [pop setContentCompressionResistancePriority:NSLayoutPriorityRequired
                                  forOrientation:NSLayoutConstraintOrientationHorizontal];
    [self.auraStack addArrangedSubview:row];
  }
  if (static_cast<int>(settings.aura_slots.size()) < kMaxAuraSlots) {
    NSString* addLabel = WtoNS(tr(lang_from_key(settings.language), StringId::kAddColorSlot));
    NSButton* add = [NSButton buttonWithTitle:addLabel target:self action:@selector(onAddSlot:)];
    add.image = LoadUiIcon("img/icon_add.svg", addLabel);
    add.imagePosition = NSImageLeft;
    [self.auraStack addArrangedSubview:add];
  }
}

- (void)onSlotLang:(NSPopUpButton*)sender {
  using namespace imeaura::mac_settings_state;
  const size_t i = static_cast<size_t>(sender.tag);
  if (i >= settings.aura_slots.size()) return;
  NSString* lid = sender.selectedItem.representedObject;
  if (!lid) return;
  settings.aura_slots[i].lang_id = lid.UTF8String;
  Emit();
  [self rebuildAuraRows];
}

- (void)onSlotColor:(NSColorWell*)sender {
  using namespace imeaura::mac_settings_state;
  const size_t i = static_cast<size_t>(sender.tag);
  if (i >= settings.aura_slots.size()) return;
  settings.aura_slots[i].color = NSToRgba(sender.color);
  Emit();
}

- (void)onRemoveSlot:(NSButton*)sender {
  using namespace imeaura;
  using namespace imeaura::mac_settings_state;
  const size_t i = static_cast<size_t>(sender.tag);
  if (i >= settings.aura_slots.size()) return;
  if (settings.aura_slots.size() <= static_cast<size_t>(kMinAuraSlots)) return;
  settings.aura_slots.erase(settings.aura_slots.begin() + static_cast<std::ptrdiff_t>(i));
  Emit();
  [self rebuildAuraRows];
}

- (void)onAddSlot:(id)sender {
  (void)sender;
  using namespace imeaura;
  using namespace imeaura::mac_settings_state;
  std::vector<std::string> used;
  for (const auto& s : settings.aura_slots) used.push_back(s.lang_id);
  auto unused = unused_input_languages(used);
  if (unused.empty()) return;
  AuraColorSlot slot;
  slot.lang_id = unused.front();
  slot.color = settings.default_color_for_new_slot(settings.aura_slots.size());
  settings.aura_slots.push_back(slot);
  Emit();
  [self rebuildAuraRows];
}

- (void)onLangChanged:(NSPopUpButton*)sender {
  using namespace imeaura::mac_settings_state;
  NSString* lid = sender.selectedItem.representedObject;
  if (!lid) return;
  settings.language = lid.UTF8String;
  Emit();
}

- (void)onFireflyToggle:(NSButton*)sender {
  using namespace imeaura::mac_settings_state;
  settings.firefly_enabled = sender.state == NSControlStateValueOn;
  Emit();
}

- (void)showWindow {
  [self.window center];
  [self.window makeKeyAndOrderFront:nil];
}

- (void)hideWindow {
  [self.window orderOut:nil];
}

- (void)syncSettings {
  using namespace imeaura::mac_settings_state;
  self.fireflyToggle.state = settings.firefly_enabled ? NSControlStateValueOn : NSControlStateValueOff;
  const Lang ui_lang = lang_from_key(settings.language);
  self.fireflyStatus.stringValue =
      firefly_active ? WtoNS(tr(ui_lang, StringId::kFireflyStateBusy))
                     : WtoNS(tr(ui_lang, StringId::kFireflyStateAvailable));
  for (NSInteger i = 0; i < self.langButton.numberOfItems; ++i) {
    NSMenuItem* item = [self.langButton itemAtIndex:i];
    NSString* lid = item.representedObject;
    if (lid && settings.language == lid.UTF8String) {
      [self.langButton selectItemAtIndex:i];
      break;
    }
  }
  [self rebuildAuraRows];
}

@end

namespace imeaura {
namespace {
IMEAuraSettingsController* g_controller = nil;
}

namespace mac_settings {

bool create(Settings initial, std::function<void(const Settings&)> cb) {
  mac_settings_state::settings = normalize_settings(initial);
  mac_settings_state::callback = std::move(cb);
  if (!g_controller) g_controller = [[IMEAuraSettingsController alloc] init];
  [g_controller syncSettings];
  return true;
}

void destroy() {
  [g_controller hideWindow];
  g_controller = nil;
  mac_settings_state::callback = nullptr;
}

void show() {
  if (!g_controller) return;
  [g_controller showWindow];
}

void hide() {
  if (!g_controller) return;
  [g_controller hideWindow];
}

bool visible() { return g_controller && g_controller.window.isVisible; }

void sync(const Settings& s) {
  mac_settings_state::settings = normalize_settings(s);
  if (g_controller) [g_controller syncSettings];
}

void set_firefly_active(bool active) {
  mac_settings_state::firefly_active = active;
  if (g_controller) [g_controller syncSettings];
}

}  // namespace mac_settings
}  // namespace imeaura
