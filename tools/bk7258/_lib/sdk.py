"""Manifest-pinned BK7258 SDK profile and bundle lifecycle."""

from __future__ import annotations

import fcntl
import hashlib
import json
import os
import re
import shlex
import shutil
import stat
import subprocess
import tempfile
import time
import xml.etree.ElementTree as ET
from contextlib import contextmanager
from dataclasses import dataclass
from pathlib import Path
from typing import Iterable


class SdkError(RuntimeError):
    """SDK source, profile, bundle, or transaction failure."""


PROFILE_ROOT = Path("chips/bk7258/bk_idk/sdk-profiles")
BUNDLE_ROOT = Path("chips/bk7258/bk_idk/armino_as_lib/versions")
BUNDLE_HASH_PREFIX = "# BK7258_BUNDLE_TREE_SHA256="
BUNDLE_OMIT_PREFIX = "# BK7258_BUNDLE_OMIT="
REQUIRED_ROOTS = frozenset({"config", "include", "libs"})
CONFIG_RE = re.compile(r"^(?:# )?(CONFIG_[A-Za-z0-9_]+)(?:=.*| is not set)$")
PROFILE_RE = re.compile(r"^(cp|ap)(?:-([a-z][a-z0-9_-]*))?$")
HASH_RE = re.compile(r"^[0-9a-f]{64}$")
COMMIT_RE = re.compile(r"^[0-9a-f]{40,64}$")
LINK_INPUT_RE = re.compile(r"[^\s\"']+\.(?:a|o|obj)(?=$|[\s\"'])")
UART_DEFINE = "CONFIG_BK_PRINTF_DISABLE"


@dataclass(frozen=True)
class ManifestSdk:
    path: str
    revision: str
    upstream: str
    version: str
    checkout: Path


@dataclass(frozen=True)
class Profile:
    name: str
    role: str
    components: tuple[Path, ...]
    expected_tree_hash: str | None
    bundle: Path


@dataclass(frozen=True)
class BundleReport:
    profile: str
    role: str
    version: str
    tree_hash: str
    files: int
    bundle: Path


def _regular(path: Path, label: str) -> None:
    try:
        mode = path.lstat().st_mode
    except OSError as error:
        raise SdkError(f"missing {label}: {path}") from error
    if stat.S_ISLNK(mode) or not stat.S_ISREG(mode):
        raise SdkError(f"{label} must be a regular non-symlink file: {path}")


def _directory(path: Path, label: str) -> None:
    try:
        mode = path.lstat().st_mode
    except OSError as error:
        raise SdkError(f"missing {label}: {path}") from error
    if stat.S_ISLNK(mode) or not stat.S_ISDIR(mode):
        raise SdkError(f"{label} must be a real directory: {path}")


def _run(command: list[str], label: str, **kwargs: object) -> subprocess.CompletedProcess:
    try:
        return subprocess.run(command, check=True, **kwargs)
    except subprocess.CalledProcessError as error:
        raise SdkError(f"{label} failed with exit status {error.returncode}") from error
    except OSError as error:
        raise SdkError(f"cannot run {label}: {command[0]}") from error


def manifest_sdk(repository: Path) -> ManifestSdk:
    manifest = repository / f"{repository.name}.xml"
    _regular(manifest, "team manifest")
    try:
        root = ET.parse(manifest).getroot()
    except (OSError, ET.ParseError) as error:
        raise SdkError(f"cannot parse team manifest: {manifest}") from error
    projects = []
    for project in root.iter("project"):
        groups = re.split(r"[\s,]+", project.get("groups", "").strip())
        if "bk7258-sdk" in groups:
            projects.append(project)
    if len(projects) != 1:
        raise SdkError("team manifest must contain exactly one bk7258-sdk project")
    project = projects[0]
    path = project.get("path", "")
    revision = project.get("revision", "")
    upstream = project.get("upstream", "")
    version = upstream.rsplit("/", 1)[-1]
    if not path or not COMMIT_RE.fullmatch(revision) or not version:
        raise SdkError("bk7258-sdk manifest project is incomplete")
    return ManifestSdk(path, revision, upstream, version, repository.parent / path)


def verify_checkout(repository: Path) -> ManifestSdk:
    """Verify the manifest-selected SDK checkout before consuming source artifacts."""

    selected = manifest_sdk(repository)
    _directory(selected.checkout, "manifest SDK checkout")
    head = _run(
        ["git", "-C", str(selected.checkout), "rev-parse", "HEAD"],
        "SDK source identity",
        stdout=subprocess.PIPE,
        text=True,
    ).stdout.strip()
    if head != selected.revision:
        raise SdkError(
            f"SDK source revision mismatch: expected={selected.revision} observed={head}"
        )
    dirty = _run(
        ["git", "-C", str(selected.checkout), "status", "--porcelain"],
        "SDK source cleanliness",
        stdout=subprocess.PIPE,
        text=True,
    ).stdout
    if dirty:
        raise SdkError("manifest SDK checkout must be clean")
    return selected


def _profile_hash(path: Path) -> str | None:
    _regular(path, "SDK profile")
    values = [
        line[len(BUNDLE_HASH_PREFIX):].strip()
        for line in path.read_text(encoding="utf-8").splitlines()
        if line.startswith(BUNDLE_HASH_PREFIX)
    ]
    if not values:
        return None
    if len(values) != 1 or not HASH_RE.fullmatch(values[0]):
        raise SdkError(f"invalid bundle tree hash metadata: {path}")
    return values[0]


def profile(repository: Path, name: str, *, require_hash: bool = True) -> Profile:
    match = PROFILE_RE.fullmatch(name)
    if match is None:
        raise SdkError(f"invalid SDK profile name: {name}")
    role, variant = match.groups()
    sdk = manifest_sdk(repository)
    root = repository / PROFILE_ROOT / sdk.version
    terminal = root / f"{name}.config"
    components = (root / f"{role}.config",)
    if variant is not None:
        components += (terminal,)
    for path in components:
        _regular(path, "SDK profile")
    expected = _profile_hash(terminal)
    if require_hash and expected is None:
        raise SdkError(f"SDK profile has no accepted bundle tree hash: {terminal}")
    return Profile(
        name=name,
        role=role,
        components=components,
        expected_tree_hash=expected,
        bundle=repository / BUNDLE_ROOT / sdk.version / name,
    )


def list_profiles(repository: Path) -> tuple[Profile, ...]:
    sdk = manifest_sdk(repository)
    root = repository / PROFILE_ROOT / sdk.version
    _directory(root, "SDK profile directory")
    result = []
    for path in sorted(root.glob("*.config")):
        if PROFILE_RE.fullmatch(path.stem):
            result.append(profile(repository, path.stem, require_hash=False))
    if not result:
        raise SdkError("no SDK profiles are defined")
    return tuple(result)


def _profile_directives(selected: Profile) -> dict[str, str]:
    result: dict[str, str] = {}
    for path in selected.components:
        for line in path.read_text(encoding="utf-8").splitlines():
            match = CONFIG_RE.fullmatch(line)
            if match is not None:
                result[match.group(1)] = line
    return result


def _profile_omits(selected: Profile) -> set[str]:
    result: set[str] = set()
    for path in selected.components:
        for line in path.read_text(encoding="utf-8").splitlines():
            if not line.startswith(BUNDLE_OMIT_PREFIX):
                continue
            for name in line[len(BUNDLE_OMIT_PREFIX):].split(","):
                value = name.strip()
                if Path(value).name != value or not value.endswith((".a", ".o", ".obj")):
                    raise SdkError(f"invalid SDK bundle omit entry: {value!r}")
                result.add(value)
    return result


def _walk_bundle(bundle: Path) -> tuple[list[tuple[str, str, Path | None]], int]:
    _directory(bundle, "SDK bundle")
    top = list(bundle.iterdir())
    if {item.name for item in top} != REQUIRED_ROOTS or len(top) != len(REQUIRED_ROOTS):
        raise SdkError("SDK bundle roots must be exactly config/include/libs")
    entries: list[tuple[str, str, Path | None]] = []
    files = 0
    for root_name in sorted(REQUIRED_ROOTS):
        root = bundle / root_name
        _directory(root, f"SDK bundle {root_name}")
        entries.append(("dir", root_name, None))
        for current, directories, filenames in os.walk(root, followlinks=False):
            current_path = Path(current)
            directories.sort()
            filenames.sort()
            for directory in directories:
                path = current_path / directory
                _directory(path, "SDK bundle directory")
                entries.append(("dir", path.relative_to(bundle).as_posix(), None))
            for filename in filenames:
                path = current_path / filename
                _regular(path, "SDK bundle file")
                entries.append(("file", path.relative_to(bundle).as_posix(), path))
                files += 1
    if files == 0:
        raise SdkError("SDK bundle contains no files")
    return sorted(entries, key=lambda item: (item[1], item[0])), files


def bundle_tree_hash(bundle: Path) -> tuple[str, int]:
    entries, files = _walk_bundle(bundle)
    digest = hashlib.sha256()
    for kind, relative, path in entries:
        digest.update(kind.encode())
        digest.update(b"\0")
        digest.update(relative.encode("utf-8"))
        digest.update(b"\0")
        if path is not None:
            content = hashlib.sha256()
            with path.open("rb") as stream:
                for chunk in iter(lambda: stream.read(1024 * 1024), b""):
                    content.update(chunk)
            digest.update(content.digest())
    return digest.hexdigest(), files


def _verify_profile_config(selected: Profile) -> None:
    sdkconfig = selected.bundle / "config/sdkconfig.h"
    _regular(sdkconfig, "SDK exported config")
    actual: dict[str, str | None] = {}
    for line in sdkconfig.read_text(encoding="utf-8").splitlines():
        match = re.fullmatch(r"#define (CONFIG_[A-Za-z0-9_]+)(?: (.*))?", line)
        if match is not None:
            actual[match.group(1)] = match.group(2) or ""
    for key, line in _profile_directives(selected).items():
        if line.startswith("# "):
            if key in actual:
                raise SdkError(f"bundle enables profile-disabled setting: {key}")
        else:
            expected = line.split("=", 1)[1]
            expected = "1" if expected == "y" else expected
            if actual.get(key) != expected:
                raise SdkError(f"bundle does not match SDK profile: {key}")


def verify(repository: Path, name: str) -> BundleReport:
    selected = profile(repository, name)
    observed, files = bundle_tree_hash(selected.bundle)
    if observed != selected.expected_tree_hash:
        raise SdkError(
            f"SDK bundle tree hash mismatch for {name}: "
            f"expected={selected.expected_tree_hash} observed={observed}"
        )
    _verify_profile_config(selected)
    link_inputs = [
        item for item in (selected.bundle / "libs").iterdir()
        if item.is_file() and item.suffix in {".a", ".o", ".obj"}
    ]
    if not link_inputs:
        raise SdkError(f"SDK profile has no resolved link inputs: {name}")
    sdk = manifest_sdk(repository)
    return BundleReport(name, selected.role, sdk.version, observed, files, selected.bundle)


def _copy_bundle(source: Path, destination: Path) -> None:
    bundle_tree_hash(source)
    shutil.copytree(source, destination, symlinks=False)


@contextmanager
def _lock(timeout: int):
    if timeout <= 0:
        raise SdkError("lock timeout must be positive")
    path = Path(tempfile.gettempdir()) / f"openvela-bk7258-sdk-{os.getuid()}.lock"
    stream = path.open("a+b")
    try:
        deadline = time.monotonic() + timeout
        while True:
            try:
                fcntl.flock(stream.fileno(), fcntl.LOCK_EX | fcntl.LOCK_NB)
                break
            except BlockingIOError as error:
                if time.monotonic() >= deadline:
                    raise SdkError(f"timed out waiting for SDK lock: {path}") from error
                time.sleep(0.05)
        yield
    finally:
        fcntl.flock(stream.fileno(), fcntl.LOCK_UN)
        stream.close()


def install(repository: Path, name: str, source: Path, *, replace: bool,
            lock_timeout: int = 600) -> BundleReport:
    selected = profile(repository, name)
    source = source.absolute()
    observed, _ = bundle_tree_hash(source)
    if observed != selected.expected_tree_hash:
        raise SdkError("prepared bundle does not match the selected profile hash")
    selected.bundle.parent.mkdir(parents=True, exist_ok=True)
    staged = Path(tempfile.mkdtemp(prefix=f".{name}.install.", dir=selected.bundle.parent))
    staged_bundle = staged / "bundle"
    backup = staged / "previous"
    try:
        _copy_bundle(source, staged_bundle)
        with _lock(lock_timeout):
            if selected.bundle.exists() and not replace:
                raise SdkError("SDK bundle exists; explicit --replace is required")
            if selected.bundle.exists():
                os.rename(selected.bundle, backup)
            try:
                os.rename(staged_bundle, selected.bundle)
                report = verify(repository, name)
            except BaseException:
                if selected.bundle.exists():
                    shutil.rmtree(selected.bundle)
                if backup.exists():
                    os.rename(backup, selected.bundle)
                raise
            if backup.exists():
                shutil.rmtree(backup)
            return report
    finally:
        shutil.rmtree(staged, ignore_errors=True)


def _merge_profile(config: Path, selected: Profile) -> None:
    _regular(config, "official SDK project config")
    overrides = _profile_directives(selected)
    kept = []
    for line in config.read_text(encoding="utf-8").splitlines():
        match = CONFIG_RE.fullmatch(line)
        if match is None or match.group(1) not in overrides:
            kept.append(line)
    kept.extend(("", "# OpenVela BK7258 SDK profile"))
    kept.extend(overrides.values())
    config.write_text("\n".join(kept) + "\n", encoding="utf-8")


def _find_export(build_root: Path, sdk_target: str) -> tuple[Path, Path]:
    matches = []
    for root in build_root.rglob("armino_as_lib"):
        role = root / sdk_target
        if (root / "include").is_dir() and (role / "config").is_dir() and (role / "libs").is_dir():
            matches.append((root, role))
    if len(matches) != 1:
        raise SdkError(f"official SDK build must produce one {sdk_target} export")
    return matches[0]


def _link_inputs(build_root: Path) -> tuple[Path, ...]:
    result: dict[Path, None] = {}
    for ninja_file in build_root.rglob("build.ninja"):
        directory = ninja_file.parent
        for elf in directory.glob("app.elf"):
            command = _run(
                ["ninja", "-C", str(directory), "-t", "commands", elf.name],
                "official SDK link-command query",
                stdout=subprocess.PIPE,
                stderr=subprocess.PIPE,
                text=True,
            )
            for match in LINK_INPUT_RE.findall(command.stdout):
                path = Path(match)
                if not path.is_absolute():
                    path = directory / path
                try:
                    path = path.resolve(strict=True)
                except OSError:
                    continue
                if path.is_file():
                    result[path] = None
    if not result:
        raise SdkError("official SDK build exposes no app.elf link command")
    return tuple(result)


def _stage_export(export: Path, role_export: Path,
                  link_inputs: tuple[Path, ...], source_root: Path,
                  build_root: Path, selected_profile: Profile,
                  destination: Path) -> None:
    destination.mkdir()
    shutil.copytree(export / "include", destination / "include", symlinks=False)
    partition_headers = list(build_root.rglob("partitions_gen.h"))
    if len(partition_headers) != 1:
        raise SdkError("official SDK build must produce one partitions_gen.h")
    shutil.copy2(partition_headers[0], destination / "include/partitions.h")
    shutil.copytree(role_export / "config", destination / "config", symlinks=False)
    libraries = destination / "libs"
    libraries.mkdir()
    candidates = [item for item in (role_export / "libs").iterdir() if item.is_file()]
    duplicates = {item.name for item in candidates if sum(other.name == item.name for other in candidates) > 1}
    if duplicates:
        raise SdkError("duplicate official SDK link input names: " + ", ".join(sorted(duplicates)))
    omitted = _profile_omits(selected_profile)
    link_names = {item.name for item in link_inputs}
    selected = [
        item for item in candidates
        if item.name in link_names and item.name not in omitted
    ]
    if not selected:
        raise SdkError("official SDK export and link command have no common inputs")
    for item in selected:
        _regular(item, "official SDK link input")
        shutil.copy2(item, libraries / item.name)

    # The official export flattens component archives by basename.  A linked
    # immutable prebuilt under components/bk_libs can legitimately share that
    # basename with its source-built adapter (notably BK7258 libbk_phy.a).
    # Preserve both without a handwritten library map; the link command and
    # manifest-pinned source path are the only selection authority.

    for item in link_inputs:
        try:
            relative = item.relative_to(source_root)
        except ValueError:
            continue
        if "bk_libs" not in relative.parts or item.name in omitted:
            continue
        _regular(item, "official SDK immutable link input")
        target = libraries / item.name
        if target.exists() and target.read_bytes() == item.read_bytes():
            continue
        if target.exists():
            digest = hashlib.sha256(item.read_bytes()).hexdigest()[:12]
            target = libraries / f"immutable-{digest}-{item.name}"
        if target.exists():
            raise SdkError(f"duplicate immutable SDK link input: {target.name}")
        shutil.copy2(item, target)


def _uart_command(compile_database: Path, role: str, output: Path,
                  compiler: Path) -> tuple[list[str], Path]:
    entries = json.loads(compile_database.read_text(encoding="utf-8"))
    suffix = f"/{role}/middleware/driver/uart/uart_driver.c"
    matches = [
        row for row in entries
        if isinstance(row, dict)
        and isinstance(row.get("file"), str)
        and row["file"].replace("\\", "/").endswith(suffix)
    ]
    if len(matches) != 1:
        raise SdkError(f"official compile database must contain one {role} UART source")
    row = matches[0]
    if isinstance(row.get("arguments"), list):
        command = [str(value) for value in row["arguments"]]
    elif isinstance(row.get("command"), str):
        command = shlex.split(row["command"])
    else:
        raise SdkError("official UART compile entry has no command")
    command[0] = str(compiler)
    define = f"-D{UART_DEFINE}"
    if define not in command:
        command.insert(1, define)
    try:
        index = command.index("-o") + 1
        command[index] = str(output)
    except (ValueError, IndexError) as error:
        raise SdkError("official UART compile command has no output") from error
    directory = row.get("directory")
    if not isinstance(directory, str):
        raise SdkError("official UART compile entry has no directory")
    return command, Path(directory)


def _patch_uart(bundle: Path, build_root: Path, role: str, toolchain: Path,
                work: Path) -> None:
    gcc = toolchain / "arm-none-eabi-gcc"
    ar = toolchain / "arm-none-eabi-ar"
    nm = toolchain / "arm-none-eabi-nm"
    for tool in (gcc, ar, nm):
        if not tool.is_file() or not os.access(tool, os.X_OK):
            raise SdkError(f"required SDK tool is missing: {tool}")
    databases = list(build_root.rglob("compile_commands.json"))
    if len(databases) != 1:
        raise SdkError("official SDK build must produce one compile_commands.json")
    patched = work / "uart_driver.c.obj"
    command, cwd = _uart_command(databases[0], role, patched, gcc)
    _run(command, "SDK UART profile patch", cwd=cwd)
    _regular(patched, "patched UART object")
    owners = []
    for archive in sorted((bundle / "libs").glob("*.a")):
        result = _run([str(ar), "t", str(archive)], "SDK archive inspection",
                      stdout=subprocess.PIPE, text=True)
        if "uart_driver.c.obj" in result.stdout.splitlines():
            owners.append(archive)
    if len(owners) != 1:
        raise SdkError("resolved SDK closure must contain one UART archive owner")
    _run([str(ar), "r", str(owners[0]), str(patched)], "SDK UART archive update")
    result = _run([str(nm), "-u", str(patched)], "SDK UART symbol verification",
                  stdout=subprocess.PIPE, text=True)
    if "bk_printf_init" in result.stdout:
        raise SdkError("patched UART object still references bk_printf_init")


def _profile_with_hash(path: Path, tree_hash: str) -> str:
    lines = [
        line for line in path.read_text(encoding="utf-8").splitlines()
        if not line.startswith(BUNDLE_HASH_PREFIX)
    ]
    lines.insert(0, BUNDLE_HASH_PREFIX + tree_hash)
    return "\n".join(lines) + "\n"


def rebuild(repository: Path, name: str, source: Path, toolchain: Path, *,
            jobs: int, replace: bool, lock_timeout: int = 600) -> BundleReport:
    """Rebuild one SDK profile from the exact manifest-pinned source."""

    if jobs <= 0:
        raise SdkError("jobs must be positive")
    selected = profile(repository, name, require_hash=False)
    sdk = manifest_sdk(repository)
    source = source.absolute()
    _directory(source, "SDK source checkout")
    head = _run(["git", "-C", str(source), "rev-parse", "HEAD"],
                "SDK source identity", stdout=subprocess.PIPE, text=True).stdout.strip()
    if head != sdk.revision:
        raise SdkError(f"SDK source revision mismatch: expected={sdk.revision} observed={head}")
    if _run(["git", "-C", str(source), "status", "--porcelain"],
            "SDK source cleanliness", stdout=subprocess.PIPE, text=True).stdout:
        raise SdkError("SDK source checkout must be clean")
    toolchain = toolchain.absolute()
    _directory(toolchain, "SDK toolchain directory")

    role_target = "bk7258_cp" if selected.role == "cp" else "bk7258_ap"
    sdk_target = "bk7258" if selected.role == "cp" else "bk7258_ap"
    official_config = (
        Path("projects/app/cp/config/bk7258/config")
        if selected.role == "cp"
        else Path("projects/app/ap/config/bk7258_ap/config")
    )
    selected.bundle.parent.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix=f"bk7258-sdk-{name}-") as temporary_name:
        work = Path(temporary_name)
        clone = work / "source"
        _run(["git", "clone", "--local", "--no-hardlinks", "--no-checkout",
              str(source), str(clone)], "SDK local source clone")
        _run(["git", "-C", str(clone), "checkout", "--detach", sdk.revision],
             "SDK source checkout")
        _merge_profile(clone / official_config, selected)
        build_root = work / "build"
        _run(
            [
                "make", "-C", str(clone), role_target, "PROJECT=app",
                f"BUILD_DIR={build_root}",
                f"COMPILER_TOOLCHAIN_PATH={toolchain}", f"-j{jobs}",
            ],
            "official SDK profile build",
        )
        export, role_export = _find_export(build_root, sdk_target)
        staged = work / "bundle"
        _stage_export(
            export, role_export, _link_inputs(build_root), clone, build_root,
            selected, staged
        )
        _patch_uart(staged, build_root, selected.role, toolchain, work)
        observed, _ = bundle_tree_hash(staged)

        transaction = Path(tempfile.mkdtemp(prefix=f".{name}.rebuild.", dir=selected.bundle.parent))
        staged_bundle = transaction / "bundle"
        shutil.copytree(staged, staged_bundle)
        profile_path = selected.components[-1]
        profile_temp = transaction / "profile.config"
        profile_temp.write_text(_profile_with_hash(profile_path, observed), encoding="utf-8")
        backup_bundle = transaction / "previous-bundle"
        backup_profile = transaction / "previous-profile"
        try:
            with _lock(lock_timeout):
                if selected.bundle.exists() and not replace:
                    raise SdkError("SDK bundle exists; explicit --replace is required")
                if selected.bundle.exists():
                    os.rename(selected.bundle, backup_bundle)
                os.rename(profile_path, backup_profile)
                try:
                    os.rename(staged_bundle, selected.bundle)
                    os.rename(profile_temp, profile_path)
                    report = verify(repository, name)
                except BaseException:
                    if selected.bundle.exists():
                        shutil.rmtree(selected.bundle)
                    if profile_path.exists():
                        profile_path.unlink()
                    if backup_bundle.exists():
                        os.rename(backup_bundle, selected.bundle)
                    os.rename(backup_profile, profile_path)
                    raise
                if backup_bundle.exists():
                    shutil.rmtree(backup_bundle)
                backup_profile.unlink()
                return report
        finally:
            shutil.rmtree(transaction, ignore_errors=True)
