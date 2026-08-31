#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0

"""Negative host tests for the BK7258 source-layer gate."""

from __future__ import annotations

import hashlib
import json
import sys
import tempfile
from pathlib import Path


REPOSITORY = Path(__file__).resolve().parents[3]
sys.path.insert(0, str(REPOSITORY / "tools/bk7258"))

from _lib import layers  # noqa: E402


def _write(root: Path, relative: str, content: str) -> Path:
    path = root / relative
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(content, encoding="utf-8")
    return path


def _exceptions(root: Path, rows: list[dict[str, str]]) -> None:
    document = {"product_protocol_files": rows, "version": 1}
    _write(
        root,
        "tools/bk7258/layer_exceptions.json",
        json.dumps(document, sort_keys=True) + "\n",
    )


def _fixture(root: Path) -> None:
    _write(
        root,
        "chips/bk7258/Kconfig",
        "config BK7258_CP_OK\n"
        "\tbool\n"
        "\tdepends on ARCH_CHIP_BK7258 && !BK7258_AP_CORE\n\n"
        "menu \"AP wrappers\"\n"
        "\tdepends on BK7258_AP_CORE\n\n"
        "config BK7258_AP_OK\n"
        "\tbool\n"
        "\tdepends on BK7258_AP_CORE\n\n"
        "endmenu\n",
    )
    _write(
        root,
        "boards/bk7258/test/src/board.c",
        "#include <arch/chip/bk7258_gpio.h>\n"
        "/* #include <driver/gpio.h>; bk_gpio_init(); */\n"
        "int board_start(void) { return bk7258_gpio_initialize(); }\n",
    )
    _write(
        root,
        "chips/bk7258/common/chip.c",
        "int bk7258_chip_initialize(void) { return 0; }\n",
    )
    _write(root, "app/bk7258/app.c", "int main(void) { return 0; }\n")
    _exceptions(root, [])


def _codes(root: Path) -> set[str]:
    issues, _ = layers.audit(root)
    return {issue.code for issue in issues}


def test_clean_fixture() -> None:
    with tempfile.TemporaryDirectory(prefix="bk7258-layers-clean-") as temporary:
        root = Path(temporary)
        _fixture(root)
        issues, report = layers.audit(root)
        assert not issues, issues
        assert report.source_files == 3
        assert report.kconfig_symbols == 2


def test_all_boundary_failures() -> None:
    with tempfile.TemporaryDirectory(prefix="bk7258-layers-bad-") as temporary:
        root = Path(temporary)
        _fixture(root)
        _write(
            root,
            "boards/bk7258/test/src/board.c",
            "#include <sdkconfig.h>\n"
            "int board_start(void) { bk_err_t rc = bk_gpio_init(); "
            "putreg32(1, 0x44000000); "
            "return rc == BK_OK; }\n",
        )
        _write(
            root,
            "chips/bk7258/common/chip.c",
            "#include <arch/board/board.h>\n"
            "int f(void) { return BK7258_BOARD_USER_LED_GPIO; }\n",
        )
        _write(
            root,
            "chips/bk7258/ap/product.c",
            "int f(void) { BT_GATT_PRIMARY_SERVICE(1, 2); return 0; }\n",
        )
        _write(
            root,
            "chips/bk7258/include/bk7258_public.h",
            "#include <driver/gpio.h>\nint f(gpio_id_t id);\n",
        )
        _write(
            root,
            "app/bk7258/app.c",
            "int main(void) { return BK7258_BOARD_LCD_BUS; }\n",
        )
        _write(
            root,
            "chips/bk7258/Kconfig",
            "menu \"AP wrappers\"\n"
            "\tdepends on BK7258_AP_CORE\n\n"
            "config BK7258_CP_BAD\n"
            "\tbool\n"
            "\tdepends on !BK7258_AP_CORE\n\n"
            "endmenu\n",
        )
        expected = {
            "SDK_INCLUDE",
            "SDK_SYMBOL",
            "SDK_TYPE",
            "RAW_REGISTER",
            "PUBLIC_SDK_ABI",
            "CHIP_TO_BOARD",
            "APP_PHYSICAL_RESOURCE",
            "KCONFIG_ROLE_MENU",
            "PRODUCT_PROTOCOL_IN_CHIP",
        }
        found = _codes(root)
        assert expected <= found, expected - found


def test_legacy_exception_is_hash_bound() -> None:
    with tempfile.TemporaryDirectory(prefix="bk7258-layers-legacy-") as temporary:
        root = Path(temporary)
        _fixture(root)
        relative = "chips/bk7258/ap/legacy.c"
        content = "int f(void) { BT_GATT_PRIMARY_SERVICE(1, 2); return 0; }\n"
        selected = _write(root, relative, content)
        digest = hashlib.sha256(selected.read_bytes()).hexdigest()
        _exceptions(root, [{
            "path": relative,
            "reason": "host-test legacy protocol",
            "sha256": digest,
        }])
        assert "PRODUCT_PROTOCOL_IN_CHIP" not in _codes(root)
        assert "EXCEPTION_HASH" not in _codes(root)

        selected.write_text(content + "/* changed */\n", encoding="utf-8")
        assert "EXCEPTION_HASH" in _codes(root)


def main() -> int:
    test_clean_fixture()
    test_all_boundary_failures()
    test_legacy_exception_is_hash_bound()
    print("BK7258_LAYER_TEST_PASS")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
