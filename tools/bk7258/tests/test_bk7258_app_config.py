#!/usr/bin/env python3
"""Focused P1 checks: application registration is decoupled from driver symbols.

Every user-visible NSH command must be enabled by its own
``CONFIG_BK7258_APP_*`` symbol and must ``depends on`` the underlying
driver/feature capability.  CMake and Classic Make may register commands
only from the App symbols; legacy driver/test symbols must never appear in
the application registration files.
"""

from __future__ import annotations

import re
import unittest
from pathlib import Path


APP_ROOT = Path(__file__).resolve().parents[3] / "app" / "hello_app"

APP_SYMBOLS = (
    "BK7258_APP_HELLO",
    "BK7258_APP_BKVALIDATE",
    "BK7258_APP_APCTL",
    "BK7258_APP_RPMSG_TEST",
    "BK7258_APP_RPMSGFS_TEST",
    "BK7258_APP_BT_IPC_TEST",
    "BK7258_APP_WIFI",
    "BK7258_APP_PSRAM_TEST",
    "BK7258_APP_GPIO_TEST",
    "BK7258_APP_GPIO_IRQ_TEST",
    "BK7258_APP_IRQ_TIMER_TEST",
    "BK7258_APP_TIMER_SELFTEST",
)

# Driver/feature symbols that used to double as application enables.
LEGACY_ENABLE_SYMBOLS = (
    "LVX_USE_DEMO_CONTEST2026_135_HELLO_APP",
    "BK7258_BKVALIDATE",
    "BK7258_AP_CONTROL",
    "BK7258_RPMSG_TEST",
    "BK7258_RPMSGFS_TEST",
    "BK7258_BT_IPC_TEST",
    "BK7258_WIFI_VNET",
    "BK7258_PSRAM_TEST",
    "BK7258_GPIO_FOUNDATION_TEST",
    "BK7258_GPIO_IRQ_TEST",
    "BK7258_SDK_IRQ_TIMER_TEST",
    "BK7258_SDK_TIMER_SELFTEST",
)

# Required underlying capability per application.  The App symbol must
# carry a ``depends on`` containing the capability so an unsatisfied
# dependency makes the command invisible in menuconfig.
APP_REQUIRES = {
    "BK7258_APP_HELLO": ("SYSTEM_NSH",),
    "BK7258_APP_BKVALIDATE": ("SYSTEM_NSH",),
    "BK7258_APP_APCTL": ("BK7258_AP_CONTROL",),
    "BK7258_APP_RPMSG_TEST": ("BK7258_RPMSG_TEST",),
    "BK7258_APP_RPMSGFS_TEST": ("BK7258_RPMSGFS_TEST",),
    "BK7258_APP_BT_IPC_TEST": ("BK7258_BT_IPC_TEST",),
    "BK7258_APP_WIFI": ("BK7258_WIFI_VNET",),
    "BK7258_APP_PSRAM_TEST": ("BK7258_PSRAM_TEST",),
    "BK7258_APP_GPIO_TEST": ("BK7258_BOARD_HAS_USER_GPIO_BINDING",),
    "BK7258_APP_GPIO_IRQ_TEST": ("BK7258_SDK_IRQ_BRIDGE",
                                 "BK7258_BOARD_HAS_USER_GPIO_BINDING"),
    "BK7258_APP_IRQ_TIMER_TEST": ("BK7258_SDK_IRQ_BRIDGE",),
    "BK7258_APP_TIMER_SELFTEST": ("SYSTEM_NSH",),
}


class AppConfigDecouplingTest(unittest.TestCase):
    def setUp(self) -> None:
        self.kconfig = (APP_ROOT / "Kconfig").read_text(encoding="utf-8")
        self.cmake = (APP_ROOT / "CMakeLists.txt").read_text(encoding="utf-8")
        self.makefile = (APP_ROOT / "Makefile").read_text(encoding="utf-8")
        self.make_defs = (APP_ROOT / "Make.defs").read_text(encoding="utf-8")

    def test_every_command_has_full_app_kconfig(self) -> None:
        for symbol in APP_SYMBOLS:
            self.assertIsNotNone(
                re.search(rf"^config {symbol}\b", self.kconfig,
                          re.MULTILINE),
                msg=f"{symbol} enable")
            for suffix in ("PROGNAME", "PRIORITY", "STACKSIZE"):
                self.assertIsNotNone(
                    re.search(rf"^config {symbol}_{suffix}\b",
                              self.kconfig, re.MULTILINE),
                    msg=f"{symbol}_{suffix}")

    def test_app_dependencies_fail_closed(self) -> None:
        for symbol, requires in APP_REQUIRES.items():
            block = self._config_block(symbol)
            for requirement in requires:
                self.assertIn(
                    requirement, block,
                    msg=f"{symbol} must depend on {requirement}")

    def test_cmake_and_make_never_use_legacy_driver_enable_symbols(self) -> None:
        registration = "\n".join((self.cmake, self.makefile, self.make_defs))
        for legacy in LEGACY_ENABLE_SYMBOLS:
            self.assertNotIn(
                legacy, registration,
                msg=f"legacy app enable {legacy} must not register commands")

    def test_every_app_is_registered_by_cmake_and_make(self) -> None:
        for symbol in APP_SYMBOLS:
            self.assertIn(f"CONFIG_{symbol}", self.cmake,
                          msg=f"CMake registration for {symbol}")
            self.assertIn(f"$(CONFIG_{symbol})", self.makefile,
                          msg=f"Makefile registration for {symbol}")

    def test_make_defs_gates_hello_app_on_app_symbols_only(self) -> None:
        self.assertIn("CONFIGURED_APPS += "
                      "$(APPDIR)/packages/demos/contest2026_135_hello_app",
                      self.make_defs)
        for symbol in APP_SYMBOLS:
            self.assertIn(f"CONFIG_{symbol}", self.make_defs)

    def _config_block(self, symbol: str) -> str:
        match = re.search(
            rf"^config {symbol}\b.*?(?=^config |\Z)", self.kconfig,
            re.MULTILINE | re.DOTALL)
        self.assertIsNotNone(match, msg=f"missing Kconfig block {symbol}")
        return match.group(0)


if __name__ == "__main__":
    unittest.main()
