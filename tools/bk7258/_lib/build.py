"""BK7258 orchestration over the official OpenVela build entry."""

from __future__ import annotations

import hashlib
import json
import os
import re
import shutil
import stat
import subprocess
import tempfile
from dataclasses import dataclass
from pathlib import Path, PurePosixPath

from _lib import image as image_domain
from _lib import layout as layout_domain
from _lib import sdk as sdk_domain
from _lib import toolchain as toolchain_domain
from _lib import trust as trust_domain


class BuildError(RuntimeError):
    """Explicit build inputs or an official build stage failed."""


BUILD_MANIFEST_FORMAT_V1 = "bk7258.build-manifest/1"
BUILD_MANIFEST_FORMAT = "bk7258.build-manifest/2"
DIGEST_RE = re.compile(r"^[0-9a-f]{64}$")


PROFILE_RE = re.compile(r"^([A-Z][A-Z0-9_]*)=(.*)$")
REQUIRED_PROFILE_FIELDS = frozenset(
    {"SCHEMA", "BOARD", "ROLE", "CLASS", "COMPAT", "SDK"}
)
REQUIRED_BOARD_FIELDS = frozenset(
    {"SCHEMA", "NAME", "CP_CONFIG", "AP_CONFIG", "PARTITION"}
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
class BoardPreset:
    board: str
    cp_config: Path
    ap_config: Path
    partition: Path


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
    build_identity: str
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
    manifest: Path


@dataclass(frozen=True)
class BuildManifest:
    source: Path
    format: str
    physical_board: str
    boot: str
    layout: layout_domain.Layout
    artifacts: dict[str, Path]
    finalized_artifacts: dict[str, Path]
    preserved_external: tuple[str, ...]
    elfs: dict[str, Path]
    sdk_profiles: tuple[str, ...]
    toolchain_sha256: str
    role_identities: dict[str, str]
    rollback_floor: int | None
    trust_fingerprints: dict[str, str]


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
    physical_board = root.parent.parent.name
    if values["BOARD"] != physical_board \
            or re.fullmatch(r"[a-z][a-z0-9_]*", values["BOARD"]) is None:
        raise BuildError(
            f"config profile board does not match its owner: {profile_path}"
        )
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


def board_preset(repository: Path, board: str) -> BoardPreset:
    """Load one physical board's maintained OpenVela build declaration."""

    if re.fullmatch(r"[a-z][a-z0-9_]*", board) is None:
        raise BuildError(f"invalid physical board name: {board!r}")

    boards_root = _directory(repository / BOARD_ROOT, "BK7258 board root")
    board_root = _directory(boards_root / board, f"{board} board root")
    try:
        board_root.relative_to(boards_root)
    except ValueError as error:
        raise BuildError(f"physical board escapes {boards_root}: {board!r}") from error

    descriptor = _regular(board_root / "openvela.conf", "board OpenVela declaration")
    values: dict[str, str] = {}
    for number, raw in enumerate(descriptor.read_text(encoding="utf-8").splitlines(), 1):
        line = raw.strip()
        if not line or line.startswith("#"):
            continue
        match = PROFILE_RE.fullmatch(line)
        if match is None or not match.group(1).startswith("BK7258_BOARD_"):
            raise BuildError(f"malformed board declaration line: {descriptor}:{number}")
        key = match.group(1).removeprefix("BK7258_BOARD_")
        if key in values:
            raise BuildError(f"duplicate board declaration field: {key}")
        values[key] = match.group(2)
    if set(values) != REQUIRED_BOARD_FIELDS:
        missing = sorted(REQUIRED_BOARD_FIELDS - values.keys())
        extra = sorted(values.keys() - REQUIRED_BOARD_FIELDS)
        raise BuildError(
            f"board declaration fields mismatch: missing={missing} extra={extra}"
        )
    if values["SCHEMA"] != "1" or values["NAME"] != board:
        raise BuildError(f"board declaration name/schema mismatch: {descriptor}")

    def repository_path(value: str, label: str) -> Path:
        if "\\" in value:
            raise BuildError(f"{label} must use a repository-relative POSIX path")
        relative = PurePosixPath(value)
        if relative.is_absolute() or not relative.parts \
                or any(part in {"", ".", ".."} for part in relative.parts):
            raise BuildError(f"invalid repository-relative {label}: {value!r}")
        path = repository.joinpath(*relative.parts)
        try:
            path.resolve(strict=True).relative_to(repository.resolve(strict=True))
        except (OSError, ValueError) as error:
            raise BuildError(f"{label} escapes the contest repository: {value!r}") \
                from error
        return path

    cp_config = repository_path(values["CP_CONFIG"], "CP config")
    ap_config = repository_path(values["AP_CONFIG"], "AP config")
    partition = repository_path(values["PARTITION"], "partition CSV")
    cp = config_profile(repository, cp_config, "cp")
    ap = config_profile(repository, ap_config, "ap")
    if cp.board != board or ap.board != board:
        raise BuildError(f"board declaration selects another physical board: {descriptor}")
    if cp.compatibility != ap.compatibility:
        raise BuildError(f"board declaration selects incompatible CP/AP configs: {descriptor}")

    partition_file = _regular(partition, "partition CSV")
    try:
        partition_file.relative_to(boards_root)
    except ValueError as error:
        raise BuildError(f"board partition is outside {boards_root}: {partition}") from error
    layout_domain.load(partition_file)
    return BoardPreset(
        board=board,
        cp_config=cp.root,
        ap_config=ap.root,
        partition=partition_file,
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


def _role_build_identity(workspace: Path, config: ConfigProfile,
                         build_config_root: Path,
                         selected_layout: layout_domain.Layout,
                         toolchain: Toolchain, sdk_tree_hash: str,
                         catalog_public_source: Path | None) -> str:
    """Hash every non-source input that CMake cannot safely rediscover."""

    try:
        config_root = build_config_root.relative_to(workspace).as_posix()
    except ValueError as error:
        raise BuildError("build config is outside the OpenVela workspace") from error
    catalog_sha256 = None
    if catalog_public_source is not None:
        catalog_sha256 = _sha256_file(
            _regular(catalog_public_source, "OTA catalog public key source")
        )
    payload = {
        "board": config.board,
        "build_config_root": config_root,
        "compatibility": config.compatibility,
        "layout_sha256": selected_layout.sha256,
        "ota_catalog_public_source_sha256": catalog_sha256,
        "profile_sha256": _sha256_file(config.root / "profile.conf"),
        "role": config.role,
        "schema": "bk7258-role-build/1",
        "sdk_profile": config.sdk_profile,
        "sdk_tree_sha256": sdk_tree_hash,
        "seed_defconfig_sha256": _sha256_file(build_config_root / "defconfig"),
        "toolchain_archive_sha256": toolchain.revision,
    }
    encoded = json.dumps(
        payload, sort_keys=True, separators=(",", ":")
    ).encode("utf-8")
    return f"bk7258-role-{hashlib.sha256(encoded).hexdigest()[:16]}"


def _role_output_root(workspace: Path, config: ConfigProfile,
                      build_config_root: Path,
                      selected_layout: layout_domain.Layout,
                      toolchain: Toolchain, sdk_tree_hash: str,
                      catalog_public_source: Path | None) -> tuple[Path, str]:
    pair_root = build_config_root.parent.parent.parent
    expected_root = (workspace / "out/bk7258").resolve()
    try:
        pair_root.resolve().relative_to(expected_root)
    except ValueError as error:
        raise BuildError("generated build config is outside out/bk7258") from error
    boot = build_config_root.parent.name
    if boot not in {"direct", "mcuboot"}:
        raise BuildError(f"generated build config has invalid boot mode: {boot}")
    identity = _role_build_identity(
        workspace, config, build_config_root, selected_layout, toolchain,
        sdk_tree_hash, catalog_public_source,
    )
    return pair_root / "roles" / boot / config.role / identity, identity


def _remove_output_tree(path: Path, workspace: Path) -> None:
    """Remove one generated output directory without following symlinks."""

    allowed_root = (workspace / "out/bk7258").resolve()
    resolved = path.resolve(strict=False)
    try:
        resolved.relative_to(allowed_root)
    except ValueError as error:
        raise BuildError(f"refusing to clean output outside {allowed_root}: {path}") from error
    if resolved == allowed_root:
        raise BuildError("refusing to clean the complete BK7258 output root")
    if path.is_symlink():
        raise BuildError(f"build output root must not be a symlink: {path}")
    if path.exists():
        if not path.is_dir():
            raise BuildError(f"build output root must be a directory: {path}")
        shutil.rmtree(path)


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
    if cp.board != ap.board:
        raise BuildError("CP/AP config profiles have different board owners")
    return (
        workspace / "out/bk7258" / cp.board
        / f"{cp.root.name}__{ap.root.name}"
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
                config: ConfigProfile, build_config_root: Path,
                selected_layout: layout_domain.Layout, toolchain: Toolchain,
                jobs: int, clean: bool,
                catalog_public_source: Path | None = None) -> RoleBuild:
    sdk_report = sdk_domain.verify(repository, config.sdk_profile)
    build_config_root = _directory(build_config_root, f"{config.role} build config")
    _regular(build_config_root / "defconfig", f"{config.role} build defconfig")
    output_root, build_identity = _role_output_root(
        workspace, config, build_config_root, selected_layout, toolchain,
        sdk_report.tree_hash, catalog_public_source,
    )
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
        config_relative = build_config_root.relative_to(workspace).as_posix()
    except ValueError as error:
        raise BuildError("build config is outside the OpenVela workspace") from error
    # The official build.sh CMake preflight removes the first three path
    # characters before checking defconfig.  Use the equivalent ".//"
    # relative prefix: both the unmodified argument consumed by lunch() and
    # the preflight argument after that removal resolve to config_relative.
    config_argument = f".//{config_relative}"
    if clean:
        _remove_output_tree(binary_root, workspace)
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
        build_identity=build_identity,
        seed_defconfig_sha256=_sha256_file(build_config_root / "defconfig"),
        resolved_config_sha256=_sha256_file(dotconfig),
    )


def _release_root(workspace: Path, cp: RoleBuild, ap: RoleBuild,
                  selected_layout: layout_domain.Layout, boot: str) -> Path:
    if boot not in {"direct", "mcuboot"}:
        raise BuildError(f"invalid release boot mode: {boot}")
    return (
        _pair_root(workspace, cp.config, ap.config, selected_layout)
        / "releases" / boot
    )


def _build_bl2(repository: Path, workspace: Path, cp: RoleBuild, ap: RoleBuild,
               selected_layout: layout_domain.Layout, toolchain: Toolchain,
               key_source: Path, rollback_floor: int, *, clean: bool) -> Bl2Build:
    root = _release_root(
        workspace, cp, ap, selected_layout, "mcuboot"
    ) / "bl2"
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
    boot = "mcuboot" if signed else "direct"
    root = _release_root(workspace, cp, ap, selected_layout, boot) / "bl1"
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
    root = _release_root(
        workspace, cp, ap, selected_layout, "direct"
    ) / "images"
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


def _manifest_record(pair_root: Path, path: Path, kind: str,
                     label: str) -> dict[str, object]:
    source = _regular(path, label)
    try:
        relative = source.relative_to(pair_root.resolve(strict=True))
    except ValueError as error:
        raise BuildError(f"{label} is outside the paired build root") from error
    return {
        "kind": kind,
        "path": relative.as_posix(),
        "sha256": _sha256_file(source),
        "size": source.stat().st_size,
    }


def _write_build_manifest(
    repository: Path, workspace: Path, boot: str,
    selected_layout: layout_domain.Layout, cp: RoleBuild, ap: RoleBuild,
    bl1: BootBuild, bl2: Bl2Build | None,
    artifacts: tuple[BuiltArtifact, ...], rollback_floor: int | None,
    public_sources: trust_domain.PublicSources | None, toolchain: Toolchain,
) -> Path:
    """Publish one atomic, hash-bound handoff from build to release."""

    pair_root = _directory(
        _pair_root(workspace, cp.config, ap.config, selected_layout),
        "paired build root",
    )
    release_root = _release_root(
        workspace, cp, ap, selected_layout, boot
    )
    try:
        partition = selected_layout.source.resolve(strict=True).relative_to(
            repository.resolve(strict=True)
        )
    except ValueError as error:
        raise BuildError("partition CSV is outside the contest repository") from error

    raw_paths = {"boot": bl1.binary, "cp": cp.binary, "ap": ap.binary}
    elf_paths = {"bl1": bl1.elf, "cp": cp.elf, "ap": ap.elf}
    if boot == "mcuboot":
        if bl2 is None or rollback_floor is None or public_sources is None:
            raise BuildError("MCUboot build manifest lacks BL2 or trust evidence")
        raw_paths["bl2"] = bl2.binary
        elf_paths["bl2"] = bl2.elf
    elif bl2 is not None or rollback_floor is not None or public_sources is not None:
        raise BuildError("direct build manifest received MCUboot-only evidence")

    sdk_rows: dict[str, dict[str, str]] = {}
    for role, profile in (("cp", cp.config.sdk_profile),
                          ("ap", ap.config.sdk_profile)):
        verified = sdk_domain.verify(repository, profile)
        sdk_rows[role] = {
            "profile": profile,
            "tree_sha256": verified.tree_hash,
        }

    trust_fingerprints: dict[str, str] = {}
    if public_sources is not None:
        trust_fingerprints = {
            "bl1_public_fingerprint": public_sources.bl1_fingerprint,
            "mcuboot_public_fingerprint": public_sources.mcuboot_fingerprint,
        }

    document = {
        "boot": boot,
        "elfs": {
            name: _manifest_record(pair_root, path, "elf", f"{name} ELF")
            for name, path in sorted(elf_paths.items())
        },
        "finalized_flash": {
            row.name: _manifest_record(
                pair_root, row.path, "finalized-flash",
                f"finalized {row.name} image",
            )
            for row in sorted(artifacts, key=lambda item: item.name)
        },
        "format": BUILD_MANIFEST_FORMAT,
        "inputs": {
            name: _manifest_record(
                pair_root, path, "raw-build", f"raw {name} input"
            )
            for name, path in sorted(raw_paths.items())
        },
        "layout": {
            "identity": selected_layout.identity,
            "partition": partition.as_posix(),
            "sha256": selected_layout.sha256,
        },
        "roles": {
            role.role: {
                "build_identity": role.build_identity,
                "resolved_config_sha256": role.resolved_config_sha256,
                "sdk_profile": role.config.sdk_profile,
                "seed_defconfig_sha256": role.seed_defconfig_sha256,
            }
            for role in (cp, ap)
        },
        "rollback_floor": rollback_floor,
        "sdk": sdk_rows,
        "target": {
            "board_family": "bk7258",
            "physical_board": cp.config.board,
        },
        "toolchain": {"archive_sha256": toolchain.revision},
        "trust": trust_fingerprints,
    }
    manifest = release_root / "build-manifest.json"
    _atomic_text(
        manifest,
        json.dumps(document, sort_keys=True, separators=(",", ":")) + "\n",
    )
    return _regular(manifest, "build manifest")


def _unique_json_object(pairs: list[tuple[str, object]]) -> dict[str, object]:
    result: dict[str, object] = {}
    for key, value in pairs:
        if key in result:
            raise BuildError(f"duplicate build manifest field: {key}")
        result[key] = value
    return result


def _manifest_mapping(value: object, keys: set[str],
                      label: str) -> dict[str, object]:
    if not isinstance(value, dict) or set(value) != keys:
        raise BuildError(f"build manifest {label} fields are invalid")
    return value


def _manifest_digest(value: object, label: str) -> str:
    if not isinstance(value, str) or DIGEST_RE.fullmatch(value) is None:
        raise BuildError(f"build manifest {label} digest is invalid")
    return value


def _manifest_relative_path(value: object, label: str) -> PurePosixPath:
    if not isinstance(value, str) or not value or "\\" in value:
        raise BuildError(f"build manifest {label} path is invalid")
    path = PurePosixPath(value)
    if path.is_absolute() or any(part in {"", ".", ".."} for part in path.parts) \
            or path.as_posix() != value:
        raise BuildError(f"build manifest {label} path is unsafe")
    return path


def _load_manifest_record(pair_root: Path, value: object, expected_kind: str,
                          label: str) -> Path:
    row = _manifest_mapping(
        value, {"kind", "path", "sha256", "size"}, label
    )
    if row["kind"] != expected_kind:
        raise BuildError(f"build manifest {label} kind is not {expected_kind}")
    relative = _manifest_relative_path(row["path"], label)
    source = _regular(pair_root.joinpath(*relative.parts), label)
    try:
        source.relative_to(pair_root)
    except ValueError as error:
        raise BuildError(f"build manifest {label} escapes its paired root") from error
    size = row["size"]
    if type(size) is not int or size <= 0 or source.stat().st_size != size:
        raise BuildError(f"build manifest {label} size changed")
    digest = _manifest_digest(row["sha256"], label)
    observed = _sha256_file(source)
    if observed != digest:
        raise BuildError(
            f"build manifest {label} hash changed: "
            f"expected={digest} observed={observed}"
        )
    return source


def load_build_manifest(repository: Path, path: Path) -> BuildManifest:
    """Load and re-hash one build-owned release handoff."""

    repository = _directory(repository, "contest repository")
    workspace = _directory(repository.parent, "OpenVela workspace")
    source = _regular(path, "build manifest")
    out_root = _directory(workspace / "out/bk7258", "BK7258 output root")
    try:
        source.relative_to(out_root)
    except ValueError as error:
        raise BuildError("build manifest is outside the BK7258 output root") from error
    if source.name != "build-manifest.json" \
            or source.parent.parent.name != "releases" \
            or source.parent.name not in {"direct", "mcuboot"}:
        raise BuildError("build manifest path does not identify one release mode")
    boot = source.parent.name
    pair_root = _directory(source.parents[2], "paired build root")
    try:
        pair_relative = pair_root.relative_to(out_root)
    except ValueError as error:
        raise BuildError("paired build root escapes the BK7258 output root") \
            from error
    if len(pair_relative.parts) != 3 \
            or re.fullmatch(r"[a-z][a-z0-9_]*", pair_relative.parts[0]) is None:
        raise BuildError("paired build root does not identify one physical board")
    path_board = pair_relative.parts[0]

    if source.stat().st_size > 1024 * 1024:
        raise BuildError("build manifest exceeds the size limit")
    text = source.read_text(encoding="utf-8")
    try:
        document = json.loads(text, object_pairs_hook=_unique_json_object)
    except json.JSONDecodeError as error:
        raise BuildError("build manifest is not valid JSON") from error
    canonical = json.dumps(document, sort_keys=True, separators=(",", ":")) + "\n"
    if text != canonical:
        raise BuildError("build manifest is not canonical JSON")
    manifest_format = document.get("format") if isinstance(document, dict) else None
    root_fields = {
        "boot", "elfs", "finalized_flash", "format", "inputs",
        "layout", "roles", "rollback_floor", "sdk", "toolchain",
        "trust",
    }
    if manifest_format == BUILD_MANIFEST_FORMAT:
        root_fields.add("target")
    elif manifest_format != BUILD_MANIFEST_FORMAT_V1:
        raise BuildError("build manifest format is unsupported")
    document = _manifest_mapping(document, root_fields, "root")
    if document["boot"] != boot:
        raise BuildError("build manifest format or boot mode is invalid")

    if manifest_format == BUILD_MANIFEST_FORMAT:
        target = _manifest_mapping(
            document["target"], {"board_family", "physical_board"}, "target"
        )
        physical_board = target["physical_board"]
        if target["board_family"] != "bk7258" \
                or physical_board != path_board:
            raise BuildError("build manifest target does not match its output root")
    else:
        physical_board = path_board

    layout_row = _manifest_mapping(
        document["layout"], {"identity", "partition", "sha256"}, "layout"
    )
    identity = layout_row["identity"]
    if not isinstance(identity, str) or identity != pair_root.name:
        raise BuildError("build manifest layout identity does not match its output root")
    partition_relative = _manifest_relative_path(
        layout_row["partition"], "partition"
    )
    partition = _regular(
        repository.joinpath(*partition_relative.parts), "partition CSV"
    )
    try:
        partition.relative_to((repository / BOARD_ROOT).resolve(strict=True))
    except ValueError as error:
        raise BuildError("build manifest partition is outside BK7258 boards") from error
    selected_layout = layout_domain.load(partition)
    if selected_layout.identity != identity \
            or selected_layout.sha256 != _manifest_digest(
                layout_row["sha256"], "layout"
            ):
        raise BuildError("build manifest layout evidence changed")

    roles = _manifest_mapping(document["roles"], {"cp", "ap"}, "roles")
    role_identities: dict[str, str] = {}
    role_profiles: dict[str, str] = {}
    for role in ("cp", "ap"):
        row = _manifest_mapping(
            roles[role],
            {
                "build_identity", "resolved_config_sha256", "sdk_profile",
                "seed_defconfig_sha256",
            },
            f"{role} role",
        )
        identity_value = row["build_identity"]
        profile = row["sdk_profile"]
        if not isinstance(identity_value, str) \
                or not re.fullmatch(r"bk7258-role-[0-9a-f]{16}", identity_value) \
                or not isinstance(profile, str) or not profile:
            raise BuildError(f"build manifest {role} role identity is invalid")
        _manifest_digest(row["resolved_config_sha256"], f"{role} resolved config")
        _manifest_digest(row["seed_defconfig_sha256"], f"{role} seed config")
        role_identities[role] = identity_value
        role_profiles[role] = profile

    sdk = _manifest_mapping(document["sdk"], {"cp", "ap"}, "SDK")
    sdk_profiles: list[str] = []
    for role in ("cp", "ap"):
        row = _manifest_mapping(
            sdk[role], {"profile", "tree_sha256"}, f"{role} SDK"
        )
        profile = row["profile"]
        if profile != role_profiles[role]:
            raise BuildError(f"build manifest {role} SDK profile is inconsistent")
        digest = _manifest_digest(row["tree_sha256"], f"{role} SDK")
        verified = sdk_domain.verify(repository, profile)
        if verified.tree_hash != digest:
            raise BuildError(f"build manifest {role} SDK bundle changed")
        sdk_profiles.append(profile)

    toolchain_row = _manifest_mapping(
        document["toolchain"], {"archive_sha256"}, "toolchain"
    )
    toolchain_sha256 = _manifest_digest(
        toolchain_row["archive_sha256"], "toolchain archive"
    )
    try:
        installed_toolchain = toolchain_domain.verify(repository)
    except toolchain_domain.ToolchainError as error:
        raise BuildError(str(error)) from error
    if installed_toolchain.archive_sha256 != toolchain_sha256:
        raise BuildError("build manifest toolchain changed")

    expected_inputs = {"boot", "cp", "ap"}
    expected_elfs = {"bl1", "cp", "ap"}
    if boot == "mcuboot":
        expected_inputs.add("bl2")
        expected_elfs.add("bl2")
    inputs = _manifest_mapping(document["inputs"], expected_inputs, "inputs")
    elfs = _manifest_mapping(document["elfs"], expected_elfs, "ELFs")
    artifact_paths = {
        name: _load_manifest_record(
            pair_root, inputs[name], "raw-build", f"raw {name} input"
        )
        for name in sorted(expected_inputs)
    }
    elf_paths = {
        name: _load_manifest_record(
            pair_root, elfs[name], "elf", f"{name} ELF"
        )
        for name in sorted(expected_elfs)
    }
    for role in ("cp", "ap"):
        prefix = ("roles", boot, role, role_identities[role])
        for label, candidate in (("input", artifact_paths[role]),
                                 ("ELF", elf_paths[role])):
            relative = candidate.relative_to(pair_root)
            if relative.parts[:4] != prefix:
                raise BuildError(
                    f"build manifest {role} {label} is outside its role identity"
                )
    boot_names = ("bl1", "bl2") if boot == "mcuboot" else ("bl1",)
    for name in boot_names:
        candidates = [elf_paths[name]]
        if name == "bl1":
            candidates.append(artifact_paths["boot"])
        elif boot == "mcuboot":
            candidates.append(artifact_paths["bl2"])
        for candidate in candidates:
            relative = candidate.relative_to(pair_root)
            if relative.parts[:3] != ("releases", boot, name):
                raise BuildError(
                    f"build manifest {name} artifact is outside its release mode"
                )

    finalized = document["finalized_flash"]
    finalized_artifacts: dict[str, Path] = {}
    preserved_external: tuple[str, ...] = ()
    if not isinstance(finalized, dict):
        raise BuildError("build manifest finalized Flash evidence is invalid")
    if boot == "mcuboot" and finalized:
        raise BuildError("MCUboot build manifest must not contain finalized Flash images")
    if boot == "direct":
        expected_names = {
            row.artifact for row in selected_layout.partitions
            if row.policy == "image" and row.artifact is not None
        }
        if set(finalized) != expected_names:
            raise BuildError(
                "direct build manifest finalized artifact set is invalid"
            )
        raw = image_domain.read_artifacts(
            {name: artifact_paths[name] for name in ("boot", "cp", "ap")}
        )
        raw["pair"] = image_domain.pair(
            selected_layout, raw["cp"], raw["ap"]
        )
        preserved_external = tuple(sorted(
            row.artifact for row in selected_layout.partitions
            if row.policy == "external" and row.artifact is not None
        ))
        expected_flash = {
            row.artifact: row.data
            for row in image_domain.finalize(
                selected_layout,
                raw,
                preserved_external=preserved_external,
            ).writes
        }
        for name, row in finalized.items():
            path = _load_manifest_record(
                pair_root, row, "finalized-flash", f"finalized {name} image"
            )
            relative = path.relative_to(pair_root)
            if relative.parts != (
                "releases", "direct", "images", f"{name}.bin"
            ):
                raise BuildError(
                    f"direct finalized {name} image is outside its release mode"
                )
            if path.read_bytes() != expected_flash[name]:
                raise BuildError(
                    f"direct finalized {name} image differs from its raw inputs"
                )
            finalized_artifacts[name] = path

    rollback_floor = document["rollback_floor"]
    trust = document["trust"]
    if boot == "mcuboot":
        if type(rollback_floor) is not int or rollback_floor < 0:
            raise BuildError("MCUboot build manifest rollback floor is invalid")
        trust = _manifest_mapping(
            trust,
            {"bl1_public_fingerprint", "mcuboot_public_fingerprint"},
            "trust",
        )
        trust_fingerprints = {
            name: _manifest_digest(value, name)
            for name, value in trust.items()
        }
        if trust_fingerprints["bl1_public_fingerprint"] == \
                trust_fingerprints["mcuboot_public_fingerprint"]:
            raise BuildError("BL1 and MCUboot build roots must be independent")
    else:
        if rollback_floor is not None or trust != {}:
            raise BuildError("direct build manifest contains MCUboot trust evidence")
        trust_fingerprints = {}

    return BuildManifest(
        source=source,
        format=manifest_format,
        physical_board=physical_board,
        boot=boot,
        layout=selected_layout,
        artifacts=artifact_paths,
        finalized_artifacts=finalized_artifacts,
        preserved_external=preserved_external,
        elfs=elf_paths,
        sdk_profiles=tuple(sdk_profiles),
        toolchain_sha256=toolchain_sha256,
        role_identities=role_identities,
        rollback_floor=rollback_floor,
        trust_fingerprints=trust_fingerprints,
    )


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
    release_root = (
        _pair_root(workspace, cp, ap, selected_layout) / "releases" / boot
    )
    if clean:
        _remove_output_tree(release_root, workspace)
    elif release_root.is_symlink():
        raise BuildError(f"release output root must not be a symlink: {release_root}")
    stale_manifest = release_root / "build-manifest.json"
    if stale_manifest.is_symlink() \
            or (stale_manifest.exists() and not stale_manifest.is_file()):
        raise BuildError(
            f"build manifest target must be a regular file: {stale_manifest}"
        )
    stale_manifest.unlink(missing_ok=True)
    public_sources = None
    if boot == "mcuboot":
        assert bl1_public_key is not None
        assert mcuboot_public_key is not None
        assert openssl is not None
        public_sources = trust_domain.write_public_sources(
            bl1_public_key=bl1_public_key,
            mcuboot_public_key=mcuboot_public_key,
            openssl=openssl,
            output=release_root / "trust",
        )
    cp_build_config = _build_config_root(
        workspace, cp, ap, cp, selected_layout, boot
    )
    ap_build_config = _build_config_root(
        workspace, cp, ap, ap, selected_layout, boot
    )
    cp_result = _role_build(
        repository, workspace, official_build, cp, cp_build_config,
        selected_layout, toolchain, jobs, clean
    )
    ap_result = _role_build(
        repository, workspace, official_build, ap, ap_build_config,
        selected_layout, toolchain, jobs, clean,
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
    manifest = _write_build_manifest(
        repository,
        workspace,
        boot,
        selected_layout,
        cp_result,
        ap_result,
        bl1,
        bl2,
        artifacts,
        rollback_floor if boot == "mcuboot" else None,
        public_sources,
        toolchain,
    )
    return BuildResult(
        selected_layout.identity,
        cp_result,
        ap_result,
        bl1,
        bl2,
        artifacts,
        preserved_external,
        manifest,
    )
