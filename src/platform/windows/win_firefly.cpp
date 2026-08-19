#include "platform/windows/win_firefly.h"

#include <windows.h>
#include <hidsdi.h>
#include <hidpi.h>
#include <setupapi.h>

#include <atomic>
#include <condition_variable>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#pragma comment(lib, "hid.lib")
#pragma comment(lib, "setupapi.lib")

namespace imeaura {
namespace {

constexpr UINT kFireflyToggleMsg = WM_APP + 100;

HHOOK g_caps_hook = nullptr;
HWND g_hook_target = nullptr;

LRESULT CALLBACK CapsHookProc(int code, WPARAM wp, LPARAM lp) {
  if (code == HC_ACTION) {
    auto* kbd = reinterpret_cast<KBDLLHOOKSTRUCT*>(lp);
    if (kbd->vkCode == VK_CAPITAL) {
      if (wp == WM_KEYDOWN || wp == WM_SYSKEYDOWN) {
        if (g_hook_target) PostMessageW(g_hook_target, kFireflyToggleMsg, 0, 0);
      }
      return 1;
    }
  }
  return CallNextHookEx(nullptr, code, wp, lp);
}

bool SetCapsLockLed(bool on) {
  GUID hid_guid{};
  HidD_GetHidGuid(&hid_guid);
  HDEVINFO dev_info = SetupDiGetClassDevsW(&hid_guid, nullptr, nullptr,
                                           DIGCF_PRESENT | DIGCF_DEVICEINTERFACE);
  if (dev_info == INVALID_HANDLE_VALUE) return false;

  bool success = false;
  SP_DEVICE_INTERFACE_DATA iface{};
  iface.cbSize = sizeof(iface);

  for (DWORD i = 0; SetupDiEnumDeviceInterfaces(dev_info, nullptr, &hid_guid, i, &iface); ++i) {
    DWORD required = 0;
    SetupDiGetDeviceInterfaceDetailW(dev_info, &iface, nullptr, 0, &required, nullptr);
    if (required == 0) continue;

    std::vector<BYTE> buf(required);
    auto* detail = reinterpret_cast<SP_DEVICE_INTERFACE_DETAIL_DATA_W*>(buf.data());
    detail->cbSize = sizeof(SP_DEVICE_INTERFACE_DETAIL_DATA_W);
    if (!SetupDiGetDeviceInterfaceDetailW(dev_info, &iface, detail, required, nullptr, nullptr))
      continue;

    HANDLE hid = CreateFileW(detail->DevicePath, GENERIC_READ | GENERIC_WRITE,
                             FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, 0, nullptr);
    if (hid == INVALID_HANDLE_VALUE) continue;

    PHIDP_PREPARSED_DATA ppd = nullptr;
    if (HidD_GetPreparsedData(hid, &ppd)) {
      HIDP_CAPS caps{};
      if (HidP_GetCaps(ppd, &caps) == HIDP_STATUS_SUCCESS && caps.OutputReportByteLength > 0) {
        USHORT num_leds = 0;
        HIDP_VALUE_CAPS led_caps[16]{};
        num_leds = 16;
        if (HidP_GetSpecificValueCaps(HidP_Output, 0x08, 0, 0, led_caps, &num_leds, ppd) ==
            HIDP_STATUS_SUCCESS) {
          for (USHORT j = 0; j < num_leds; ++j) {
            if (led_caps[j].Range.UsageMin <= 0x02 && led_caps[j].Range.UsageMax >= 0x02) {
              std::vector<BYTE> report(caps.OutputReportByteLength, 0);
              HidP_SetUsageValue(HidP_Output, 0x08, 0, 0x02, on ? 1 : 0, ppd, (PCHAR)report.data(),
                                 (ULONG)report.size());
              if (HidD_SetOutputReport(hid, report.data(), (ULONG)report.size())) {
                success = true;
              }
              break;
            }
          }
        }
      }
      HidD_FreePreparsedData(ppd);
    }
    CloseHandle(hid);
  }
  SetupDiDestroyDeviceInfoList(dev_info);
  return success;
}

std::wstring DndRegPath() {
  return L"Software\\Microsoft\\Windows\\CurrentVersion\\CloudStore\\Store\\Cache\\DefaultAccount\\"
         L"$$windows.data.notifications.quiethourssettings\\Current";
}

bool ReadDndState(bool& dnd_on) {
  HKEY key = nullptr;
  if (RegOpenKeyExW(HKEY_CURRENT_USER, DndRegPath().c_str(), 0, KEY_READ, &key) != ERROR_SUCCESS)
    return false;

  DWORD type = 0, size = 0;
  RegQueryValueExW(key, L"Data", nullptr, &type, nullptr, &size);
  if (type != REG_BINARY || size < 0x20) {
    RegCloseKey(key);
    return false;
  }

  std::vector<BYTE> data(size);
  if (RegQueryValueExW(key, L"Data", nullptr, nullptr, data.data(), &size) != ERROR_SUCCESS) {
    RegCloseKey(key);
    return false;
  }
  RegCloseKey(key);

  std::wstring blob(reinterpret_cast<const wchar_t*>(data.data() + 0x1a),
                    (size - 0x1a) / sizeof(wchar_t));
  dnd_on = blob.find(L"Microsoft.QuietHoursProfile.PriorityOnly") != std::wstring::npos;
  return true;
}

std::string BackupPath() {
  char* appdata = nullptr;
  size_t len = 0;
  if (_dupenv_s(&appdata, &len, "APPDATA") != 0 || !appdata) return "";
  std::string result = (std::filesystem::path(appdata) / "IMEAura" / "dnd_backup.bin").string();
  free(appdata);
  return result;
}

void BackupDndBlob() {
  HKEY key = nullptr;
  if (RegOpenKeyExW(HKEY_CURRENT_USER, DndRegPath().c_str(), 0, KEY_READ, &key) != ERROR_SUCCESS)
    return;
  DWORD size = 0;
  RegQueryValueExW(key, L"Data", nullptr, nullptr, nullptr, &size);
  if (size == 0) { RegCloseKey(key); return; }
  std::vector<BYTE> data(size);
  RegQueryValueExW(key, L"Data", nullptr, nullptr, data.data(), &size);
  RegCloseKey(key);

  auto path = BackupPath();
  if (path.empty()) return;
  std::filesystem::create_directories(std::filesystem::path(path).parent_path());
  std::ofstream f(path, std::ios::binary);
  f.write(reinterpret_cast<const char*>(data.data()), static_cast<std::streamsize>(size));
}

void RestoreDndBlob() {
  auto path = BackupPath();
  if (path.empty() || !std::filesystem::exists(path)) return;
  std::ifstream f(path, std::ios::binary | std::ios::ate);
  if (!f) return;
  const auto size = f.tellg();
  if (size <= 0) return;
  f.seekg(0);
  std::vector<BYTE> data(static_cast<size_t>(size));
  f.read(reinterpret_cast<char*>(data.data()), size);
  f.close();

  HKEY key = nullptr;
  if (RegOpenKeyExW(HKEY_CURRENT_USER, DndRegPath().c_str(), 0, KEY_WRITE, &key) == ERROR_SUCCESS) {
    RegSetValueExW(key, L"Data", 0, REG_BINARY, data.data(), static_cast<DWORD>(data.size()));
    RegCloseKey(key);
  }
  std::filesystem::remove(path);
}

void RestartWpnService() {
  SC_HANDLE scm = OpenSCManagerW(nullptr, nullptr, SC_MANAGER_ENUMERATE_SERVICE);
  if (!scm) return;

  DWORD needed = 0, count = 0, resume = 0;
  EnumServicesStatusExW(scm, SC_ENUM_PROCESS_INFO, SERVICE_WIN32, SERVICE_ACTIVE,
                        nullptr, 0, &needed, &count, &resume, nullptr);
  std::vector<BYTE> buf(needed);
  EnumServicesStatusExW(scm, SC_ENUM_PROCESS_INFO, SERVICE_WIN32, SERVICE_ACTIVE,
                        buf.data(), needed, &needed, &count, &resume, nullptr);

  auto* svcs = reinterpret_cast<ENUM_SERVICE_STATUS_PROCESSW*>(buf.data());
  for (DWORD i = 0; i < count; ++i) {
    std::wstring name(svcs[i].lpServiceName);
    if (name.find(L"WpnUserService_") == 0) {
      SC_HANDLE svc = OpenServiceW(scm, svcs[i].lpServiceName,
                                   SERVICE_STOP | SERVICE_START | SERVICE_QUERY_STATUS);
      if (svc) {
        SERVICE_STATUS status{};
        ControlService(svc, SERVICE_CONTROL_STOP, &status);
        for (int w = 0; w < 20 && status.dwCurrentState != SERVICE_STOPPED; ++w) {
          Sleep(100);
          QueryServiceStatus(svc, &status);
        }
        StartServiceW(svc, 0, nullptr);
        CloseServiceHandle(svc);
      }
      break;
    }
  }
  CloseServiceHandle(scm);
}

}  // namespace

struct WinFireflyBackend::Impl {
  std::function<void()> on_toggle;
  std::atomic<bool> active{false};
  std::atomic<bool> led_ok{false};
  bool dnd_backed_up = false;

  std::mutex dnd_mutex;
  std::condition_variable dnd_cv;
  std::atomic<bool> dnd_stop{true};
  bool dnd_target = false;
  bool dnd_pending = false;
  std::thread dnd_thread;

  void dnd_worker() {
    while (true) {
      bool target;
      {
        std::unique_lock lk(dnd_mutex);
        dnd_cv.wait(lk, [this] { return dnd_pending || dnd_stop.load(); });
        if (dnd_stop.load() && !dnd_pending) break;
        target = dnd_target;
        dnd_pending = false;
      }
      // Write DND state via registry — this is blocking I/O
      HKEY key = nullptr;
      if (RegOpenKeyExW(HKEY_CURRENT_USER, DndRegPath().c_str(), 0, KEY_READ | KEY_WRITE, &key) ==
          ERROR_SUCCESS) {
        DWORD size = 0;
        RegQueryValueExW(key, L"Data", nullptr, nullptr, nullptr, &size);
        if (size >= 0x20) {
          std::vector<BYTE> data(size);
          RegQueryValueExW(key, L"Data", nullptr, nullptr, data.data(), &size);

          // Swap the profile string in the blob
          std::wstring blob(reinterpret_cast<const wchar_t*>(data.data() + 0x1a),
                            (size - 0x1a) / sizeof(wchar_t));
          const wchar_t* from_str = target ? L"Microsoft.QuietHoursProfile.Unrestricted"
                                           : L"Microsoft.QuietHoursProfile.PriorityOnly";
          const wchar_t* to_str = target ? L"Microsoft.QuietHoursProfile.PriorityOnly"
                                         : L"Microsoft.QuietHoursProfile.Unrestricted";
          auto pos = blob.find(from_str);
          if (pos != std::wstring::npos) {
            blob.replace(pos, wcslen(from_str), to_str);
            std::vector<BYTE> new_data(data.begin(), data.begin() + 0x1a);
            const auto* ws = reinterpret_cast<const BYTE*>(blob.data());
            new_data.insert(new_data.end(), ws, ws + blob.size() * sizeof(wchar_t));
            RegSetValueExW(key, L"Data", 0, REG_BINARY, new_data.data(),
                           static_cast<DWORD>(new_data.size()));
          }
        }
        RegCloseKey(key);
      }
      RestartWpnService();
    }
  }
};

WinFireflyBackend::WinFireflyBackend() = default;

WinFireflyBackend::~WinFireflyBackend() { stop(); }

FireflyCaps WinFireflyBackend::caps() const {
  FireflyCaps c{};
  c.can_intercept_caps = true;
  c.can_drive_led = true;
  c.can_set_dnd = true;
  c.can_read_dnd = true;
  return c;
}

bool WinFireflyBackend::start(std::function<void()> on_toggle) {
  if (impl_) return true;
  impl_ = new Impl();
  impl_->on_toggle = std::move(on_toggle);

  RestoreDndBlob();
  BackupDndBlob();
  impl_->dnd_backed_up = true;

  impl_->dnd_stop.store(false);
  impl_->dnd_thread = std::thread([this] { impl_->dnd_worker(); });

  g_caps_hook = SetWindowsHookExW(WH_KEYBOARD_LL, CapsHookProc, GetModuleHandleW(nullptr), 0);
  return g_caps_hook != nullptr;
}

void WinFireflyBackend::stop() {
  if (!impl_) return;

  if (g_caps_hook) {
    UnhookWindowsHookEx(g_caps_hook);
    g_caps_hook = nullptr;
  }
  g_hook_target = nullptr;

  {
    std::lock_guard lk(impl_->dnd_mutex);
    impl_->dnd_stop.store(true);
    impl_->dnd_cv.notify_one();
  }
  if (impl_->dnd_thread.joinable()) impl_->dnd_thread.join();

  if (impl_->active.load()) {
    set_led(false);
  }
  if (impl_->dnd_backed_up) {
    RestoreDndBlob();
    RestartWpnService();
  }

  delete impl_;
  impl_ = nullptr;
}

void WinFireflyBackend::set_led(bool on) {
  if (!impl_) return;
  impl_->led_ok.store(SetCapsLockLed(on));
}

void WinFireflyBackend::set_dnd(bool on) {
  if (!impl_) return;
  std::lock_guard lk(impl_->dnd_mutex);
  impl_->dnd_target = on;
  impl_->dnd_pending = true;
  impl_->dnd_cv.notify_one();
}

bool WinFireflyBackend::read_dnd(bool& out) {
  return ReadDndState(out);
}

bool WinFireflyBackend::is_active() const {
  return impl_ && impl_->active.load();
}

void WinFireflyBackend::handle_toggle() {
  if (!impl_) return;
  bool cur = impl_->active.load();
  bool next = !cur;
  impl_->active.store(next);
  set_led(next);
  set_dnd(next);
  if (impl_->on_toggle) impl_->on_toggle();
}

void WinFireflyBackend::set_target_hwnd(HWND hwnd) {
  g_hook_target = hwnd;
}

}  // namespace imeaura
