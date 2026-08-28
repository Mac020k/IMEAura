#include "platform/windows/win_firefly.h"

#include "core/firefly.h"
#include "platform/windows/win_ime.h"

#include <hidsdi.h>
#include <setupapi.h>
#include <mmdeviceapi.h>
#include <endpointvolume.h>

#include <atomic>
#include <condition_variable>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <thread>
#include <vector>

#pragma comment(lib, "hid.lib")
#pragma comment(lib, "setupapi.lib")

namespace imeaura {
namespace {

constexpr UINT kFireflyToggleMsg = WM_APP + 100;
constexpr ULONG_PTR kFireflyInjectTag = 0x46495245u;  // 'FIRE'

HHOOK g_hook = nullptr;
HWND g_target = nullptr;
WinFireflyBackend* g_self = nullptr;

bool IsInjected(const KBDLLHOOKSTRUCT* kbd) {
  return (kbd->flags & LLKHF_INJECTED) != 0 || kbd->dwExtraInfo == kFireflyInjectTag;
}

bool CapsLockOn() { return (GetKeyState(VK_CAPITAL) & 1) != 0; }

void SendTaggedVk(WORD vk, bool down) {
  INPUT in{};
  in.type = INPUT_KEYBOARD;
  in.ki.wVk = vk;
  in.ki.dwFlags = down ? 0 : KEYEVENTF_KEYUP;
  in.ki.dwExtraInfo = kFireflyInjectTag;
  SendInput(1, &in, sizeof(INPUT));
}

void SetCapsLockState(bool on) {
  if (CapsLockOn() == on) return;
  SendTaggedVk(VK_CAPITAL, true);
  SendTaggedVk(VK_CAPITAL, false);
}

bool ShiftDown() {
  return (GetAsyncKeyState(VK_SHIFT) & 0x8000) != 0 || (GetAsyncKeyState(VK_LSHIFT) & 0x8000) != 0 ||
         (GetAsyncKeyState(VK_RSHIFT) & 0x8000) != 0;
}

bool ModifiersDown() {
  return (GetAsyncKeyState(VK_CONTROL) & 0x8000) != 0 || (GetAsyncKeyState(VK_MENU) & 0x8000) != 0 ||
         (GetAsyncKeyState(VK_LWIN) & 0x8000) != 0 || (GetAsyncKeyState(VK_RWIN) & 0x8000) != 0;
}

void SendUnicodeChar(wchar_t ch, bool up) {
  INPUT in{};
  in.type = INPUT_KEYBOARD;
  in.ki.wVk = 0;
  in.ki.wScan = ch;
  in.ki.dwFlags = KEYEVENTF_UNICODE | (up ? KEYEVENTF_KEYUP : 0);
  in.ki.dwExtraInfo = kFireflyInjectTag;
  SendInput(1, &in, sizeof(INPUT));
}

bool SetLedHid(bool on) {
  GUID guid{};
  HidD_GetHidGuid(&guid);
  HDEVINFO info = SetupDiGetClassDevsW(&guid, nullptr, nullptr, DIGCF_PRESENT | DIGCF_DEVICEINTERFACE);
  if (info == INVALID_HANDLE_VALUE) return false;

  bool ok = false;
  SP_DEVICE_INTERFACE_DATA iface{};
  iface.cbSize = sizeof(iface);
  for (DWORD i = 0; SetupDiEnumDeviceInterfaces(info, nullptr, &guid, i, &iface); ++i) {
    DWORD need = 0;
    SetupDiGetDeviceInterfaceDetailW(info, &iface, nullptr, 0, &need, nullptr);
    if (!need) continue;
    std::vector<BYTE> buf(need);
    auto* detail = reinterpret_cast<SP_DEVICE_INTERFACE_DETAIL_DATA_W*>(buf.data());
    detail->cbSize = sizeof(SP_DEVICE_INTERFACE_DETAIL_DATA_W);
    if (!SetupDiGetDeviceInterfaceDetailW(info, &iface, detail, need, nullptr, nullptr)) continue;

    HANDLE hid = CreateFileW(detail->DevicePath, GENERIC_READ | GENERIC_WRITE,
                             FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, 0, nullptr);
    if (hid == INVALID_HANDLE_VALUE) continue;

    PHIDP_PREPARSED_DATA ppd = nullptr;
    if (HidD_GetPreparsedData(hid, &ppd)) {
      HIDP_CAPS caps{};
      if (HidP_GetCaps(ppd, &caps) == HIDP_STATUS_SUCCESS && caps.UsagePage == 0x01 && caps.Usage == 0x06 &&
          caps.OutputReportByteLength > 0) {
        USHORT n = 16;
        HIDP_VALUE_CAPS leds[16]{};
        if (HidP_GetSpecificValueCaps(HidP_Output, 0x08, 0, 0, leds, &n, ppd) == HIDP_STATUS_SUCCESS) {
          for (USHORT j = 0; j < n; ++j) {
            if (leds[j].Range.UsageMin <= 0x02 && leds[j].Range.UsageMax >= 0x02) {
              std::vector<BYTE> report(caps.OutputReportByteLength, 0);
              if (leds[j].ReportID) report[0] = static_cast<BYTE>(leds[j].ReportID);
              HidP_SetUsageValue(HidP_Output, 0x08, 0, 0x02, on ? 1 : 0, ppd,
                                 reinterpret_cast<PCHAR>(report.data()), static_cast<ULONG>(report.size()));
              if (HidD_SetOutputReport(hid, report.data(), static_cast<ULONG>(report.size()))) ok = true;
              break;
            }
          }
        }
      }
      HidD_FreePreparsedData(ppd);
    }
    CloseHandle(hid);
    if (ok) break;
  }
  SetupDiDestroyDeviceInfoList(info);
  return ok;
}

const wchar_t* kDndPaths[] = {
    L"Software\\Microsoft\\Windows\\CurrentVersion\\CloudStore\\Store\\DefaultAccount\\Current\\"
    L"{df7eeb95-82d3-4b7d-a1a6-d22a13380da6}$windows.data.donotdisturb.quiethourssettings\\"
    L"windows.data.donotdisturb.quiethourssettings",
    L"Software\\Microsoft\\Windows\\CurrentVersion\\CloudStore\\Store\\Cache\\DefaultAccount\\"
    L"$$windows.data.notifications.quiethourssettings\\Current",
};

HKEY OpenDnd(DWORD access) {
  for (const wchar_t* path : kDndPaths) {
    HKEY key = nullptr;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, path, 0, access, &key) == ERROR_SUCCESS) return key;
  }
  return nullptr;
}

std::vector<BYTE> ToUtf16Le(const std::wstring& s) {
  std::vector<BYTE> out(s.size() * 2);
  for (size_t i = 0; i < s.size(); ++i) {
    out[i * 2] = static_cast<BYTE>(s[i] & 0xFF);
    out[i * 2 + 1] = static_cast<BYTE>((s[i] >> 8) & 0xFF);
  }
  return out;
}

size_t FindPat(const std::vector<BYTE>& hay, const std::vector<BYTE>& needle) {
  if (needle.empty() || hay.size() < needle.size()) return static_cast<size_t>(-1);
  for (size_t i = 0; i + needle.size() <= hay.size(); ++i) {
    if (std::memcmp(hay.data() + i, needle.data(), needle.size()) == 0) return i;
  }
  return static_cast<size_t>(-1);
}

bool SwapProfile(std::vector<BYTE>& data, bool dnd_on) {
  const std::wstring unrestricted = L"Microsoft.QuietHoursProfile.Unrestricted";
  const std::wstring priority = L"Microsoft.QuietHoursProfile.PriorityOnly";
  const std::wstring alarms = L"Microsoft.QuietHoursProfile.AlarmsOnly";
  const std::wstring& to = dnd_on ? priority : unrestricted;
  const std::wstring* froms[] = {&unrestricted, &priority, &alarms};
  for (const std::wstring* from : froms) {
    if (*from == to) continue;
    auto fb = ToUtf16Le(*from);
    auto pos = FindPat(data, fb);
    if (pos == static_cast<size_t>(-1)) continue;
    auto tb = ToUtf16Le(to);
    data.erase(data.begin() + static_cast<std::ptrdiff_t>(pos),
               data.begin() + static_cast<std::ptrdiff_t>(pos + fb.size()));
    data.insert(data.begin() + static_cast<std::ptrdiff_t>(pos), tb.begin(), tb.end());
    return true;
  }
  return false;
}

std::string BackupPath() {
  char* appdata = nullptr;
  size_t len = 0;
  if (_dupenv_s(&appdata, &len, "APPDATA") != 0 || !appdata) return {};
  std::string p = (std::filesystem::path(appdata) / "IMEAura" / "dnd_backup.bin").string();
  free(appdata);
  return p;
}

void BackupDnd() {
  HKEY key = OpenDnd(KEY_READ);
  if (!key) return;
  DWORD size = 0;
  RegQueryValueExW(key, L"Data", nullptr, nullptr, nullptr, &size);
  if (!size) {
    RegCloseKey(key);
    return;
  }
  std::vector<BYTE> data(size);
  if (RegQueryValueExW(key, L"Data", nullptr, nullptr, data.data(), &size) != ERROR_SUCCESS) {
    RegCloseKey(key);
    return;
  }
  RegCloseKey(key);
  auto path = BackupPath();
  if (path.empty()) return;
  std::filesystem::create_directories(std::filesystem::path(path).parent_path());
  std::ofstream f(path, std::ios::binary);
  f.write(reinterpret_cast<const char*>(data.data()), static_cast<std::streamsize>(size));
}

void RestoreDnd() {
  auto path = BackupPath();
  if (path.empty() || !std::filesystem::exists(path)) return;
  std::ifstream f(path, std::ios::binary | std::ios::ate);
  if (!f) return;
  auto sz = f.tellg();
  if (sz <= 0) return;
  f.seekg(0);
  std::vector<BYTE> data(static_cast<size_t>(sz));
  f.read(reinterpret_cast<char*>(data.data()), sz);
  f.close();
  HKEY key = OpenDnd(KEY_WRITE);
  if (key) {
    RegSetValueExW(key, L"Data", 0, REG_BINARY, data.data(), static_cast<DWORD>(data.size()));
    RegCloseKey(key);
  }
  std::filesystem::remove(path);
}

void RestartWpn() {
  SC_HANDLE scm = OpenSCManagerW(nullptr, nullptr, SC_MANAGER_ENUMERATE_SERVICE);
  if (!scm) return;
  DWORD need = 0, count = 0, resume = 0;
  EnumServicesStatusExW(scm, SC_ENUM_PROCESS_INFO, SERVICE_WIN32, SERVICE_ACTIVE, nullptr, 0, &need, &count,
                        &resume, nullptr);
  if (!need) {
    CloseServiceHandle(scm);
    return;
  }
  std::vector<BYTE> buf(need);
  if (!EnumServicesStatusExW(scm, SC_ENUM_PROCESS_INFO, SERVICE_WIN32, SERVICE_ACTIVE, buf.data(), need, &need,
                             &count, &resume, nullptr)) {
    CloseServiceHandle(scm);
    return;
  }
  auto* svcs = reinterpret_cast<ENUM_SERVICE_STATUS_PROCESSW*>(buf.data());
  for (DWORD i = 0; i < count; ++i) {
    std::wstring name(svcs[i].lpServiceName);
    if (name.rfind(L"WpnUserService_", 0) != 0) continue;
    SC_HANDLE svc = OpenServiceW(scm, svcs[i].lpServiceName, SERVICE_STOP | SERVICE_START | SERVICE_QUERY_STATUS);
    if (!svc) break;
    SERVICE_STATUS st{};
    ControlService(svc, SERVICE_CONTROL_STOP, &st);
    for (int w = 0; w < 20 && st.dwCurrentState != SERVICE_STOPPED; ++w) {
      Sleep(100);
      QueryServiceStatus(svc, &st);
    }
    StartServiceW(svc, 0, nullptr);
    CloseServiceHandle(svc);
    break;
  }
  CloseServiceHandle(scm);
}

bool ProbeDnd() {
  HKEY key = OpenDnd(KEY_READ);
  if (!key) return false;
  DWORD type = 0, size = 0;
  LONG rc = RegQueryValueExW(key, L"Data", nullptr, &type, nullptr, &size);
  RegCloseKey(key);
  return rc == ERROR_SUCCESS && type == REG_BINARY && size >= 0x20;
}

bool ProbeKeepAwake() {
  const EXECUTION_STATE prev = SetThreadExecutionState(ES_CONTINUOUS);
  SetThreadExecutionState(ES_CONTINUOUS);
  return prev != static_cast<EXECUTION_STATE>(0);
}

bool ProbeMicMute() {
  IMMDeviceEnumerator* enumerator = nullptr;
  if (FAILED(CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL, __uuidof(IMMDeviceEnumerator),
                              reinterpret_cast<void**>(&enumerator)))) {
    return false;
  }
  IMMDevice* device = nullptr;
  const HRESULT hr = enumerator->GetDefaultAudioEndpoint(eCapture, eConsole, &device);
  enumerator->Release();
  if (FAILED(hr) || !device) return false;
  IAudioEndpointVolume* volume = nullptr;
  const HRESULT hr2 = device->Activate(__uuidof(IAudioEndpointVolume), CLSCTX_ALL, nullptr,
                                       reinterpret_cast<void**>(&volume));
  device->Release();
  if (FAILED(hr2) || !volume) return false;
  volume->Release();
  return true;
}

bool ProbeVoiceInput() { return true; }

void SendVoiceTypingShortcut() {
  SendTaggedVk(VK_LWIN, true);
  SendTaggedVk('H', true);
  SendTaggedVk('H', false);
  SendTaggedVk(VK_LWIN, false);
}

LRESULT CALLBACK HookProc(int code, WPARAM wp, LPARAM lp) {
  if (!g_self) return CallNextHookEx(nullptr, code, wp, lp);
  return g_self->filter_key(code, wp, lp);
}

}  // namespace

struct WinFireflyBackend::Impl {
  std::function<void()> on_toggle;
  std::atomic<bool> busy{false};
  std::string caps_mode = kFireflyCapsUppercase;
  std::string led_mode = kFireflyLedAuto;
  std::string busy_action = kFireflyBusyDnd;
  bool keep_display_on = false;
  bool preserved_caps_on = false;
  bool saved_caps_on = false;
  bool hid_ok = false;
  bool dnd_ok = false;
  bool keep_awake_ok = false;
  bool mic_mute_ok = false;
  bool voice_ok = false;
  bool dnd_backed_up = false;
  bool use_plan_b = false;
  bool keep_awake_active = false;
  bool mic_saved_valid = false;
  BOOL mic_was_muted = FALSE;

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
      HKEY key = OpenDnd(KEY_READ | KEY_WRITE);
      if (key) {
        DWORD size = 0;
        RegQueryValueExW(key, L"Data", nullptr, nullptr, nullptr, &size);
        if (size >= 0x20) {
          std::vector<BYTE> data(size);
          if (RegQueryValueExW(key, L"Data", nullptr, nullptr, data.data(), &size) == ERROR_SUCCESS) {
            if (SwapProfile(data, target)) {
              RegSetValueExW(key, L"Data", 0, REG_BINARY, data.data(), static_cast<DWORD>(data.size()));
            }
          }
        }
        RegCloseKey(key);
      }
      RestartWpn();
    }
  }
};

WinFireflyBackend::WinFireflyBackend() = default;
WinFireflyBackend::~WinFireflyBackend() { stop(); }

FireflyCapabilities WinFireflyBackend::capabilities() const {
  FireflyCapabilities c{};
  c.can_intercept_caps = true;
  c.can_drive_led = true;
  c.can_set_dnd = impl_ ? impl_->dnd_ok : ProbeDnd();
  c.can_keep_awake = impl_ ? impl_->keep_awake_ok : ProbeKeepAwake();
  c.can_mute_mic = impl_ ? impl_->mic_mute_ok : ProbeMicMute();
  c.can_trigger_voice_input = impl_ ? impl_->voice_ok : ProbeVoiceInput();
  return c;
}

bool WinFireflyBackend::start(std::function<void()> on_toggle, const std::string& caps_mode) {
  if (impl_) return true;
  impl_ = new Impl();
  impl_->on_toggle = std::move(on_toggle);
  impl_->caps_mode = caps_mode.empty() ? kFireflyCapsUppercase : caps_mode;
  impl_->busy.store(false);
  impl_->saved_caps_on = CapsLockOn();
  impl_->preserved_caps_on = impl_->saved_caps_on;
  impl_->dnd_ok = ProbeDnd();
  impl_->keep_awake_ok = ProbeKeepAwake();
  impl_->mic_mute_ok = ProbeMicMute();
  impl_->voice_ok = ProbeVoiceInput();
  impl_->hid_ok = SetLedHid(false);
  impl_->use_plan_b = false;

  if (impl_->caps_mode == kFireflyCapsUppercase) {
    SetCapsLockState(true);
  } else if (impl_->caps_mode == kFireflyCapsLowercase) {
    SetCapsLockState(false);
  }

  RestoreDnd();
  BackupDnd();
  impl_->dnd_backed_up = true;
  impl_->dnd_stop.store(false);
  impl_->dnd_thread = std::thread([this] { impl_->dnd_worker(); });

  g_self = this;
  g_hook = SetWindowsHookExW(WH_KEYBOARD_LL, HookProc, GetModuleHandleW(nullptr), 0);
  if (!g_hook) {
    stop();
    return false;
  }

  set_led(false);
  set_dnd(false);
  return true;
}

void WinFireflyBackend::stop() {
  if (!impl_) return;
  if (g_hook) {
    UnhookWindowsHookEx(g_hook);
    g_hook = nullptr;
  }
  g_self = nullptr;
  g_target = nullptr;

  {
    std::lock_guard lk(impl_->dnd_mu);
    impl_->dnd_stop.store(true);
    impl_->dnd_cv.notify_one();
  }
  if (impl_->dnd_thread.joinable()) impl_->dnd_thread.join();

  if (impl_->dnd_backed_up) {
    RestoreDnd();
    RestartWpn();
  }
  clear_sustained_busy_effects();
  SetCapsLockState(impl_->saved_caps_on);
  delete impl_;
  impl_ = nullptr;
}

void WinFireflyBackend::set_caps_mode(const std::string& mode) {
  if (!impl_) return;
  impl_->caps_mode = mode.empty() ? kFireflyCapsUppercase : mode;
  if (mode == kFireflyCapsUppercase) {
    SetCapsLockState(true);
  } else if (mode == kFireflyCapsLowercase) {
    SetCapsLockState(false);
  } else if (mode == kFireflyCapsPreserve) {
    impl_->preserved_caps_on = CapsLockOn();
  }
  set_led(impl_->busy.load());
}

void WinFireflyBackend::set_led_mode(const std::string& mode) {
  if (!impl_) return;
  impl_->led_mode = mode.empty() ? kFireflyLedAuto : mode;
  set_led(impl_->busy.load());
}

void WinFireflyBackend::set_busy_action(const std::string& action, bool keep_display_on) {
  if (!impl_) return;
  const bool was_busy = impl_->busy.load();
  if (was_busy) clear_sustained_busy_effects();
  impl_->busy_action = normalize_busy_action(action);
  impl_->keep_display_on = keep_display_on;
  if (was_busy) {
    const auto fx = resolve_busy_effects(impl_->busy_action, true, false, impl_->keep_display_on);
    apply_busy_effects(fx);
  }
}

void WinFireflyBackend::set_led(bool on) {
  if (!impl_) return;
  if (impl_->led_mode == kFireflyLedNone) return;

  if (impl_->led_mode == kFireflyLedHid || impl_->use_plan_b) {
    impl_->hid_ok = SetLedHid(on);
    if (impl_->led_mode == kFireflyLedHid || impl_->use_plan_b) return;
  }
  // Plan A auto: CapsLock toggle bit is the Busy lamp.
  SetCapsLockState(on);
}

void WinFireflyBackend::set_dnd(bool on) {
  if (!impl_ || !impl_->dnd_ok) return;
  std::lock_guard lk(impl_->dnd_mu);
  impl_->dnd_target = on;
  impl_->dnd_pending = true;
  impl_->dnd_cv.notify_one();
}

void WinFireflyBackend::set_keep_awake(bool on, bool keep_display_on) {
  if (!impl_ || !impl_->keep_awake_ok) return;
  if (on) {
    EXECUTION_STATE flags = ES_CONTINUOUS | ES_SYSTEM_REQUIRED;
    if (keep_display_on) flags = static_cast<EXECUTION_STATE>(flags | ES_DISPLAY_REQUIRED);
    SetThreadExecutionState(flags);
    impl_->keep_awake_active = true;
  } else if (impl_->keep_awake_active) {
    SetThreadExecutionState(ES_CONTINUOUS);
    impl_->keep_awake_active = false;
  }
}

void WinFireflyBackend::set_mic_mute(bool on) {
  if (!impl_ || !impl_->mic_mute_ok) return;
  IMMDeviceEnumerator* enumerator = nullptr;
  if (FAILED(CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL, __uuidof(IMMDeviceEnumerator),
                                reinterpret_cast<void**>(&enumerator)))) {
    return;
  }
  IMMDevice* device = nullptr;
  if (FAILED(enumerator->GetDefaultAudioEndpoint(eCapture, eConsole, &device))) {
    enumerator->Release();
    return;
  }
  enumerator->Release();
  IAudioEndpointVolume* volume = nullptr;
  if (FAILED(device->Activate(__uuidof(IAudioEndpointVolume), CLSCTX_ALL, nullptr,
                              reinterpret_cast<void**>(&volume)))) {
    device->Release();
    return;
  }
  device->Release();
  if (on) {
    if (!impl_->mic_saved_valid) {
      volume->GetMute(&impl_->mic_was_muted);
      impl_->mic_saved_valid = true;
    }
    volume->SetMute(TRUE, nullptr);
  } else if (impl_->mic_saved_valid) {
    volume->SetMute(impl_->mic_was_muted, nullptr);
    impl_->mic_saved_valid = false;
  }
  volume->Release();
}

void WinFireflyBackend::trigger_voice_input() {
  if (!impl_ || !impl_->voice_ok) return;
  SendVoiceTypingShortcut();
}

void WinFireflyBackend::clear_sustained_busy_effects() {
  set_dnd(false);
  set_keep_awake(false, false);
  set_mic_mute(false);
}

void WinFireflyBackend::apply_busy_effects(const FireflyBusyEffects& fx) {
  set_dnd(fx.want_dnd);
  set_keep_awake(fx.want_keep_awake, fx.keep_display_on);
  set_mic_mute(fx.want_mic_mute);
  if (fx.trigger_voice_input) trigger_voice_input();
}

bool WinFireflyBackend::is_active() const { return impl_ && impl_->busy.load(); }

void WinFireflyBackend::handle_toggle() {
  if (!impl_) return;
  const bool prev = impl_->busy.load();
  const bool next = !prev;
  impl_->busy.store(next);
  FireflyInput in{};
  in.enabled = true;
  in.toggle_requested = true;
  in.current_active = prev;
  in.busy_action = impl_->busy_action;
  in.keep_display_on = impl_->keep_display_on;
  const FireflyOutput out = evaluate_firefly(in);
  set_led(out.want_led_on);
  apply_busy_effects(out.effects);
  if (impl_->on_toggle) impl_->on_toggle();
}

void WinFireflyBackend::set_target_hwnd(HWND hwnd) { g_target = hwnd; }

LRESULT WinFireflyBackend::filter_key(int code, WPARAM wp, LPARAM lp) {
  if (code != HC_ACTION || !impl_) return CallNextHookEx(nullptr, code, wp, lp);
  auto* kbd = reinterpret_cast<KBDLLHOOKSTRUCT*>(lp);
  if (IsInjected(kbd)) return CallNextHookEx(nullptr, code, wp, lp);

  const bool down = (wp == WM_KEYDOWN || wp == WM_SYSKEYDOWN);
  const bool up = (wp == WM_KEYUP || wp == WM_SYSKEYUP);

  if (kbd->vkCode == VK_CAPITAL) {
    if (down && g_target) PostMessageW(g_target, kFireflyToggleMsg, 0, 0);
    return 1;
  }

  if (!impl_->use_plan_b && kbd->vkCode >= 'A' && kbd->vkCode <= 'Z') {
    if (!ModifiersDown() && !win_is_japanese_input()) {
      if (down) {
        const bool upper =
            firefly_want_uppercase(impl_->caps_mode, ShiftDown(), impl_->preserved_caps_on);
        const wchar_t ch = static_cast<wchar_t>((upper ? L'A' : L'a') + (kbd->vkCode - 'A'));
        SendUnicodeChar(ch, false);
        SendUnicodeChar(ch, true);
      }
      return 1;
    }
  }
  (void)up;
  return CallNextHookEx(nullptr, code, wp, lp);
}

}  // namespace imeaura
