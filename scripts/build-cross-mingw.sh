#!/bin/sh
set -eu

project_dir=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
build_dir=${1:-"$project_dir/build-windows-x64"}
cppwinrt_dir=${CPPWINRT_INCLUDE_DIR:?Set CPPWINRT_INCLUDE_DIR to the generated C++/WinRT projection directory.}
sdk_um_dir=${WINDOWS_SDK_UM_INCLUDE_DIR:?Set WINDOWS_SDK_UM_INCLUDE_DIR to the Windows SDK um include directory.}

cmake -S "$project_dir" -B "$build_dir" -G Ninja \
  -DCMAKE_TOOLCHAIN_FILE="$project_dir/scripts/mingw-x64-toolchain.cmake" \
  -DCMAKE_BUILD_TYPE=Release \
  -DBUILD_TESTING=OFF \
  -DCPPWINRT_INCLUDE_DIR="$cppwinrt_dir" \
  -DWINDOWS_SDK_UM_INCLUDE_DIR="$sdk_um_dir"
cmake --build "$build_dir" --parallel
