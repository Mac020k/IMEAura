#include "app/app.h"

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <fcntl.h>
#include <io.h>
#include <cstdio>
#endif

#include <cstring>
#include <iostream>

namespace {

imeaura::AppOptions parse_args(int argc, char** argv) {
  imeaura::AppOptions opts;
  for (int i = 1; i < argc; ++i) {
    if (std::strcmp(argv[i], "--probe") == 0) opts.probe_mode = true;
    if (std::strcmp(argv[i], "--json") == 0) opts.probe_json = true;
  }
  if (opts.probe_mode) opts.probe_json = true;
  return opts;
}

#ifdef _WIN32
void bind_handle(HANDLE hout) {
  if (!hout || hout == INVALID_HANDLE_VALUE) return;
  const int fd = _open_osfhandle(reinterpret_cast<intptr_t>(hout), _O_TEXT);
  if (fd < 0) return;
  FILE* fp = _fdopen(fd, "w");
  if (!fp) return;
  *stdout = *fp;
  setvbuf(stdout, nullptr, _IONBF, 0);
}

void bind_stdout_for_probe() {
  const HANDLE inherited = GetStdHandle(STD_OUTPUT_HANDLE);
  const DWORD type =
      (inherited && inherited != INVALID_HANDLE_VALUE) ? GetFileType(inherited) : FILE_TYPE_UNKNOWN;
  if (type == FILE_TYPE_PIPE || type == FILE_TYPE_DISK) {
    bind_handle(inherited);
    return;
  }
  if (AttachConsole(ATTACH_PARENT_PROCESS)) {
    FILE* fp = nullptr;
    freopen_s(&fp, "CONOUT$", "w", stdout);
    freopen_s(&fp, "CONOUT$", "w", stderr);
    std::ios::sync_with_stdio(true);
  }
}
#endif

}  // namespace

int main(int argc, char** argv) {
  const auto opts = parse_args(argc, argv);
#ifdef _WIN32
  if (opts.probe_mode) bind_stdout_for_probe();
#endif
  imeaura::App app(opts);
  const int code = app.run();
  std::cout.flush();
  return code;
}
