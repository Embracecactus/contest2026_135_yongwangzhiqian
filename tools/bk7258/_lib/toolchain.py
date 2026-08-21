"""Content-addressed BK7258 Arm GNU toolchain installation."""

from __future__ import annotations

import fcntl
import hashlib
import json
import os
import posixpath
import shutil
import stat
import subprocess
import tarfile
import tempfile
import time
import urllib.error
import urllib.parse
import urllib.request
from contextlib import contextmanager
from dataclasses import dataclass
from pathlib import Path, PurePosixPath


class ToolchainError(RuntimeError):
    """The locked toolchain is absent, corrupt, or cannot be installed."""


LOCK_PATH = Path("tools/bk7258/toolchain.json")
LOCK_SCHEMA = "bk7258-toolchain/1"
RECEIPT = ".bk7258-toolchain.json"
RECEIPT_SCHEMA = "bk7258-toolchain-install/1"
REQUIRED_TOOLS = (
    "arm-none-eabi-gcc",
    "arm-none-eabi-g++",
    "arm-none-eabi-ar",
    "arm-none-eabi-objcopy",
    "arm-none-eabi-objdump",
    "arm-none-eabi-size",
    "arm-none-eabi-nm",
    "arm-none-eabi-readelf",
)


@dataclass(frozen=True)
class Lock:
    name: str
    url: str
    archive: str
    sha256: str
    install: Path
    gcc_version: str


@dataclass(frozen=True)
class Report:
    root: Path
    binary_dir: Path
    archive_sha256: str
    gcc_version: str


def _regular(path: Path, label: str) -> Path:
    try:
        mode = path.lstat().st_mode
    except OSError as error:
        raise ToolchainError(f"missing {label}: {path}") from error
    if stat.S_ISLNK(mode) or not stat.S_ISREG(mode):
        raise ToolchainError(f"{label} must be a regular non-symlink file: {path}")
    return path


def _directory(path: Path, label: str) -> Path:
    try:
        mode = path.lstat().st_mode
    except OSError as error:
        raise ToolchainError(f"missing {label}: {path}") from error
    if stat.S_ISLNK(mode) or not stat.S_ISDIR(mode):
        raise ToolchainError(f"{label} must be a real directory: {path}")
    return path


def load(repository: Path) -> Lock:
    path = _regular(repository / LOCK_PATH, "toolchain lock")
    try:
        value = json.loads(path.read_text(encoding="utf-8"))
    except (OSError, UnicodeError, json.JSONDecodeError) as error:
        raise ToolchainError(f"cannot parse toolchain lock: {path}") from error
    keys = {"schema", "name", "url", "archive", "sha256", "install", "gcc_version"}
    if not isinstance(value, dict) or set(value) != keys:
        raise ToolchainError("toolchain lock has an unsupported schema")
    if value["schema"] != LOCK_SCHEMA:
        raise ToolchainError("toolchain lock version is unsupported")
    for key in keys - {"schema"}:
        if not isinstance(value[key], str) or not value[key]:
            raise ToolchainError(f"toolchain lock field is invalid: {key}")
    if len(value["sha256"]) != 64 or any(ch not in "0123456789abcdef" for ch in value["sha256"]):
        raise ToolchainError("toolchain archive SHA-256 is invalid")
    url = urllib.parse.urlparse(value["url"])
    if url.scheme != "https" or url.hostname != "developer.arm.com":
        raise ToolchainError("toolchain URL must use the Arm official HTTPS host")
    if Path(url.path).name != value["archive"] or Path(value["archive"]).name != value["archive"]:
        raise ToolchainError("toolchain archive name does not match its URL")
    install = Path(value["install"])
    if (
        install.is_absolute()
        or not install.parts
        or install.parts[0] != "prebuilt"
        or install.name != value["name"]
        or ".." in install.parts
    ):
        raise ToolchainError("toolchain install path must remain inside the workspace")
    return Lock(
        value["name"], value["url"], value["archive"], value["sha256"],
        install, value["gcc_version"],
    )


def _sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def _receipt(lock: Lock) -> str:
    return json.dumps(
        {
            "archive_sha256": lock.sha256,
            "schema": RECEIPT_SCHEMA,
        },
        sort_keys=True,
        separators=(",", ":"),
    ) + "\n"


def _capture(command: list[str], label: str) -> str:
    try:
        result = subprocess.run(
            command,
            check=True,
            stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT,
            text=True,
            env={"PATH": "/usr/bin:/bin", "LC_ALL": "C"},
        )
    except (OSError, subprocess.CalledProcessError) as error:
        raise ToolchainError(f"cannot run {label}: {command[0]}") from error
    return result.stdout.strip()


def _verify_root(root: Path, lock: Lock, *, require_receipt: bool) -> Report:
    _directory(root, "BK7258 toolchain installation")
    if require_receipt:
        receipt = _regular(root / RECEIPT, "toolchain receipt")
        try:
            observed = receipt.read_text(encoding="utf-8")
        except (OSError, UnicodeError) as error:
            raise ToolchainError(f"cannot read toolchain receipt: {receipt}") from error
        if observed != _receipt(lock):
            raise ToolchainError("installed toolchain receipt does not match the lock")
    binary_dir = _directory(root / "bin", "BK7258 toolchain bin directory")
    for name in REQUIRED_TOOLS:
        tool = _regular(binary_dir / name, f"BK7258 tool {name}")
        if not os.access(tool, os.X_OK):
            raise ToolchainError(f"BK7258 tool is not executable: {tool}")
    version = _capture([str(binary_dir / "arm-none-eabi-gcc"), "--version"], "BK7258 GCC version")
    first_line = version.splitlines()[0] if version else ""
    if first_line != lock.gcc_version:
        raise ToolchainError(
            f"BK7258 GCC version mismatch: expected={lock.gcc_version!r} observed={first_line!r}"
        )
    return Report(root, binary_dir, lock.sha256, first_line)


def verify(repository: Path) -> Report:
    repository = _directory(repository, "contest repository")
    lock = load(repository)
    return _verify_root(repository / lock.install, lock, require_receipt=True)


def _download(url: str, destination: Path) -> None:
    request = urllib.request.Request(url, headers={"User-Agent": "openvela-bk7258-toolchain/1"})
    try:
        with urllib.request.urlopen(request, timeout=60) as response, destination.open("wb") as output:
            shutil.copyfileobj(response, output, length=1024 * 1024)
    except (OSError, urllib.error.URLError) as error:
        raise ToolchainError(f"cannot download locked toolchain: {url}") from error


def _safe_member(member: tarfile.TarInfo, top: str) -> None:
    name = PurePosixPath(member.name)
    if name.is_absolute() or not name.parts or ".." in name.parts or name.parts[0] != top:
        raise ToolchainError(f"unsafe toolchain archive member: {member.name!r}")
    if member.isdev() or member.isfifo():
        raise ToolchainError(f"unsupported toolchain archive member: {member.name!r}")
    if member.issym() or member.islnk():
        target = member.linkname
        if member.issym():
            target = posixpath.join(posixpath.dirname(member.name), target)
        normalized = PurePosixPath(posixpath.normpath(target))
        if normalized.is_absolute() or not normalized.parts or normalized.parts[0] != top or ".." in normalized.parts:
            raise ToolchainError(f"unsafe toolchain archive link: {member.name!r}")


@contextmanager
def _lock(timeout: int):
    if timeout <= 0:
        raise ToolchainError("lock timeout must be positive")
    path = Path(tempfile.gettempdir()) / f"openvela-bk7258-toolchain-{os.getuid()}.lock"
    stream = path.open("a+b")
    try:
        deadline = time.monotonic() + timeout
        while True:
            try:
                fcntl.flock(stream.fileno(), fcntl.LOCK_EX | fcntl.LOCK_NB)
                break
            except BlockingIOError as error:
                if time.monotonic() >= deadline:
                    raise ToolchainError(f"timed out waiting for toolchain lock: {path}") from error
                time.sleep(0.05)
        yield
    finally:
        fcntl.flock(stream.fileno(), fcntl.LOCK_UN)
        stream.close()


def install(repository: Path, archive: Path | None, *, replace: bool,
            lock_timeout: int = 600) -> Report:
    repository = _directory(repository, "contest repository")
    lock = load(repository)
    target = repository / lock.install
    target.parent.mkdir(parents=True, exist_ok=True)
    transaction = Path(tempfile.mkdtemp(prefix=".bk7258-toolchain.", dir=target.parent))
    try:
        source = transaction / lock.archive
        if archive is None:
            _download(lock.url, source)
        else:
            archive = _regular(archive.absolute(), "toolchain archive")
            shutil.copy2(archive, source)
        observed = _sha256(source)
        if observed != lock.sha256:
            raise ToolchainError(
                f"toolchain archive SHA-256 mismatch: expected={lock.sha256} observed={observed}"
            )
        extracted = transaction / "extract"
        extracted.mkdir()
        try:
            with tarfile.open(source, mode="r:bz2") as package:
                members = package.getmembers()
                if not members:
                    raise ToolchainError("toolchain archive is empty")
                for member in members:
                    _safe_member(member, lock.name)
                package.extractall(extracted)
        except (OSError, tarfile.TarError) as error:
            raise ToolchainError("cannot extract locked toolchain archive") from error
        candidate = extracted / lock.name
        _verify_root(candidate, lock, require_receipt=False)
        (candidate / RECEIPT).write_text(_receipt(lock), encoding="utf-8")
        backup = transaction / "previous"
        with _lock(lock_timeout):
            if target.exists() and not replace:
                return verify(repository)
            if target.exists():
                os.rename(target, backup)
            try:
                os.rename(candidate, target)
                report = verify(repository)
            except BaseException:
                if target.exists():
                    shutil.rmtree(target)
                if backup.exists():
                    os.rename(backup, target)
                raise
            if backup.exists():
                shutil.rmtree(backup)
            return report
    finally:
        shutil.rmtree(transaction, ignore_errors=True)
