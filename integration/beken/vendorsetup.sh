#!/usr/bin/env bash
# SPDX-License-Identifier: Apache-2.0

# OpenVela sources vendor setup files after adding its global prebuilts.
# NuttX's ARM CMake toolchain probes a bare arm-none-eabi-gcc before CMake
# resolves the compiler through CMAKE_PROGRAM_PATH, so the selected compiler
# must also lead PATH during configure.

if [ -n "${BK7258_TOOLCHAIN_BIN:-}" ]; then
  bk7258_prebuilt_root=$(readlink -f "${T}/vendor/beken/prebuilt")
  bk7258_toolchain_bin=$(readlink -f "${BK7258_TOOLCHAIN_BIN}")
  case "${bk7258_toolchain_bin}" in
    "${bk7258_prebuilt_root}"/*/bin)
      if [ ! -x "${bk7258_toolchain_bin}/arm-none-eabi-gcc" ]; then
        echo "BK7258 locked compiler is missing" >&2
        return 1
      fi
      export PATH="${bk7258_toolchain_bin}:${PATH}"
      ;;
    *)
      echo "BK7258 toolchain escapes vendor/beken/prebuilt" >&2
      return 1
      ;;
  esac
  unset bk7258_prebuilt_root bk7258_toolchain_bin
fi
