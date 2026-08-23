#include "platform/linux/linux_settings.h"

#include "core/i18n.h"
#include "core/input_languages.h"
#include "core/tokens.h"

#include <cstdio>
#include <gtk/gtk.h>

#include <functional>
#include <string>
#include <vector>

namespace imeaura {
namespace {

Settings g_settings = default_settings();
std::function<void(const Settings&)> g_cb;
bool g_firefly_active = false;
GtkWidget* g_window = nullptr;
GtkWidget* g_aura_box = nullptr;
GtkWidget* g_lang_drop = nullptr;
GtkWidget* g_firefly_switch = nullptr;
GtkWidget* g_firefly_status = nullptr;
bool g_visible = false;

void Emit() {
  g_settings = normalize_settings(g_settings);
  if (g_cb) g_cb(g_settings);
}

std::string WideToUtf8(const wchar_t* s) {
  if (!s) return {};
  std::string out;
  for (const wchar_t* p = s; *p; ++p) {
    const uint32_t cp = static_cast<uint32_t>(*p);
    if (cp < 0x80) {
      out.push_back(static_cast<char>(cp));
    } else if (cp < 0x800) {
      out.push_back(static_cast<char>(0xC0 | (cp >> 6)));
      out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    } else {
      out.push_back(static_cast<char>(0xE0 | (cp >> 12)));
      out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
      out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    }
  }
  return out;
}

void RebuildAura(GtkWidget* box);

void OnLangChanged(GtkDropDown* drop, GParamSpec*, gpointer) {
  const guint idx = gtk_drop_down_get_selected(drop);
  size_t n = 0;
  const auto* cat = input_language_catalog(n);
  if (idx < n) {
    g_settings.language = cat[idx].id;
    Emit();
  }
}

void OnFireflyToggled(GtkSwitch* sw, GParamSpec*, gpointer) {
  g_settings.firefly_enabled = gtk_switch_get_active(sw);
  Emit();
}

void OnAddSlot(GtkButton*, gpointer box) {
  std::vector<std::string> used;
  for (const auto& s : g_settings.aura_slots) used.push_back(s.lang_id);
  auto unused = unused_input_languages(used);
  if (unused.empty()) return;
  AuraColorSlot slot;
  slot.lang_id = unused.front();
  slot.color = g_settings.default_color_for_new_slot(g_settings.aura_slots.size());
  g_settings.aura_slots.push_back(slot);
  Emit();
  RebuildAura(GTK_WIDGET(box));
}

struct SlotCtx {
  size_t index = 0;
  GtkWidget* box = nullptr;
};

void OnRemoveSlot(GtkButton*, gpointer data) {
  auto* ctx = static_cast<SlotCtx*>(data);
  if (g_settings.aura_slots.size() <= static_cast<size_t>(kMinAuraSlots)) return;
  if (ctx->index >= g_settings.aura_slots.size()) return;
  g_settings.aura_slots.erase(g_settings.aura_slots.begin() + static_cast<std::ptrdiff_t>(ctx->index));
  Emit();
  RebuildAura(ctx->box);
  delete ctx;
}

void OnSlotLang(GtkDropDown* drop, GParamSpec*, gpointer data) {
  auto* ctx = static_cast<SlotCtx*>(data);
  if (ctx->index >= g_settings.aura_slots.size()) return;
  std::vector<std::string> used;
  for (size_t j = 0; j < g_settings.aura_slots.size(); ++j) {
    if (j != ctx->index) used.push_back(g_settings.aura_slots[j].lang_id);
  }
  auto choices = unused_input_languages(used);
  choices.insert(choices.begin(), g_settings.aura_slots[ctx->index].lang_id);
  const guint idx = gtk_drop_down_get_selected(drop);
  if (idx < choices.size()) {
    g_settings.aura_slots[ctx->index].lang_id = choices[idx];
    Emit();
    RebuildAura(ctx->box);
  }
}

void RebuildAura(GtkWidget* box) {
  GtkWidget* child = gtk_widget_get_first_child(box);
  while (child) {
    GtkWidget* next = gtk_widget_get_next_sibling(child);
    gtk_box_remove(GTK_BOX(box), child);
    child = next;
  }

  for (size_t i = 0; i < g_settings.aura_slots.size(); ++i) {
    GtkWidget* row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    std::vector<std::string> used;
    for (size_t j = 0; j < g_settings.aura_slots.size(); ++j) {
      if (j != i) used.push_back(g_settings.aura_slots[j].lang_id);
    }
    auto choices = unused_input_languages(used);
    choices.insert(choices.begin(), g_settings.aura_slots[i].lang_id);
    std::vector<std::string> owned;
    owned.reserve(choices.size());
    for (const auto& id : choices) {
      owned.push_back(WideToUtf8(input_language_display_name(id, true)));
    }
    std::vector<const char*> labels;
    labels.reserve(owned.size() + 1);
    for (const auto& s : owned) labels.push_back(s.c_str());
    labels.push_back(nullptr);
    GtkWidget* drop = gtk_drop_down_new_from_strings(labels.data());
    auto* ctx = new SlotCtx{i, box};
    g_signal_connect(drop, "notify::selected", G_CALLBACK(OnSlotLang), ctx);
    gtk_box_append(GTK_BOX(row), drop);

    char color_css[64];
    const auto& c = g_settings.aura_slots[i].color;
    std::snprintf(color_css, sizeof(color_css), "background:#%02X%02X%02X;", c.r, c.g, c.b);
    GtkWidget* swatch = gtk_button_new_with_label(" ");
    gtk_widget_set_size_request(swatch, 48, 28);
    gtk_box_append(GTK_BOX(row), swatch);

    if (g_settings.aura_slots.size() > static_cast<size_t>(kMinAuraSlots)) {
      GtkWidget* rem = gtk_button_new_with_label(
          WideToUtf8(tr(lang_from_key(g_settings.language), StringId::kRemoveColorSlot)).c_str());
      auto* rctx = new SlotCtx{i, box};
      g_signal_connect(rem, "clicked", G_CALLBACK(OnRemoveSlot), rctx);
      gtk_box_append(GTK_BOX(row), rem);
    }
    gtk_box_append(GTK_BOX(box), row);
    (void)color_css;
  }

  if (static_cast<int>(g_settings.aura_slots.size()) < kMaxAuraSlots) {
    GtkWidget* add = gtk_button_new_with_label(
        WideToUtf8(tr(lang_from_key(g_settings.language), StringId::kAddColorSlot)).c_str());
    g_signal_connect(add, "clicked", G_CALLBACK(OnAddSlot), box);
    gtk_box_append(GTK_BOX(box), add);
  }
}

void BuildWindow() {
  if (g_window) return;
  g_window = gtk_window_new();
  gtk_window_set_title(GTK_WINDOW(g_window), "IME Aura");
  gtk_window_set_default_size(GTK_WINDOW(g_window), 420, 520);
  g_signal_connect(g_window, "close-request", G_CALLBACK(+[](GtkWindow*, gpointer) -> gboolean {
                     g_visible = false;
                     gtk_widget_set_visible(g_window, FALSE);
                     return TRUE;
                   }),
                   nullptr);

  GtkWidget* notebook = gtk_notebook_new();
  gtk_window_set_child(GTK_WINDOW(g_window), notebook);

  g_aura_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
  gtk_widget_set_margin_start(g_aura_box, 12);
  gtk_widget_set_margin_end(g_aura_box, 12);
  gtk_widget_set_margin_top(g_aura_box, 12);
  RebuildAura(g_aura_box);
  gtk_notebook_append_page(GTK_NOTEBOOK(notebook), g_aura_box, gtk_label_new("Aura"));

  GtkWidget* ff = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
  gtk_widget_set_margin_start(ff, 12);
  g_firefly_switch = gtk_switch_new();
  g_signal_connect(g_firefly_switch, "notify::active", G_CALLBACK(OnFireflyToggled), nullptr);
  gtk_box_append(GTK_BOX(ff), g_firefly_switch);
  g_firefly_status = gtk_label_new("");
  gtk_box_append(GTK_BOX(ff), g_firefly_status);
  gtk_notebook_append_page(GTK_NOTEBOOK(notebook), ff, gtk_label_new("Firefly"));

  GtkWidget* gen = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
  gtk_widget_set_margin_start(gen, 12);
  gtk_box_append(GTK_BOX(gen), gtk_label_new("Language"));
  size_t n = 0;
  const auto* cat = input_language_catalog(n);
  std::vector<std::string> owned;
  for (size_t i = 0; i < n; ++i) {
    owned.push_back(WideToUtf8(cat[i].native_name));
  }
  std::vector<const char*> labels;
  for (const auto& s : owned) labels.push_back(s.c_str());
  labels.push_back(nullptr);
  g_lang_drop = gtk_drop_down_new_from_strings(labels.data());
  g_signal_connect(g_lang_drop, "notify::selected", G_CALLBACK(OnLangChanged), nullptr);
  gtk_box_append(GTK_BOX(gen), g_lang_drop);
  gtk_notebook_append_page(GTK_NOTEBOOK(notebook), gen, gtk_label_new("General"));
}

void SyncUi() {
  if (!g_window) return;
  gtk_switch_set_active(GTK_SWITCH(g_firefly_switch), g_settings.firefly_enabled);
  gtk_label_set_text(GTK_LABEL(g_firefly_status), g_firefly_active ? "Busy" : "Available");
  size_t n = 0;
  const auto* cat = input_language_catalog(n);
  for (guint i = 0; i < n; ++i) {
    if (g_settings.language == cat[i].id) {
      gtk_drop_down_set_selected(GTK_DROP_DOWN(g_lang_drop), i);
      break;
    }
  }
  RebuildAura(g_aura_box);
}

}  // namespace

namespace linux_settings {

bool create(Settings initial, std::function<void(const Settings&)> cb) {
  g_settings = normalize_settings(initial);
  g_cb = std::move(cb);
  gtk_init();
  BuildWindow();
  SyncUi();
  return true;
}

void destroy() {
  if (g_window) {
    gtk_window_destroy(GTK_WINDOW(g_window));
    g_window = nullptr;
  }
  g_cb = nullptr;
  g_visible = false;
}

void show() {
  BuildWindow();
  SyncUi();
  gtk_widget_set_visible(g_window, TRUE);
  g_visible = true;
}

void hide() {
  if (g_window) gtk_widget_set_visible(g_window, FALSE);
  g_visible = false;
}

bool visible() { return g_visible; }

void sync(const Settings& s) {
  g_settings = normalize_settings(s);
  SyncUi();
}

void set_firefly_active(bool active) {
  g_firefly_active = active;
  SyncUi();
}

}  // namespace linux_settings
}  // namespace imeaura
