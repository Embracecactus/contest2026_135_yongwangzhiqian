#!/usr/bin/env python3
"""The sole maintainer-facing BK7258 build and release workflow entry."""

from __future__ import annotations

import argparse
import hashlib
import json
import os
import re
import shutil
import sys
import tempfile
from pathlib import Path


TOOLS = Path(__file__).resolve().parent
REPOSITORY = TOOLS.parents[1]
sys.path.insert(0, str(TOOLS))

from _lib import build as build_domain  # noqa: E402
from _lib import image as image_domain  # noqa: E402
from _lib import layout as layout_domain  # noqa: E402
from _lib import package as package_domain  # noqa: E402
from _lib import sdk as sdk_domain  # noqa: E402
from _lib import toolchain as toolchain_domain  # noqa: E402
from _lib import trust as trust_domain  # noqa: E402


def _parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(prog="bk7258.py")
    commands = parser.add_subparsers(dest="command", required=True)

    build = commands.add_parser("build", help="build CP and AP through OpenVela")
    build.add_argument(
        "--board", metavar="NAME",
        help="load the physical board's maintained openvela.conf declaration",
    )
    build.add_argument("--cp-config", type=Path)
    build.add_argument("--ap-config", type=Path)
    build.add_argument(
        "--boot", choices=("direct", "mcuboot"), required=True,
        help=(
            "mcuboot is the only signed release chain; direct is an unsigned "
            "bring-up/diagnostic chain and cannot be released"
        ),
    )
    build.add_argument("--partition", type=Path)
    build.add_argument("--jobs", type=int, default=min(os.cpu_count() or 1, 8))
    build.add_argument("--bl1-public-key", type=Path)
    build.add_argument("--mcuboot-public-key", type=Path)
    build.add_argument("--openssl", type=Path)
    build.add_argument("--rollback-floor", type=lambda value: int(value, 0))
    build.add_argument("--clean", action="store_true")

    toolchain = commands.add_parser("toolchain", help="manage the locked Arm GNU toolchain")
    toolchain_commands = toolchain.add_subparsers(dest="toolchain_command", required=True)
    toolchain_install = toolchain_commands.add_parser("install", help="install the locked toolchain")
    toolchain_install.add_argument("--archive", type=Path)
    toolchain_install.add_argument("--replace", action="store_true")
    toolchain_commands.add_parser("verify", help="verify the installed locked toolchain")

    sdk = commands.add_parser("sdk", help="manage manifest-pinned SDK bundles")
    sdk_commands = sdk.add_subparsers(dest="sdk_command", required=True)
    sdk_commands.add_parser("list", help="list explicit SDK profiles")
    sdk_verify = sdk_commands.add_parser("verify", help="verify one SDK profile bundle")
    sdk_verify.add_argument("--profile", required=True)
    sdk_install = sdk_commands.add_parser("install", help="install one prepared bundle")
    sdk_install.add_argument("--profile", required=True)
    sdk_install.add_argument("--bundle", type=Path, required=True)
    sdk_install.add_argument("--replace", action="store_true")
    sdk_rebuild = sdk_commands.add_parser("rebuild", help="rebuild one SDK profile")
    sdk_rebuild.add_argument("--profile", required=True)
    sdk_rebuild.add_argument("--source", type=Path, required=True)
    sdk_rebuild.add_argument("--jobs", type=int, required=True)
    sdk_rebuild.add_argument("--replace", action="store_true")

    package = commands.add_parser(
        "package", help="inspect packages or create unsigned diagnostic packages"
    )
    package_commands = package.add_subparsers(dest="package_command", required=True)
    create = package_commands.add_parser(
        "create", help="create one unsigned direct-boot diagnostic package"
    )
    create.add_argument("--build-manifest", type=Path, required=True)
    create.add_argument(
        "--unsigned", action="store_true", required=True,
        help="package already-finalized direct-boot bytes for diagnostics",
    )
    create.add_argument("--output", type=Path, required=True)
    extract = package_commands.add_parser("extract", help="extract a verified package")
    extract.add_argument("--package", type=Path, required=True)
    extract.add_argument("--output", type=Path, required=True)
    flash_contract = package_commands.add_parser(
        "flash-contract", help="print the verified sparse write contract"
    )
    flash_contract.add_argument("--package", type=Path, required=True)
    materialize = package_commands.add_parser(
        "materialize", help="create one trust-verified BKFIL full image"
    )
    materialize.add_argument("--package", type=Path, required=True)
    materialize.add_argument("--base", type=Path, required=True)
    materialize.add_argument("--base-sha256", required=True)
    materialize.add_argument("--openssl", type=Path, required=True)
    materialize.add_argument("--output", type=Path, required=True)

    release = commands.add_parser(
        "release", help="publish a hash-bound signed release from one build manifest"
    )
    release_commands = release.add_subparsers(
        dest="release_command", required=True
    )
    full = release_commands.add_parser(
        "full", help="create and materialize one signed full release"
    )
    full.add_argument("--build-manifest", type=Path, required=True)
    full.add_argument("--bl1-key", type=Path, required=True)
    full.add_argument("--mcuboot-key", type=Path, required=True)
    full.add_argument("--version", required=True)
    full.add_argument("--base", type=Path, required=True)
    full.add_argument("--base-sha256", required=True)
    full.add_argument("--openssl", type=Path, required=True)
    full.add_argument("--output-dir", type=Path, required=True)
    ota = release_commands.add_parser(
        "ota", help="create one pending signed CP/AP OTA release"
    )
    ota.add_argument("--build-manifest", type=Path, required=True)
    ota.add_argument("--mcuboot-key", type=Path, required=True)
    ota.add_argument("--version", required=True)
    ota.add_argument("--openssl", type=Path, required=True)
    ota.add_argument("--output-dir", type=Path, required=True)

    verify = commands.add_parser("verify", help="perform read-only verification")
    verify_commands = verify.add_subparsers(dest="verify_command", required=True)
    verify_layout = verify_commands.add_parser("layout", help="verify one partition CSV")
    verify_layout.add_argument("--partition", type=Path, required=True)
    verify_image = verify_commands.add_parser("image", help="verify artifact placement")
    verify_image.add_argument("--partition", type=Path, required=True)
    verify_image.add_argument("--artifact", action="append", required=True,
                              metavar="NAME=PATH")
    verify_image.add_argument("--preserve-external", action="append", default=[],
                              metavar="NAME")
    verify_manifest = verify_commands.add_parser(
        "build-manifest", help="re-hash one build-to-release handoff"
    )
    verify_manifest.add_argument("--manifest", type=Path, required=True)
    verify_package = verify_commands.add_parser("package", help="verify a package")
    verify_package.add_argument("--package", type=Path, required=True)
    verify_trust = verify_commands.add_parser("trust", help="verify package trust evidence")
    verify_trust.add_argument("--package", type=Path, required=True)
    verify_trust.add_argument("--openssl", type=Path, required=True)
    return parser


def _pairs(values: list[str], label: str) -> dict[str, str]:
    result: dict[str, str] = {}
    for value in values:
        if "=" not in value:
            raise ValueError(f"{label} must be NAME=VALUE: {value!r}")
        name, item = value.split("=", 1)
        if not name or not item or name in result:
            raise ValueError(f"invalid or duplicate {label}: {value!r}")
        result[name] = item
    return result


def _repository_input(path: Path) -> Path:
    """Resolve repository-owned inputs independently of the caller's cwd."""

    return path if path.is_absolute() else REPOSITORY / path


def _workspace_input(path: Path) -> Path:
    """Resolve generated OpenVela inputs independently of the caller's cwd."""

    return path if path.is_absolute() else REPOSITORY.parent / path


def _build_inputs(args: argparse.Namespace) -> tuple[Path, Path, Path]:
    explicit = (args.cp_config, args.ap_config, args.partition)
    if args.board is not None:
        if any(value is not None for value in explicit):
            raise ValueError(
                "--board cannot be mixed with --cp-config, --ap-config or "
                "--partition"
            )
        preset = build_domain.board_preset(REPOSITORY, args.board)
        return preset.cp_config, preset.ap_config, preset.partition
    if any(value is None for value in explicit):
        raise ValueError(
            "build requires either --board or all of --cp-config, "
            "--ap-config and --partition"
        )
    cp_config, ap_config, partition = explicit
    assert cp_config is not None
    assert ap_config is not None
    assert partition is not None
    return cp_config, ap_config, partition


def _sha256_file(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def _release_generation(version: str) -> int:
    match = re.fullmatch(
        r"(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)\."
        r"(0|[1-9][0-9]*)\+([1-9][0-9]*)",
        version,
    )
    if match is None:
        raise ValueError(
            "release version must be MAJOR.MINOR.REVISION+GENERATION"
        )
    generation = int(match.group(4), 10)
    if generation > 0xffffffff:
        raise ValueError("release generation exceeds the MCUboot counter range")
    return generation


def _release_output(path: Path) -> tuple[Path, Path]:
    output = path.absolute()
    if output.exists() or output.is_symlink():
        raise ValueError(f"release output directory already exists: {output}")
    output.parent.mkdir(parents=True, exist_ok=True)
    staging = Path(tempfile.mkdtemp(prefix=f".{output.name}.", dir=output.parent))
    return output, staging


def _release_sdk_evidence(
    manifest: build_domain.BuildManifest,
) -> dict[str, str]:
    result: dict[str, str] = {}
    for profile in manifest.sdk_profiles:
        verified = sdk_domain.verify(REPOSITORY, profile)
        result[profile] = verified.tree_hash
    return result


def _release_summary(staging: Path, document: dict[str, object]) -> None:
    target = staging / "release.json"
    target.write_text(
        json.dumps(document, sort_keys=True, separators=(",", ":")) + "\n",
        encoding="utf-8",
        newline="\n",
    )


def _release(args: argparse.Namespace) -> None:
    manifest = build_domain.load_build_manifest(
        REPOSITORY, _workspace_input(args.build_manifest)
    )
    manifest_sha256 = _sha256_file(manifest.source)
    if manifest.format != build_domain.BUILD_MANIFEST_FORMAT:
        raise build_domain.BuildError(
            "signed releases require a target-bound build manifest"
        )
    if manifest.boot != "mcuboot" or manifest.rollback_floor is None:
        raise build_domain.BuildError(
            "signed releases require one MCUboot build manifest"
        )
    generation = _release_generation(args.version)
    if args.release_command == "full" \
            and generation != manifest.rollback_floor:
        raise trust_domain.TrustError(
            "full release generation must equal the compiled rollback floor"
        )
    if args.release_command == "ota" \
            and generation < manifest.rollback_floor:
        raise trust_domain.TrustError(
            "OTA generation is below the compiled rollback floor"
        )

    output, staging = _release_output(args.output_dir)
    try:
        package_root = staging / "package"
        evidence_root = staging / "evidence"
        package_root.mkdir(parents=True)
        evidence_root.mkdir(parents=True)
        toolchain = build_domain.toolchain_root(REPOSITORY) / "bin"
        sdk_evidence = _release_sdk_evidence(manifest)
        official_imgtool = (
            REPOSITORY.parent / "apps/boot/mcuboot/mcuboot/scripts/imgtool.py"
        )
        if args.release_command == "full":
            signed = trust_domain.signed_release(
                layout=manifest.layout,
                artifacts=manifest.artifacts,
                bl1_private_key=args.bl1_key,
                mcuboot_private_key=args.mcuboot_key,
                bl1_elf=manifest.elfs["bl1"],
                bl2_elf=manifest.elfs["bl2"],
                version=args.version,
                security_counter=generation,
                bl1_security_counter=generation,
                official_imgtool=official_imgtool,
                openssl=args.openssl,
                objcopy=toolchain / "arm-none-eabi-objcopy",
                nm=toolchain / "arm-none-eabi-nm",
            )
            evidence = signed.evidence.manifest()
            persistent_payload = package_domain.persistent_payload_from_base(
                manifest.layout, args.base, args.base_sha256
            )
            suffix = "full"
            member_names = {
                "ap": "ap.bin",
                "bl2_a": "bl2-a.bin",
                "bl2_b": "bl2-b.bin",
                "boot": "boot.bin",
                "cp": "cp.bin",
                "manifest_a": "manifest-a.bin",
                "manifest_b": "manifest-b.bin",
                "pair": "pair.bin",
            }
        else:
            signed = trust_domain.signed_ota_pair(
                layout=manifest.layout,
                artifacts={
                    name: manifest.artifacts[name] for name in ("cp", "ap")
                },
                mcuboot_private_key=args.mcuboot_key,
                bl2_elf=manifest.elfs["bl2"],
                version=args.version,
                security_counter=generation,
                official_imgtool=official_imgtool,
                openssl=args.openssl,
                objcopy=toolchain / "arm-none-eabi-objcopy",
            )
            evidence = signed.evidence.manifest()
            persistent_payload = None
            suffix = "ota"
            member_names = {"ap": "ap.bin", "cp": "cp.bin"}

        # No raw build input is read after this point.  Re-hash the complete
        # handoff once more so a concurrent or accidental artifact change
        # cannot produce a package that disagrees with its copied evidence.
        build_domain.load_build_manifest(REPOSITORY, manifest.source)
        if _sha256_file(manifest.source) != manifest_sha256:
            raise build_domain.BuildError(
                "build manifest changed while the release was being created"
            )
        manifest_copy = evidence_root / "build-manifest.json"
        shutil.copyfile(manifest.source, manifest_copy)
        if _sha256_file(manifest_copy) != manifest_sha256:
            raise build_domain.BuildError("copied build manifest hash changed")

        fingerprint_names = (
            ("bl1_public_fingerprint", "mcuboot_public_fingerprint")
            if args.release_command == "full"
            else ("mcuboot_public_fingerprint",)
        )
        for name in fingerprint_names:
            expected = manifest.trust_fingerprints.get(name)
            observed = evidence.get(name)
            if expected is not None and observed != expected:
                raise trust_domain.TrustError(
                    f"release key does not match build manifest: {name}"
                )

        package_path = package_root / (
            f"firmware-{manifest.physical_board}-v{args.version}-{suffix}.bkpack"
        )
        package_report = package_domain.create(
            image_set=signed.image_set,
            member_names=member_names,
            sdk_evidence=sdk_evidence,
            trust_evidence=evidence,
            physical_board=manifest.physical_board,
            output=package_path,
            catalog_signer=lambda catalog: trust_domain.sign_catalog(
                catalog, args.mcuboot_key, args.openssl
            ),
            persistent_payload=persistent_payload,
            publication_verifier=lambda candidate: _verify_package_trust(
                candidate, args.openssl
            ),
        )
        _verify_package_trust(package_path, args.openssl)

        operator_report = None
        materialization = None
        if args.release_command == "full":
            flash_root = staging / "flash"
            flash_root.mkdir()
            operator_path = flash_root / (
                f"operator-{manifest.physical_board}-v{args.version}.bin"
            )
            operator_report = package_domain.materialize_full_image(
                package_path,
                args.base,
                args.base_sha256,
                operator_path,
            )
            immutable_tail = min(
                row.offset for row in manifest.layout.partitions
                if row.policy == "immutable"
            )
            materialization = {
                "accepted_base_sha256": args.base_sha256.lower(),
                "accepted_base_size": immutable_tail,
                "flash_end": immutable_tail,
                "flash_offset": 0,
                "flash_size": immutable_tail,
            }

        summary: dict[str, object] = {
            "build_manifest": {
                "path": "evidence/build-manifest.json",
                "sha256": _sha256_file(evidence_root / "build-manifest.json"),
            },
            "format": "bk7258.release/2",
            "generation": generation,
            "layout": {
                "identity": manifest.layout.identity,
                "sha256": manifest.layout.sha256,
            },
            "mode": args.release_command,
            "package": {
                "path": f"package/{package_path.name}",
                "sha256": package_report["sha256"],
            },
            "security": {
                key: evidence[key]
                for key in (
                    "bl1_public_fingerprint", "mcuboot_public_fingerprint"
                )
                if key in evidence
            },
            "target": {
                "board_family": "bk7258",
                "physical_board": manifest.physical_board,
            },
            "version": args.version,
        }
        if operator_report is not None:
            operator_path = Path(str(operator_report["output"]))
            summary["operator"] = {
                "path": f"flash/{operator_path.name}",
                "sha256": operator_report["sha256"],
                "size": operator_report["size"],
            }
            summary["materialization"] = materialization
        _release_summary(staging, summary)
        os.replace(staging, output)
    finally:
        if staging.exists():
            shutil.rmtree(staging)

    print(
        f"bk7258 release {args.release_command}: PASS "
        f"output={output} version={args.version} generation={generation}"
    )


def _build(args: argparse.Namespace) -> None:
    cp_config, ap_config, partition = _build_inputs(args)
    result = build_domain.build(
        REPOSITORY,
        _repository_input(cp_config),
        _repository_input(ap_config),
        _repository_input(partition),
        boot=args.boot,
        bl1_public_key=args.bl1_public_key,
        mcuboot_public_key=args.mcuboot_public_key,
        openssl=args.openssl,
        rollback_floor=args.rollback_floor,
        jobs=args.jobs,
        clean=args.clean,
    )
    print(f"bk7258 build: PASS layout={result.partition_identity}")
    print(f"bl1 elf={result.bl1.elf} bin={result.bl1.binary} map={result.bl1.map_file}")
    if result.bl2 is not None:
        print(
            f"bl2 elf={result.bl2.elf} bin={result.bl2.binary} "
            f"map={result.bl2.map_file} copy_size={result.bl2.copy_size}"
        )
    print(
        f"cp elf={result.cp.elf} bin={result.cp.binary} map={result.cp.map_file} "
        f"identity={result.cp.build_identity} "
        f"seed={result.cp.seed_defconfig_sha256} config={result.cp.resolved_config_sha256}"
    )
    print(
        f"ap elf={result.ap.elf} bin={result.ap.binary} map={result.ap.map_file} "
        f"identity={result.ap.build_identity} "
        f"seed={result.ap.seed_defconfig_sha256} config={result.ap.resolved_config_sha256}"
    )
    for row in result.artifacts:
        print(f"image {row.name}={row.path} size={row.size} sha256={row.sha256}")
    for name in result.preserved_external:
        print(f"preserve external={name}")
    print(f"build manifest={result.manifest}")


def _toolchain(args: argparse.Namespace) -> None:
    if args.toolchain_command == "install":
        report = toolchain_domain.install(
            REPOSITORY,
            args.archive,
            replace=args.replace,
        )
        print(
            f"bk7258 toolchain install: PASS root={report.root} "
            f"sha256={report.archive_sha256}"
        )
    else:
        report = toolchain_domain.verify(REPOSITORY)
        print(
            f"bk7258 toolchain verify: PASS root={report.root} "
            f"version={report.gcc_version!r} sha256={report.archive_sha256}"
        )


def _sdk(args: argparse.Namespace) -> None:
    if args.sdk_command == "list":
        selected = sdk_domain.manifest_sdk(REPOSITORY)
        print(f"sdk source={selected.path} revision={selected.revision} version={selected.version}")
        for row in sdk_domain.list_profiles(REPOSITORY):
            state = row.expected_tree_hash or "unaccepted"
            print(f"profile={row.name} role={row.role} tree={state} bundle={row.bundle}")
    elif args.sdk_command == "verify":
        row = sdk_domain.verify(REPOSITORY, args.profile)
        print(
            f"bk7258 sdk verify: PASS profile={row.profile} role={row.role} "
            f"files={row.files} tree={row.tree_hash}"
        )
    elif args.sdk_command == "install":
        row = sdk_domain.install(
            REPOSITORY, args.profile, args.bundle, replace=args.replace
        )
        print(f"bk7258 sdk install: PASS profile={row.profile} tree={row.tree_hash}")
    else:
        row = sdk_domain.rebuild(
            REPOSITORY,
            args.profile,
            args.source,
            build_domain.toolchain_root(REPOSITORY) / "bin",
            jobs=args.jobs,
            replace=args.replace,
        )
        print(f"bk7258 sdk rebuild: PASS profile={row.profile} tree={row.tree_hash}")


def _verify_package_trust(package: Path,
                          openssl: Path) -> dict[str, object]:
    evidence, layout, images, catalog, catalog_signature = \
        package_domain.trust_material(package)
    trust_domain.verify_signed_material(
        security=evidence,
        layout=layout,
        images=images,
        official_imgtool=(
            REPOSITORY.parent / "apps/boot/mcuboot/mcuboot/scripts/imgtool.py"
        ),
        openssl=openssl,
        catalog=catalog,
        catalog_signature=catalog_signature,
    )
    return evidence


def _package(args: argparse.Namespace) -> None:
    if args.package_command == "extract":
        output = package_domain.extract(args.package, args.output)
        print(f"bk7258 package extract: PASS output={output}")
        return
    if args.package_command == "flash-contract":
        import json
        print(json.dumps(package_domain.flash_contract(args.package),
                         sort_keys=True, separators=(",", ":")))
        return
    if args.package_command == "materialize":
        report = package_domain.verify(args.package)
        if not report["full_update"]:
            raise package_domain.PackageError(
                "materialization requires a signed full-update package"
            )
        _verify_package_trust(args.package, args.openssl)
        report = package_domain.materialize_full_image(
            args.package, args.base, args.base_sha256, args.output
        )
        print(
            "bk7258 package materialize: PASS "
            f"output={report['output']} size=0x{report['size']:x} "
            f"writes={report['writes']} sha256={report['sha256']}"
        )
        return
    manifest = build_domain.load_build_manifest(
        REPOSITORY, _workspace_input(args.build_manifest)
    )
    if manifest.format != build_domain.BUILD_MANIFEST_FORMAT:
        raise package_domain.PackageError(
            "diagnostic packaging requires a target-bound build manifest"
        )
    if manifest.boot != "direct":
        raise package_domain.PackageError(
            "unsigned diagnostic packaging requires a direct build manifest"
        )
    artifacts = image_domain.read_artifacts(manifest.finalized_artifacts)
    image_set = image_domain.finalized(
        manifest.layout,
        artifacts,
        preserved_external=manifest.preserved_external,
    )
    trust_evidence = trust_domain.unsigned().manifest()
    member_names = {
        name: path.name for name, path in manifest.finalized_artifacts.items()
    }
    sdk_evidence: dict[str, str] = {}
    for name in manifest.sdk_profiles:
        row = sdk_domain.verify(REPOSITORY, name)
        sdk_evidence[name] = row.tree_hash
    report = package_domain.create(
        image_set=image_set,
        member_names=member_names,
        sdk_evidence=sdk_evidence,
        trust_evidence=trust_evidence,
        physical_board=manifest.physical_board,
        output=args.output,
    )
    print(
        f"bk7258 package create: PASS output={report['package']} "
        f"sha256={report['sha256']}"
    )


def _verify(args: argparse.Namespace) -> None:
    if args.verify_command == "layout":
        row = layout_domain.load(_repository_input(args.partition))
        print(
            f"bk7258 verify layout: PASS identity={row.identity} "
            f"partitions={len(row.partitions)}"
        )
    elif args.verify_command == "image":
        selected_layout = layout_domain.load(_repository_input(args.partition))
        paths = {name: Path(value) for name, value in _pairs(args.artifact, "artifact").items()}
        artifacts = image_domain.read_artifacts(paths)
        result = image_domain.finalized(
            selected_layout,
            artifacts,
            preserved_external=tuple(args.preserve_external),
        )
        print(
            f"bk7258 verify image: PASS writes={len(result.writes)} "
            f"erases={len(result.erases)}"
        )
    elif args.verify_command == "build-manifest":
        result = build_domain.load_build_manifest(
            REPOSITORY, _workspace_input(args.manifest)
        )
        print(
            f"bk7258 verify build-manifest: PASS boot={result.boot} "
            f"board={result.physical_board} layout={result.layout.identity} "
            f"source={result.source}"
        )
    elif args.verify_command == "package":
        result = package_domain.verify(args.package)
        security = (
            "signed-evidence" if result["security"] == "signed"
            else result["security"]
        )
        print(
            f"bk7258 verify package: PASS images={result['images']} "
            f"board={result['physical_board'] or 'legacy-unbound'} "
            f"security={security} sha256={result['sha256']}"
        )
    else:
        evidence = _verify_package_trust(args.package, args.openssl)
        if evidence.get("mode") == "signed-ota":
            print("bk7258 verify trust: PASS public MCUboot CP/AP signatures")
        else:
            print("bk7258 verify trust: PASS public BL1/BL2/CP/AP signatures")


def main(argv: list[str] | None = None) -> int:
    parser = _parser()
    args = parser.parse_args(argv)
    try:
        if args.command == "build":
            _build(args)
        elif args.command == "toolchain":
            _toolchain(args)
        elif args.command == "sdk":
            _sdk(args)
        elif args.command == "package":
            _package(args)
        elif args.command == "release":
            _release(args)
        else:
            _verify(args)
    except (
        build_domain.BuildError,
        image_domain.ImageError,
        layout_domain.LayoutError,
        package_domain.PackageError,
        sdk_domain.SdkError,
        toolchain_domain.ToolchainError,
        trust_domain.TrustError,
        OSError,
        UnicodeError,
        ValueError,
    ) as error:
        print(f"bk7258: error: {error}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
