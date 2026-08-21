"""BK7258 orchestration over the official OpenVela build entry."""

from __future__ import annotations

import hashlib
import os
import re
import shutil
import stat
import subprocess
import tempfile
from dataclasses import dataclass
from pathlib import Path

from _lib import image as image_domain
from _lib import layout as layout_domain
from _lib import sdk as sdk_domain
from _lib import toolchain as toolchain_domain
from _lib import trust as trust_domain


class BuildError(RuntimeError):
    """Explicit build inputs or an official build stage failed."""


PROFILE_RE = re.compile(r"^([A-Z][A-Z0-9_]*)=(.*)$")
REQUIRED_PROFILE_FIELDS = frozenset(
    {"SCHEMA", "BOARD", "ROLE", "CLASS", "COMPAT", "SDK"}
)
BOARD_ROOT = Path("boards/bk7258")
CHIP_ROOT = Path("chips/bk7258")
@dataclass(frozen=True)
class ConfigProfile:
    root: Path
    board: str
    role: str
    compatibility: str
    sdk_profile: str


@dataclass(frozen=True)
class Toolchain:
    root: Path
    binary_dir: Path
    revision: str
    make: Path


@dataclass(frozen=True)
class EarlyRoute:
    swd_enable: int
    swd_pin_group: int
    swd_target: int
    swd_boot_hold: int
    console_uart: int
    console_baud: int
    console_data_bits: int
    console_parity: int
    console_stop_bits: int
    uart2_pin_group: int


@dataclass(frozen=True)
class RoleBuild:
    role: str
    config: ConfigProfile
    build_config_root: Path
    output_root: Path
    binary_root: Path
    elf: Path
    binary: Path
    map_file: Path
    dotconfig: Path
    generated_layout: layout_domain.GeneratedLayout
    seed_defconfig_sha256: str
    resolved_config_sha256: str


@dataclass(frozen=True)
class BootBuild:
    root: Path
    elf: Path
    binary: Path
    map_file: Path
    config_header: Path


@dataclass(frozen=True)
class Bl2Build:
    root: Path
    elf: Path
    binary: Path
    map_file: Path
    config_header: Path
    copy_size: int


@dataclass(frozen=True)
class BuiltArtifact:
    name: str
    path: Path
    size: int
    sha256: str


@dataclass(frozen=True)
class BuildResult:
    partition_identity: str
    cp: RoleBuild
    ap: RoleBuild
    bl1: BootBuild
    bl2: Bl2Build | None
    artifacts: tuple[BuiltArtifact, ...]
    preserved_external: tuple[str, ...]


def _regular(path: Path, label: str) -> Path:
    path = path.absolute()
    try:
        mode = path.lstat().st_mode
    except OSError as error:
        raise BuildError(f"missing {label}: {path}") from error
    if stat.S_ISLNK(mode) or not stat.S_ISREG(mode):
        raise BuildError(f"{label} must be a regular non-symlink file: {path}")
    return path.resolve(strict=True)


def _directory(path: Path, label: str) -> Path:
    path = path.absolute()
    try:
        mode = path.lstat().st_mode
    except OSError as error:
        raise BuildError(f"missing {label}: {path}") from error
    if stat.S_ISLNK(mode) or not stat.S_ISDIR(mode):
        raise BuildError(f"{label} must be a real directory: {path}")
    return path.resolve(strict=True)


def _official_entry(path: Path, workspace: Path) -> Path:
    try:
        resolved = path.resolve(strict=True)
        resolved.relative_to(workspace)
    except (OSError, ValueError) as error:
        raise BuildError(f"official build link escapes the workspace: {path}") from error
    if not resolved.is_file() or not os.access(resolved, os.X_OK):
        raise BuildError(f"official build entry is not executable: {resolved}")
    return resolved


def config_profile(repository: Path, path: Path, expected_role: str) -> ConfigProfile:
    root = _directory(path, f"{expected_role} config directory")
    allowed_root = (repository / BOARD_ROOT).resolve(strict=True)
    try:
        root.relative_to(allowed_root)
    except ValueError as error:
        raise BuildError(f"{expected_role} config must be under {allowed_root}") from error
    if root.parent.name != "configs" or root.parent.parent.name == "common":
        raise BuildError(f"{expected_role} config must be owned by one physical board")
    _regular(root / "defconfig", f"{expected_role} defconfig")
    profile_path = _regular(root / "profile.conf", f"{expected_role} profile")
    values: dict[str, str] = {}
    for number, raw in enumerate(profile_path.read_text(encoding="utf-8").splitlines(), 1):
        line = raw.strip()
        if not line or line.startswith("#"):
            continue
        match = PROFILE_RE.fullmatch(line)
        if match is None or not match.group(1).startswith("BK7258_PROFILE_"):
            raise BuildError(f"malformed profile line: {profile_path}:{number}")
        key = match.group(1).removeprefix("BK7258_PROFILE_")
        if key in values:
            raise BuildError(f"duplicate profile field: {key}")
        values[key] = match.group(2)
    if set(values) != REQUIRED_PROFILE_FIELDS:
        missing = sorted(REQUIRED_PROFILE_FIELDS - values.keys())
        extra = sorted(values.keys() - REQUIRED_PROFILE_FIELDS)
        raise BuildError(f"profile fields mismatch: missing={missing} extra={extra}")
    if values["SCHEMA"] != "1" or values["ROLE"] != expected_role:
        raise BuildError(f"config profile role/schema mismatch: {profile_path}")
    if values["CLASS"] != "runnable":
        raise BuildError(f"build config is not runnable: {profile_path}")
    sdk = sdk_domain.profile(repository, values["SDK"])
    if sdk.role != expected_role:
        raise BuildError(f"SDK profile role mismatch: {values['SDK']}")
    return ConfigProfile(
        root=root,
        board=values["BOARD"],
        role=values["ROLE"],
        compatibility=values["COMPAT"],
        sdk_profile=values["SDK"],
    )


def _run(command: list[str], label: str, *, cwd: Path,
         environment: dict[str, str]) -> None:
    try:
        result = subprocess.run(command, cwd=cwd, env=environment, check=False)
    except OSError as error:
        raise BuildError(f"cannot run {label}: {command[0]}") from error
    if result.returncode != 0:
        raise BuildError(f"{label} failed with exit status {result.returncode}")


def _unique(root: Path, names: tuple[str, ...], label: str) -> Path:
    matches = []
    for name in names:
        matches.extend(path for path in root.rglob(name) if path.is_file() and not path.is_symlink())
    unique = sorted(set(path.resolve() for path in matches))
    if len(unique) != 1:
        raise BuildError(f"official build must produce one {label}: {unique}")
    return unique[0]


def _build_environment(toolchain: Toolchain) -> dict[str, str]:
    """Keep host discovery usable without accepting hidden build inputs."""

    environment: dict[str, str] = {
        "PATH": f"{toolchain.binary_dir}:{os.environ.get('PATH', '/usr/bin:/bin')}",
        "CMAKE_PROGRAM_PATH": str(toolchain.binary_dir),
        "LANG": "C.UTF-8",
        "LC_ALL": "C.UTF-8",
        "PYTHONDONTWRITEBYTECODE": "1",
    }
    for name in ("TMPDIR", "TMP", "TEMP"):
        value = os.environ.get(name)
        if value:
            environment[name] = value
    return environment


def _atomic_text(path: Path, content: str) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    if path.is_symlink() or (path.exists() and not path.is_file()):
        raise BuildError(f"generated text target is not a regular file: {path}")
    descriptor, name = tempfile.mkstemp(prefix=f".{path.name}.", dir=path.parent)
    temporary = Path(name)
    try:
        with os.fdopen(descriptor, "w", encoding="utf-8", newline="\n") as stream:
            stream.write(content)
        os.replace(temporary, path)
    finally:
        temporary.unlink(missing_ok=True)


def _atomic_bytes(path: Path, content: bytes) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    if path.is_symlink() or (path.exists() and not path.is_file()):
        raise BuildError(f"generated binary target is not a regular file: {path}")
    descriptor, name = tempfile.mkstemp(prefix=f".{path.name}.", dir=path.parent)
    temporary = Path(name)
    try:
        with os.fdopen(descriptor, "wb") as stream:
            stream.write(content)
        os.replace(temporary, path)
    finally:
        temporary.unlink(missing_ok=True)


def _sha256_file(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def _dotconfig(path: Path) -> dict[str, str | None]:
    values: dict[str, str | None] = {}
    for raw in path.read_text(encoding="utf-8").splitlines():
        if raw.startswith("CONFIG_") and "=" in raw:
            key, value = raw.split("=", 1)
            values[key] = value
        else:
            match = re.fullmatch(r"# (CONFIG_[A-Z0-9_]+) is not set", raw)
            if match is not None:
                values[match.group(1)] = None
    return values


def _enabled(values: dict[str, str | None], name: str) -> bool:
    return values.get(name) == "y"


def _choice(values: dict[str, str | None], names: tuple[str, ...],
            label: str) -> str:
    selected = [name for name in names if _enabled(values, name)]
    if len(selected) != 1:
        raise BuildError(f"resolved CP config must select exactly one {label}: {selected}")
    return selected[0]


def _config_integer(values: dict[str, str | None], name: str) -> int:
    value = values.get(name)
    if value is None:
        raise BuildError(f"resolved CP config has no value for {name}")
    try:
        return int(value, 0)
    except ValueError as error:
        raise BuildError(f"resolved CP config integer is malformed: {name}={value}") from error


def _early_route(cp: RoleBuild) -> EarlyRoute:
    values = _dotconfig(cp.dotconfig)
    console_name = _choice(
        values,
        (
            "CONFIG_BK7258_CONSOLE_NONE",
            "CONFIG_BK7258_CONSOLE_RTT",
            "CONFIG_BK7258_CONSOLE_UART0",
            "CONFIG_BK7258_CONSOLE_UART1",
            "CONFIG_BK7258_CONSOLE_UART2",
        ),
        "early console",
    )
    console_map = {
        "CONFIG_BK7258_CONSOLE_UART0": 0,
        "CONFIG_BK7258_CONSOLE_UART1": 1,
        "CONFIG_BK7258_CONSOLE_UART2": 2,
        "CONFIG_BK7258_CONSOLE_NONE": 3,
        "CONFIG_BK7258_CONSOLE_RTT": 3,
    }
    console = console_map[console_name]
    if console < 3:
        prefix = f"CONFIG_BK7258_UART{console}"
        baud = _config_integer(values, f"{prefix}_BAUD")
        data_bits = _config_integer(values, f"{prefix}_DATA_BITS")
        parity = _config_integer(values, f"{prefix}_PARITY")
        stop_bits = _config_integer(values, f"{prefix}_STOP_BITS")
    else:
        baud, data_bits, parity, stop_bits = 1, 8, 0, 1

    swd = _enabled(values, "CONFIG_BK7258_SWD_DEBUG")
    if swd:
        swd_pins = _choice(
            values,
            ("CONFIG_BK7258_SWD_PINS_P20_P21", "CONFIG_BK7258_SWD_PINS_P0_P1"),
            "SWD pin group",
        )
        swd_target = _choice(
            values,
            (
                "CONFIG_BK7258_SWD_TARGET_CP",
                "CONFIG_BK7258_SWD_TARGET_AP0",
                "CONFIG_BK7258_SWD_TARGET_AP1",
            ),
            "SWD target",
        )
        swd_pin_group = 0 if swd_pins.endswith("P20_P21") else 1
        swd_target_value = {
            "CONFIG_BK7258_SWD_TARGET_CP": 0,
            "CONFIG_BK7258_SWD_TARGET_AP0": 1,
            "CONFIG_BK7258_SWD_TARGET_AP1": 2,
        }[swd_target]
    else:
        swd_pin_group = 0
        swd_target_value = 0
    uart2_group = 1 if _enabled(values, "CONFIG_BK7258_UART2_PINS_P40_P41") else 0
    return EarlyRoute(
        swd_enable=int(swd),
        swd_pin_group=swd_pin_group,
        swd_target=swd_target_value,
        swd_boot_hold=int(swd and _enabled(values, "CONFIG_BK7258_SWD_BOOT_HOLD")),
        console_uart=console,
        console_baud=baud,
        console_data_bits=data_bits,
        console_parity=parity,
        console_stop_bits=stop_bits,
        uart2_pin_group=uart2_group,
    )


def _config_header(guard: str, macros: dict[str, int]) -> str:
    lines = [
        "/* Generated from explicit build inputs; do not edit. */",
        f"#ifndef {guard}",
        f"#define {guard}",
        "",
    ]
    lines.extend(f"#define {name} {value}" for name, value in macros.items())
    lines.extend(("", "#endif", ""))
    return "\n".join(lines)


def _bl1_config(cp: RoleBuild, *, signed: bool, bl2_copy_size: int,
                rollback_floor: int) -> str:
    if rollback_floor < 0:
        raise BuildError("rollback floor must be non-negative")
    route = _early_route(cp)
    macros = {
        "BK7258_BL1_SIGNED": int(signed),
        "BK7258_BL1_USE_BL2": int(signed),
        "BK7258_BL1_MANIFEST_ENFORCE": int(signed),
        "BK7258_BL1_MANIFEST_RAW_PAGE": int(signed),
        "BK7258_BL1_BOOT_CONTROL_STAGING": 0,
        "BK7258_BL1_OTP_ROOT_POLICY": int(signed),
        "BK7258_BL1_TRUSTENGINE_PROBE": 0,
        "BK7258_BL2_COPY_SIZE": bl2_copy_size,
        "BK7258_BL1_MANIFEST_MIN_IMAGE_VERSION": rollback_floor,
        "BK7258_BL1_SWD_ENABLE": route.swd_enable,
        "BK7258_BL1_SWD_PIN_GROUP": route.swd_pin_group,
        "BK7258_BL1_SWD_TARGET": route.swd_target,
        "BK7258_BL1_SWD_BOOT_HOLD": route.swd_boot_hold,
        "BK7258_BL1_CONSOLE_UART": route.console_uart,
        "BK7258_BL1_CONSOLE_BAUD": route.console_baud,
        "BK7258_BL1_CONSOLE_DATA_BITS": route.console_data_bits,
        "BK7258_BL1_CONSOLE_PARITY": route.console_parity,
        "BK7258_BL1_CONSOLE_STOP_BITS": route.console_stop_bits,
        "BK7258_BL1_UART2_PIN_GROUP": route.uart2_pin_group,
    }
    return _config_header("__BK7258_BUILD_BL1_CONFIG_H", macros)


def _bl2_config(cp: RoleBuild, rollback_floor: int, copy_size: int) -> str:
    if rollback_floor < 0:
        raise BuildError("rollback floor must be non-negative")
    route = _early_route(cp)
    macros = {
        "BK7258_BL2_COPY_SIZE": copy_size,
        "BK7258_BL2_SECURITY_COUNTER_FLOOR": rollback_floor,
        "BK7258_BL2_SWD_ENABLE": route.swd_enable,
        "BK7258_BL2_SWD_PIN_GROUP": route.swd_pin_group,
        "BK7258_BL2_SWD_TARGET": route.swd_target,
        "BK7258_BL2_SWD_BOOT_HOLD": route.swd_boot_hold,
        "BK7258_BL2_CONSOLE_UART": route.console_uart,
        "BK7258_BL2_CONSOLE_BAUD": route.console_baud,
        "BK7258_BL2_CONSOLE_DATA_BITS": route.console_data_bits,
        "BK7258_BL2_CONSOLE_PARITY": route.console_parity,
        "BK7258_BL2_CONSOLE_STOP_BITS": route.console_stop_bits,
        "BK7258_BL2_UART2_PIN_GROUP": route.uart2_pin_group,
    }
    return _config_header("__BK7258_BUILD_BL2_CONFIG_H", macros)


def _verify_storage_topology(cp: RoleBuild, ap: RoleBuild,
                             selected_layout: layout_domain.Layout) -> None:
    symbols = {
        "CONFIG_BK7258_STORAGE_ONCHIP_PERSISTENT": "onchip-persistent",
        "CONFIG_BK7258_STORAGE_REMOVABLE_BLOCK": "removable-block",
        "CONFIG_BK7258_STORAGE_FIXED_BLOCK": "fixed-block",
    }
    selected = set()
    for role in (cp, ap):
        values = _dotconfig(role.dotconfig)
        selected.update(value for key, value in symbols.items() if _enabled(values, key))
    if selected != {selected_layout.storage_topology}:
        raise BuildError(
            f"resolved storage topology does not match the selected CSV: "
            f"config={sorted(selected)} csv={selected_layout.storage_topology}"
        )


def _capture(command: list[str], label: str, *, cwd: Path) -> str:
    try:
        result = subprocess.run(
            command,
            cwd=cwd,
            env={"PATH": os.environ.get("PATH", "/usr/bin:/bin"), "LC_ALL": "C"},
            check=False,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            text=True,
        )
    except OSError as error:
        raise BuildError(f"cannot run {label}: {command[0]}") from error
    if result.returncode != 0:
        raise BuildError(f"{label} failed with exit status {result.returncode}")
    return result.stdout.strip()


def resolve_toolchain(repository: Path, workspace: Path) -> Toolchain:
    """Resolve the content-addressed Arm GNU prebuilt without PATH fallback."""

    try:
        report = toolchain_domain.verify(repository)
    except toolchain_domain.ToolchainError as error:
        raise BuildError(str(error)) from error
    make = shutil.which("make", path=os.environ.get("PATH", "/usr/bin:/bin"))
    if make is None:
        raise BuildError("make is required for the project BL1")
    return Toolchain(
        report.root,
        report.binary_dir,
        report.archive_sha256,
        Path(make).resolve(strict=True),
    )


def toolchain_root(repository: Path) -> Path:
    repository = _directory(repository, "contest repository")
    workspace = _directory(repository.parent, "OpenVela workspace")
    return resolve_toolchain(repository, workspace).root


def _pair_root(workspace: Path, cp: ConfigProfile, ap: ConfigProfile,
               selected_layout: layout_domain.Layout) -> Path:
    return (
        workspace / "out/bk7258" / f"{cp.root.name}__{ap.root.name}"
        / selected_layout.identity
    )


def _build_config_root(workspace: Path, cp: ConfigProfile, ap: ConfigProfile,
                       selected: ConfigProfile,
                       selected_layout: layout_domain.Layout,
                       boot: str) -> Path:
    root = (
        _pair_root(workspace, cp, ap, selected_layout)
        / "configs" / boot / selected.role
    )
    if root.is_symlink():
        raise BuildError(f"generated build config root must not be a symlink: {root}")
    lines = [
        line for line in (selected.root / "defconfig").read_text(encoding="utf-8").splitlines()
        if not line.startswith("CONFIG_BK7258_MCUBOOT_IMAGE=")
        and line != "# CONFIG_BK7258_MCUBOOT_IMAGE is not set"
    ]
    boot_setting = (
        "CONFIG_BK7258_MCUBOOT_IMAGE=y"
        if boot == "mcuboot"
        else "# CONFIG_BK7258_MCUBOOT_IMAGE is not set"
    )
    lines.extend(("", boot_setting))
    _atomic_text(root / "defconfig", "\n".join(lines) + "\n")
    selector = f"CONFIG_BK7258_BOARD_{selected.board.upper()}"
    if f"{selector}=y" not in lines:
        raise BuildError(
            f"{selected.role} profile does not select its physical board: {selector}"
        )
    make_defs = _regular(
        selected.root.parents[2] / "common/scripts/Make.defs",
        "BK7258 common board Make.defs",
    )
    _atomic_text(
        root / "Make.defs",
        f"BK7258_EXPECTED_BOARD_SELECTOR := {selector}\n"
        + make_defs.read_text(encoding="utf-8"),
    )
    return root


def _role_build(repository: Path, workspace: Path, official_build: Path,
                config: ConfigProfile, build_config_root: Path, output_name: str,
                selected_layout: layout_domain.Layout, toolchain: Toolchain,
                jobs: int, clean: bool,
                catalog_public_source: Path | None = None) -> RoleBuild:
    sdk_report = sdk_domain.verify(repository, config.sdk_profile)
    build_config_root = _directory(build_config_root, f"{config.role} build config")
    _regular(build_config_root / "defconfig", f"{config.role} build defconfig")
    output_root = workspace / "out/bk7258" / output_name
    binary_root = output_root / "cmake"
    generated = layout_domain.emit(selected_layout, output_root / "generated")

    environment = _build_environment(toolchain)
    environment.update(
        {
            "BK7258_SDK_DIR": str(sdk_report.bundle),
            "BK7258_TOOLCHAIN_BIN": str(toolchain.binary_dir),
            "BK7258_PARTITION_CSV": str(selected_layout.source),
            "BK7258_PARTITION_HEADER": str(generated.header),
            "BK7258_PARTITION_LINKER": str(generated.linker),
            "BK7258_PARTITION_SDK_CSV": str(generated.sdk_csv),
            "BK7258_PARTITION_ID": selected_layout.identity,
            "BK7258_PARTITION_SHA256": selected_layout.sha256,
        }
    )
    if catalog_public_source is not None:
        environment["BK7258_OTA_CATALOG_PUBLIC_SOURCE"] = str(
            _regular(catalog_public_source, "OTA catalog public key source")
        )
    ccache_root = output_root / "ccache"
    ccache_temp = ccache_root / "tmp"
    ccache_temp.mkdir(parents=True, exist_ok=True)
    environment["CCACHE_DIR"] = str(ccache_root)
    environment["CCACHE_TEMPDIR"] = str(ccache_temp)
    try:
        config_argument = build_config_root.relative_to(workspace).as_posix()
    except ValueError as error:
        raise BuildError("build config is outside the OpenVela workspace") from error
    if clean:
        _run([str(official_build), config_argument, "distclean"],
             f"official {config.role} clean",
             cwd=workspace, environment=environment)
        _run(
            [str(official_build), config_argument, "--cmake", "-b",
             str(binary_root), "distclean"],
            f"official {config.role} CMake clean",
            cwd=workspace,
            environment=environment,
        )
    base = [
        str(official_build), config_argument, "--cmake",
        "-b", str(binary_root),
    ]
    _run(base + [f"-j{jobs}"], f"official {config.role} build",
         cwd=workspace, environment=environment)
    dotconfig = _unique(binary_root, (".config",), f"{config.role} .config")
    return RoleBuild(
        role=config.role,
        config=config,
        build_config_root=build_config_root,
        output_root=output_root,
        binary_root=binary_root,
        elf=_unique(binary_root, ("nuttx", "nuttx.elf"), f"{config.role} ELF"),
        binary=_unique(binary_root, ("nuttx.bin",), f"{config.role} raw binary"),
        map_file=_unique(binary_root, ("nuttx.map",), f"{config.role} map"),
        dotconfig=dotconfig,
        generated_layout=generated,
        seed_defconfig_sha256=_sha256_file(build_config_root / "defconfig"),
        resolved_config_sha256=_sha256_file(dotconfig),
    )


def _common_root(workspace: Path, cp: RoleBuild, ap: RoleBuild,
                 selected_layout: layout_domain.Layout) -> Path:
    return (
        workspace / "out/bk7258"
        / f"{cp.config.root.name}__{ap.config.root.name}"
        / selected_layout.identity
    )


def _build_bl2(repository: Path, workspace: Path, cp: RoleBuild, ap: RoleBuild,
               selected_layout: layout_domain.Layout, toolchain: Toolchain,
               key_source: Path, rollback_floor: int, *, clean: bool) -> Bl2Build:
    root = _common_root(workspace, cp, ap, selected_layout) / "bl2"
    if root.is_symlink():
        raise BuildError(f"BL2 output root must not be a symlink: {root}")
    root.mkdir(parents=True, exist_ok=True)
    config_header = root / "bk7258_bl2_config.h"
    initial_copy_size = selected_layout.logical_size(
        selected_layout.artifact("bl2_a")
    )
    _atomic_text(
        config_header, _bl2_config(cp, rollback_floor, initial_copy_size)
    )
    makefile = _regular(
        repository / CHIP_ROOT / "bootloader/bl2/Makefile",
        "project BL2 Makefile",
    )
    mcuboot_root = _directory(
        workspace / "apps/boot/mcuboot/mcuboot", "pinned MCUboot source"
    )
    environment = _build_environment(toolchain)
    command = [
        str(toolchain.make), "-f", str(makefile),
        f"OUT={root}",
        f"TOOLCHAIN={toolchain.binary_dir}",
        f"PARTITION_HEADER={cp.generated_layout.header}",
        f"CONFIG_HEADER={config_header}",
        f"MCUBOOT_ROOT={mcuboot_root}",
        f"KEY_SOURCE={_regular(key_source, 'build-local MCUboot public key source')}",
    ]
    if clean:
        _run(command + ["clean"], "project BL2 clean",
             cwd=repository, environment=environment)
    _run(command + ["all"], "project BL2 build",
         cwd=repository, environment=environment)
    binary = _regular(root / "bl2.bin", "project BL2 raw binary")
    copy_size = (binary.stat().st_size + 31) // 32 * 32
    capacity = selected_layout.logical_size(selected_layout.artifact("bl2_a"))
    if copy_size <= 0 or copy_size > capacity:
        raise BuildError(
            f"project BL2 exceeds its selected execution capacity: {copy_size} > {capacity}"
        )
    if copy_size != initial_copy_size:
        _atomic_text(config_header, _bl2_config(cp, rollback_floor, copy_size))
        _run(command + ["all"], "project BL2 final-size rebuild",
             cwd=repository, environment=environment)
        binary = _regular(root / "bl2.bin", "project BL2 raw binary")
        rebuilt_copy_size = (binary.stat().st_size + 31) // 32 * 32
        if rebuilt_copy_size != copy_size:
            raise BuildError(
                f"BL2 size is not stable after binding its copy contract: "
                f"{copy_size} -> {rebuilt_copy_size}"
            )
    elf = _regular(root / "bl2.elf", "project BL2 ELF")
    try:
        trust_domain.validate_bl2_vector(
            binary, elf, toolchain.binary_dir / "arm-none-eabi-nm"
        )
    except trust_domain.TrustError as error:
        raise BuildError(f"project BL2 vector contract failed: {error}") from error
    return Bl2Build(
        root=root,
        elf=elf,
        binary=binary,
        map_file=_regular(root / "bl2.map", "project BL2 map"),
        config_header=_regular(config_header, "project BL2 config"),
        copy_size=copy_size,
    )


def _build_bl1(repository: Path, workspace: Path, cp: RoleBuild, ap: RoleBuild,
               selected_layout: layout_domain.Layout, toolchain: Toolchain, *,
               signed: bool, bl2_copy_size: int, rollback_floor: int,
               key_source: Path | None, clean: bool) -> BootBuild:
    root = _common_root(workspace, cp, ap, selected_layout) / "bl1"
    if root.is_symlink():
        raise BuildError(f"BL1 output root must not be a symlink: {root}")
    root.mkdir(parents=True, exist_ok=True)
    config_header = root / "bk7258_bl1_config.h"
    _atomic_text(
        config_header,
        _bl1_config(
            cp, signed=signed, bl2_copy_size=bl2_copy_size,
            rollback_floor=rollback_floor,
        ),
    )
    makefile = _regular(
        repository / CHIP_ROOT / "bootloader/Makefile", "project BL1 Makefile"
    )
    environment = _build_environment(toolchain)
    command = [
        str(toolchain.make), "-f", str(makefile),
        f"MODE={'mcuboot' if signed else 'direct'}",
        f"OUT={root}",
        f"TOOLCHAIN={toolchain.binary_dir}",
        f"PARTITION_HEADER={cp.generated_layout.header}",
        f"CONFIG_HEADER={config_header}",
    ]
    if signed:
        if key_source is None:
            raise BuildError("signed BL1 requires a build-local public key source")
        command.extend(
            [
                f"NUTTX_ROOT={workspace / 'nuttx'}",
                f"BL1_KEY_SOURCE={key_source}",
            ]
        )
    if clean:
        _run(command + ["clean"], "project BL1 clean",
             cwd=repository, environment=environment)
    _run(command + ["all"], "project BL1 build",
         cwd=repository, environment=environment)
    return BootBuild(
        root=root,
        elf=_regular(root / "bl.elf", "project BL1 ELF"),
        binary=_regular(root / "bl.bin", "project BL1 raw binary"),
        map_file=_regular(root / "bl.map", "project BL1 map"),
        config_header=_regular(config_header, "project BL1 config"),
    )


def _finalize_direct_images(workspace: Path, cp: RoleBuild, ap: RoleBuild,
                            bl1: BootBuild,
                            selected_layout: layout_domain.Layout
                            ) -> tuple[tuple[BuiltArtifact, ...], tuple[str, ...]]:
    raw = image_domain.read_artifacts(
        {"boot": bl1.binary, "cp": cp.binary, "ap": ap.binary}
    )
    raw["pair"] = image_domain.pair(selected_layout, raw["cp"], raw["ap"])
    preserved_external = tuple(sorted(
        item.artifact for item in selected_layout.partitions
        if item.policy == "external" and item.artifact is not None
    ))
    image_set = image_domain.finalize(
        selected_layout, raw, preserved_external=preserved_external
    )
    root = _common_root(workspace, cp, ap, selected_layout) / "images"
    if root.is_symlink():
        raise BuildError(f"final image root must not be a symlink: {root}")
    root.mkdir(parents=True, exist_ok=True)
    result = []
    for row in image_set.writes:
        path = root / f"{row.artifact}.bin"
        _atomic_bytes(path, row.data)
        result.append(
            BuiltArtifact(
                name=row.artifact,
                path=path,
                size=len(row.data),
                sha256=hashlib.sha256(row.data).hexdigest(),
            )
        )
    image_domain.finalized(
        selected_layout,
        image_domain.read_artifacts({row.name: row.path for row in result}),
        preserved_external=preserved_external,
    )
    return tuple(result), preserved_external


def build(repository: Path, cp_config: Path, ap_config: Path, partition: Path,
          *, boot: str, bl1_public_key: Path | None,
          mcuboot_public_key: Path | None, openssl: Path | None,
          rollback_floor: int | None, jobs: int, clean: bool) -> BuildResult:
    """Build CP then AP through the official OpenVela out-of-tree entry."""

    if jobs <= 0:
        raise BuildError("jobs must be positive")
    repository = _directory(repository, "contest repository")
    workspace = _directory(repository.parent, "OpenVela workspace")
    official_build = _official_entry(workspace / "build.sh", workspace)
    toolchain = resolve_toolchain(repository, workspace)
    cp = config_profile(repository, cp_config, "cp")
    ap = config_profile(repository, ap_config, "ap")
    if (cp.board, cp.compatibility) != (ap.board, ap.compatibility):
        raise BuildError("CP/AP config profiles are not a compatible pair")
    if boot not in {"direct", "mcuboot"}:
        raise BuildError("boot must be direct or mcuboot")
    key_inputs = (bl1_public_key, mcuboot_public_key, openssl, rollback_floor)
    if boot == "direct" and any(value is not None for value in key_inputs):
        raise BuildError("direct build must not receive signing or rollback inputs")
    if boot == "mcuboot" and any(value is None for value in key_inputs):
        raise BuildError(
            "mcuboot build requires BL1/MCUboot public keys, OpenSSL and rollback floor"
        )
    selected_layout = layout_domain.load(partition)
    public_sources = None
    if boot == "mcuboot":
        assert bl1_public_key is not None
        assert mcuboot_public_key is not None
        assert openssl is not None
        public_sources = trust_domain.write_public_sources(
            bl1_public_key=bl1_public_key,
            mcuboot_public_key=mcuboot_public_key,
            openssl=openssl,
            output=_pair_root(workspace, cp, ap, selected_layout) / "trust",
        )
    cp_build_config = _build_config_root(
        workspace, cp, ap, cp, selected_layout, boot
    )
    ap_build_config = _build_config_root(
        workspace, cp, ap, ap, selected_layout, boot
    )
    cp_result = _role_build(
        repository, workspace, official_build, cp, cp_build_config,
        f"{cp.root.name}-{boot}", selected_layout, toolchain, jobs, clean
    )
    ap_result = _role_build(
        repository, workspace, official_build, ap, ap_build_config,
        f"{ap.root.name}-{boot}", selected_layout, toolchain, jobs, clean,
        public_sources.catalog_source if public_sources is not None else None,
    )
    _verify_storage_topology(cp_result, ap_result, selected_layout)
    if boot == "direct":
        bl2 = None
        bl1 = _build_bl1(
            repository, workspace, cp_result, ap_result, selected_layout,
            toolchain, signed=False, bl2_copy_size=32,
            rollback_floor=0, key_source=None, clean=clean,
        )
        artifacts, preserved_external = _finalize_direct_images(
            workspace, cp_result, ap_result, bl1, selected_layout
        )
    else:
        assert bl1_public_key is not None
        assert mcuboot_public_key is not None
        assert openssl is not None
        assert rollback_floor is not None
        assert public_sources is not None
        bl2 = _build_bl2(
            repository, workspace, cp_result, ap_result, selected_layout,
            toolchain, public_sources.mcuboot_source, rollback_floor,
            clean=clean,
        )
        bl1 = _build_bl1(
            repository, workspace, cp_result, ap_result, selected_layout,
            toolchain, signed=True, bl2_copy_size=bl2.copy_size,
            rollback_floor=rollback_floor,
            key_source=public_sources.bl1_source, clean=clean,
        )
        artifacts = ()
        preserved_external = ()
    return BuildResult(
        selected_layout.identity,
        cp_result,
        ap_result,
        bl1,
        bl2,
        artifacts,
        preserved_external,
    )
