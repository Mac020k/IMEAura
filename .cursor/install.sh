#!/usr/bin/env bash
# Idempotent Cloud Agent bootstrap for IME Aura (Linux native C++20 build).
set -euo pipefail

# 1. System dependencies (mirrors .github/workflows/release.yml Linux job).
export DEBIAN_FRONTEND=noninteractive
sudo apt-get update
sudo apt-get install -y --no-install-recommends \
  ninja-build \
  pkg-config \
  libwayland-dev \
  wayland-protocols \
  libdbus-1-dev \
  libatspi2.0-dev \
  libgtk-4-dev

# 2. Configure with the linux-ninja preset.
#    Pin GCC explicitly: the VM's default `c++` alternative may point at Clang,
#    which cannot locate libstdc++ here. GCC matches the CI toolchain.
cmake --preset linux-ninja \
  -DCMAKE_C_COMPILER=gcc \
  -DCMAKE_CXX_COMPILER=g++

# 3. Build the app and unit tests.
cmake --build --preset linux-ninja --config Release
