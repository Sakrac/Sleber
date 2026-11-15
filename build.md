# Sleber Build Instructions

This document covers all currently supported build targets in this repository.

## Supported platforms and compilers

- Windows
  - MSVC (Visual Studio 2022), x64 and x86
  - MinGW-w64 GCC
  - LLVM/Clang toolchains that target Win32
- Linux
  - GCC
  - Clang

## Common requirements

- CMake 3.16+
- A C11-capable compiler

## Windows

### Option A: MSVC (Visual Studio 2022) with CMake Presets

Presets are defined in `CMakePresets.json`.

#### x64 Debug

```powershell
cmake --preset msvc-x64-debug
cmake --build --preset build-msvc-x64-debug
```

#### x86 Debug

```powershell
cmake --preset msvc-x86-debug
cmake --build --preset build-msvc-x86-debug
```

AddressSanitizer is enabled automatically for both MSVC Windows Debug presets.
MSVC Windows Release builds use link-time code generation.

#### x64 Release

```powershell
cmake --preset msvc-x64-release
cmake --build --preset build-msvc-x64-release
```

#### x86 Release

```powershell
cmake --preset msvc-x86-release
cmake --build --preset build-msvc-x86-release
```

Built executable location (preset builds):

- `build/msvc-*/<Configuration>/Sleber.exe`

### Option B: MinGW-w64 GCC (Ninja)

```powershell
cmake -S . -B build/mingw-gcc -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build/mingw-gcc
```

### Option C: Clang on Windows (Ninja)

```powershell
cmake -S . -B build/win-clang -G Ninja -DCMAKE_C_COMPILER=clang -DCMAKE_BUILD_TYPE=Release
cmake --build build/win-clang
```

Notes:

- The Windows build links against system libraries: `comdlg32`, `gdi32`, `ole32`, `user32`.
- No GTK dependency is required on Windows.

## Linux

Linux builds require GTK3 development packages and `pkg-config`.

### Debian/Ubuntu prerequisites

```bash
sudo apt update
sudo apt install -y build-essential cmake ninja-build pkg-config libgtk-3-dev
```

### Fedora prerequisites

```bash
sudo dnf install -y gcc cmake ninja-build pkgconf-pkg-config gtk3-devel
```

### Arch prerequisites

```bash
sudo pacman -S --needed base-devel cmake ninja pkgconf gtk3
```

### GCC build (Ninja)

```bash
cmake -S . -B build/linux-gcc -G Ninja -DCMAKE_C_COMPILER=gcc -DCMAKE_BUILD_TYPE=Release
cmake --build build/linux-gcc
```

### Clang build (Ninja)

```bash
cmake -S . -B build/linux-clang -G Ninja -DCMAKE_C_COMPILER=clang -DCMAKE_BUILD_TYPE=Release
cmake --build build/linux-clang
```

Built executable location:

- `build/linux-*/Sleber`

## Run

From the corresponding build directory output location:

- Windows: `Sleber.exe`
- Linux: `./Sleber`