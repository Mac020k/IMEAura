#include "platform/linux/linux_firefly.h"

#include "core/firefly.h"
#include "platform/linux/linux_ime.h"

#include <X11/X.h>
#include <X11/XKBlib.h>
#include <X11/Xlib.h>
#include <X11/extensions/XTest.h>
#include <X11/keysym.h>

#include <atomic>
#include <condition_variable>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace imeaura {
namespace {

LinuxFireflyBackend* g_self = nullptr;

bool RunCommand(const std::string& cmd) { return std::system(cmd.c_str()) == 0; }

bool CapsLockOn(Display* dpy) {
  if (!dpy) return false;
  unsigned int state = 0;
  if (XkbGetIndicatorState(dpy, XkbUseCoreKbd, &state) != Success) return false;
  return (state & 0x01) != 0;  // Caps lock indicator bit
}

void SendTaggedKey(Display* dpy, KeyCode key, bool down) {
  if (!dpy) return;
  XTestFakeKeyEvent(dpy, key, down ? True : False, CurrentTime);
  XFlush(dpy);
}

void SetCapsLockState(Display* dpy, KeyCode caps_code, bool on) {
  if (!dpy || !caps_code) return;
  if (CapsLockOn(dpy) == on) return;
  SendTaggedKey(dpy, caps_code, true);
  SendTaggedKey(dpy, caps_code, false);
}

bool ShiftDown(Display* dpy) {
  if (!dpy) return false;
  char keys[32]{};
  XQueryKeymap(dpy, keys);
  const KeyCode shift_l = XKeysymToKeycode(dpy, XK_Shift_L);
  const KeyCode shift_r = XKeysymToKeycode(dpy, XK_Shift_R);
  const bool l = shift_l && (keys[shift_l / 8] & (1 << (shift_l % 8))) != 0;
  const bool r = shift_r && (keys[shift_r / 8] & (1 << (shift_r % 8))) != 0;
  return l || r;
}

bool ModifiersDown(Display* dpy) {
  if (!dpy) return false;
  char keys[32]{};
  XQueryKeymap(dpy, keys);
  const KeyCode codes[] = {
      XKeysymToKeycode(dpy, XK_Control_L), XKeysymToKeycode(dpy, XK_Control_R),
      XKeysymToKeycode(dpy, XK_Alt_L),     XKeysymToKeycode(dpy, XK_Alt_R),
      XKeysymToKeycode(dpy, XK_Super_L),   XKeysymToKeycode(dpy, XK_Super_R),
  };
  for (KeyCode code : codes) {
    if (code && (keys[code / 8] & (1 << (code % 8))) != 0) return true;
  }
  return false;
}

void SendUnicodeChar(Display* dpy, wchar_t ch) {
  if (!dpy) return;
  KeySym sym = static_cast<KeySym>(ch);
  KeyCode code = XKeysymToKeycode(dpy, sym);
  if (!code) return;
  SendTaggedKey(dpy, code, true);
  SendTaggedKey(dpy, code, false);
}

bool SetLedSysfs(bool on) {
  namespace fs = std::filesystem;
  if (!fs::exists("/sys/class/leds")) return false;
  bool ok = false;
  for (const auto& entry : fs::directory_iterator("/sys/class/leds")) {
    const std::string name = entry.path().filename().string();
    if (name.find("capslock") == std::string::npos && name.find("CapsLock") == std::string::npos) continue;
    std::ofstream f(entry.path() / "brightness");
    if (!f) continue;
    f << (on ? 1 : 0);
    ok = f.good();
    if (ok) break;
  }
  return ok;
}

std::string DndBackupPath() {
  const char* xdg = std::getenv("XDG_CONFIG_HOME");
  std::filesystem::path base;
  if (xdg) base = xdg;
  else {
    const char* home = std::getenv("HOME");
    if (!home) return {};
    base = std::filesystem::path(home) / ".config";
  }
  return (base / "ime_aura" / "dnd_backup.txt").string();
}

bool GnomeDndWritable() {
  return RunCommand("gsettings writable org.gnome.desktop.notifications disable-notifications 2>/dev/null | "
                    "grep -q true");
}

bool ReadGnomeDnd(bool& on) {
  FILE* f = popen("gsettings get org.gnome.desktop.notifications disable-notifications 2>/dev/null", "r");
  if (!f) return false;
  char buf[64]{};
  const bool got = fgets(buf, sizeof(buf), f) != nullptr;
  pclose(f);
  if (!got) return false;
  on = std::strncmp(buf, "true", 4) == 0;
  return true;
}

bool WriteGnomeDnd(bool on) {
  const char* val = on ? "true" : "false";
  std::string cmd =
      std::string("gsettings set org.gnome.desktop.notifications disable-notifications ") + val;
  return RunCommand(cmd);
}

bool ProbeDnd() { return GnomeDndWritable(); }

void BackupDnd() {
  bool on = false;
  if (!ReadGnomeDnd(on)) return;
  auto path = DndBackupPath();
  if (path.empty()) return;
  std::filesystem::create_directories(std::filesystem::path(path).parent_path());
  std::ofstream f(path);
  f << (on ? "1" : "0");
}

void RestoreDnd() {
  auto path = DndBackupPath();
  if (path.empty() || !std::filesystem::exists(path)) return;
  std::ifstream f(path);
  char c = '0';
  f >> c;
  WriteGnomeDnd(c == '1');
  std::filesystem::remove(path);
}

}  // namespace

struct LinuxFireflyBackend::Impl {
  Display* dpy = nullptr;
  Window root = 0;
  KeyCode caps_code = 0;
  int xtest_event_base = 0;
  int xtest_error_base = 0;
  int xtest_major = 0;
  int xtest_minor = 0;
  bool xtest_ok = false;

  std::function<void()> on_toggle;
  std::atomic<bool> busy{false};
  std::string caps_mode = kFireflyCapsUppercase;
  std::string led_mode = kFireflyLedAuto;
  bool preserved_caps_on = false;
  bool saved_caps_on = false;
  bool hid_led_ok = false;
  bool dnd_ok = false;
  bool dnd_backed_up = false;
  bool use_sysfs_led = false;

  std::mutex dnd_mu;
  std::condition_variable dnd_cv;
  std::atomic<bool> dnd_stop{true};
  bool dnd_target = false;
  bool dnd_pending = false;
  std::thread dnd_thread;

  void dnd_worker() {
    while (true) {
      bool target = false;
      {
        std::unique_lock lk(dnd_mu);
        dnd_cv.wait(lk, [&] { return dnd_pending || dnd_stop.load(); });
        if (dnd_stop.load() && !dnd_pending) break;
        target = dnd_target;
        dnd_pending = false;
      }
      WriteGnomeDnd(target);
    }
  }

  void grab_letter_keys() {
    if (!dpy || !root) return;
    for (int sym = XK_a; sym <= XK_z; ++sym) {
      const KeyCode code = XKeysymToKeycode(dpy, static_cast<KeySym>(sym));
      if (code) XGrabKey(dpy, code, AnyModifier, root, True, GrabModeAsync, GrabModeAsync);
    }
    for (int sym = XK_A; sym <= XK_Z; ++sym) {
      const KeyCode code = XKeysymToKeycode(dpy, static_cast<KeySym>(sym));
      if (code) XGrabKey(dpy, code, AnyModifier, root, True, GrabModeAsync, GrabModeAsync);
    }
    XSync(dpy, False);
  }

  void ungrab_letter_keys() {
    if (!dpy || !root) return;
    for (int sym = XK_a; sym <= XK_z; ++sym) {
      const KeyCode code = XKeysymToKeycode(dpy, static_cast<KeySym>(sym));
      if (code) XUngrabKey(dpy, code, AnyModifier, root);
    }
    for (int sym = XK_A; sym <= XK_Z; ++sym) {
      const KeyCode code = XKeysymToKeycode(dpy, static_cast<KeySym>(sym));
      if (code) XUngrabKey(dpy, code, AnyModifier, root);
    }
    XSync(dpy, False);
  }

  void on_caps_press() {
    if (g_self) g_self->handle_toggle();
  }
};

LinuxFireflyBackend::LinuxFireflyBackend() = default;
LinuxFireflyBackend::~LinuxFireflyBackend() { stop(); }

FireflyCapabilities LinuxFireflyBackend::capabilities() const {
  FireflyCapabilities c{};
  c.can_intercept_caps = impl_ && impl_->dpy != nullptr;
  c.can_drive_led = true;
  c.can_set_dnd = impl_ ? impl_->dnd_ok : ProbeDnd();
  return c;
}

bool LinuxFireflyBackend::start(std::function<void()> on_toggle, const std::string& caps_mode) {
  if (impl_) return true;
  impl_ = new Impl();
  impl_->on_toggle = std::move(on_toggle);
  impl_->caps_mode = caps_mode.empty() ? kFireflyCapsUppercase : caps_mode;
  impl_->busy.store(false);
  impl_->dnd_ok = ProbeDnd();
  impl_->hid_led_ok = SetLedSysfs(false);

  impl_->dpy = XOpenDisplay(nullptr);
  if (!impl_->dpy) {
    stop();
    return false;
  }
  impl_->root = DefaultRootWindow(impl_->dpy);
  impl_->caps_code = XKeysymToKeycode(impl_->dpy, XK_Caps_Lock);
  impl_->xtest_ok =
      XTestQueryExtension(impl_->dpy, &impl_->xtest_event_base, &impl_->xtest_error_base, &impl_->xtest_major,
                          &impl_->xtest_minor);
  if (!impl_->xtest_ok || !impl_->caps_code) {
    stop();
    return false;
  }

  impl_->saved_caps_on = CapsLockOn(impl_->dpy);
  impl_->preserved_caps_on = impl_->saved_caps_on;

  if (impl_->caps_mode == kFireflyCapsUppercase) {
    SetCapsLockState(impl_->dpy, impl_->caps_code, true);
  } else if (impl_->caps_mode == kFireflyCapsLowercase) {
    SetCapsLockState(impl_->dpy, impl_->caps_code, false);
  }

  XGrabKey(impl_->dpy, impl_->caps_code, AnyModifier, impl_->root, True, GrabModeAsync, GrabModeAsync);
  impl_->grab_letter_keys();
  XSelectInput(impl_->dpy, impl_->root, KeyPressMask | KeyReleaseMask);
  XSync(impl_->dpy, False);

  RestoreDnd();
  BackupDnd();
  impl_->dnd_backed_up = true;
  impl_->dnd_stop.store(false);
  impl_->dnd_thread = std::thread([this] { impl_->dnd_worker(); });

  g_self = this;
  set_led(false);
  set_dnd(false);
  return true;
}

void LinuxFireflyBackend::stop() {
  if (!impl_) return;

  if (impl_->dpy && impl_->caps_code) {
    impl_->ungrab_letter_keys();
    XUngrabKey(impl_->dpy, impl_->caps_code, AnyModifier, impl_->root);
    XSync(impl_->dpy, False);
  }
  g_self = nullptr;

  {
    std::lock_guard lk(impl_->dnd_mu);
    impl_->dnd_stop.store(true);
    impl_->dnd_cv.notify_one();
  }
  if (impl_->dnd_thread.joinable()) impl_->dnd_thread.join();

  if (impl_->dnd_backed_up) RestoreDnd();
  if (impl_->dpy) {
    SetCapsLockState(impl_->dpy, impl_->caps_code, impl_->saved_caps_on);
    XCloseDisplay(impl_->dpy);
    impl_->dpy = nullptr;
  }
  delete impl_;
  impl_ = nullptr;
}

void LinuxFireflyBackend::set_caps_mode(const std::string& mode) {
  if (!impl_ || !impl_->dpy) return;
  impl_->caps_mode = mode.empty() ? kFireflyCapsUppercase : mode;
  if (mode == kFireflyCapsUppercase) {
    SetCapsLockState(impl_->dpy, impl_->caps_code, true);
  } else if (mode == kFireflyCapsLowercase) {
    SetCapsLockState(impl_->dpy, impl_->caps_code, false);
  } else if (mode == kFireflyCapsPreserve) {
    impl_->preserved_caps_on = CapsLockOn(impl_->dpy);
  }
  set_led(impl_->busy.load());
}

void LinuxFireflyBackend::set_led_mode(const std::string& mode) {
  if (!impl_) return;
  impl_->led_mode = mode.empty() ? kFireflyLedAuto : mode;
  impl_->use_sysfs_led = impl_->led_mode == kFireflyLedHid || (impl_->led_mode == kFireflyLedAuto && impl_->hid_led_ok);
  set_led(impl_->busy.load());
}

void LinuxFireflyBackend::set_led(bool on) {
  if (!impl_) return;
  if (impl_->led_mode == kFireflyLedNone) return;

  if (impl_->use_sysfs_led || impl_->led_mode == kFireflyLedHid) {
    if (SetLedSysfs(on)) return;
  }

  if (impl_->dpy && impl_->caps_code) SetCapsLockState(impl_->dpy, impl_->caps_code, on);
}

void LinuxFireflyBackend::set_dnd(bool on) {
  if (!impl_) return;
  std::lock_guard lk(impl_->dnd_mu);
  impl_->dnd_target = on;
  impl_->dnd_pending = true;
  impl_->dnd_cv.notify_one();
}

bool LinuxFireflyBackend::is_active() const { return impl_ && impl_->busy.load(); }

void LinuxFireflyBackend::handle_toggle() {
  if (!impl_) return;
  const bool next = !impl_->busy.load();
  impl_->busy.store(next);
  FireflyInput in{};
  in.enabled = true;
  in.toggle_requested = true;
  in.current_active = !next;
  const FireflyOutput out = evaluate_firefly(in);
  set_led(out.want_led_on);
  set_dnd(out.want_dnd);
  if (impl_->on_toggle) impl_->on_toggle();
}

void LinuxFireflyBackend::process_x11_events() {
  if (!impl_ || !impl_->dpy) return;
  while (XPending(impl_->dpy)) {
    XEvent ev{};
    XNextEvent(impl_->dpy, &ev);
    if (ev.type == KeyPress && ev.xkey.keycode == impl_->caps_code) {
      handle_toggle();
      continue;
    }
    if (ev.type == KeyPress || ev.type == KeyRelease) {
      const KeySym sym = XkbKeycodeToKeysym(impl_->dpy, ev.xkey.keycode, 0, 0);
      if ((sym >= XK_a && sym <= XK_z) || (sym >= XK_A && sym <= XK_Z)) {
        if (!ModifiersDown(impl_->dpy) && !linux_is_japanese_input()) {
          if (ev.type == KeyPress) {
            const bool upper =
                firefly_want_uppercase(impl_->caps_mode, ShiftDown(impl_->dpy), impl_->preserved_caps_on);
            const wchar_t base = upper ? L'A' : L'a';
            const int offset = (sym >= XK_a && sym <= XK_z) ? (sym - XK_a) : (sym - XK_A);
            SendUnicodeChar(impl_->dpy, static_cast<wchar_t>(base + offset));
          }
          continue;
        }
      }
    }
  }
}

}  // namespace imeaura
