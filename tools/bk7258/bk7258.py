#!/usr/bin/env python3
"""The sole maintainer-facing BK7258 build, SDK, package, and verify entry."""

from __future__ import annotations

import argparse
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
    build.add_argument("--cp-config", type=Path, required=True)
    build.add_argument("--ap-config", type=Path, required=True)
    build.add_argument("--boot", choices=("direct", "mcuboot"), required=True)
    build.add_argument("--partition", type=Path, required=True)
    build.add_argument("--jobs", type=int, required=True)
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

    package = commands.add_parser("package", help="create or extract a delivery package")
    package_commands = package.add_subparsers(dest="package_command", required=True)
    create = package_commands.add_parser("create", help="create one deterministic package")
    create.add_argument("--partition", type=Path, required=True)
    create.add_argument("--artifact", action="append", required=True, metavar="NAME=PATH")
    create.add_argument("--member", action="append", required=True, metavar="NAME=BASENAME")
    create.add_argument("--sdk-profile", action="append", required=True)
    create.add_argument("--preserve-external", action="append", default=[],
                        metavar="NAME")
    security = create.add_mutually_exclusive_group(required=True)
    security.add_argument("--unsigned", action="store_true")
    security.add_argument("--signed", action="store_true")
    security.add_argument("--ota-apps", action="store_true",
                          help="create pending signed CP/AP OTA images only")
    create.add_argument("--bl1-key", type=Path)
    create.add_argument("--mcuboot-key", type=Path)
    create.add_argument("--bl1-elf", type=Path)
    create.add_argument("--bl2-elf", type=Path)
    create.add_argument("--openssl", type=Path)
    create.add_argument("--version")
    create.add_argument("--security-counter", type=lambda value: int(value, 0))
    create.add_argument("--bl1-security-counter", type=lambda value: int(value, 0))
    create.add_argument("--output", type=Path, required=True)
    extract = package_commands.add_parser("extract", help="extract a verified package")
    extract.add_argument("--package", type=Path, required=True)
    extract.add_argument("--output", type=Path, required=True)

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


def _build(args: argparse.Namespace) -> None:
    result = build_domain.build(
        REPOSITORY,
        _repository_input(args.cp_config),
        _repository_input(args.ap_config),
        _repository_input(args.partition),
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
        f"seed={result.cp.seed_defconfig_sha256} config={result.cp.resolved_config_sha256}"
    )
    print(
        f"ap elf={result.ap.elf} bin={result.ap.binary} map={result.ap.map_file} "
        f"seed={result.ap.seed_defconfig_sha256} config={result.ap.resolved_config_sha256}"
    )
    for row in result.artifacts:
        print(f"image {row.name}={row.path} size={row.size} sha256={row.sha256}")
    for name in result.preserved_external:
        print(f"preserve external={name}")


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


def _package(args: argparse.Namespace) -> None:
    if args.package_command == "extract":
        output = package_domain.extract(args.package, args.output)
        print(f"bk7258 package extract: PASS output={output}")
        return
    selected_layout = layout_domain.load(_repository_input(args.partition))
    artifact_paths = {name: Path(value) for name, value in _pairs(args.artifact, "artifact").items()}
    signed_inputs = (
        args.bl1_key, args.mcuboot_key, args.bl1_elf, args.bl2_elf,
        args.openssl, args.version, args.security_counter,
        args.bl1_security_counter,
    )
    if args.signed:
        if any(value is None for value in signed_inputs) or args.preserve_external:
            raise trust_domain.TrustError(
                "signed package requires all explicit key/ELF/version/counter inputs "
                "and cannot preserve a release artifact"
            )
        toolchain = build_domain.toolchain_root(REPOSITORY) / "bin"
        release = trust_domain.signed_release(
            layout=selected_layout,
            artifacts=artifact_paths,
            bl1_private_key=args.bl1_key,
            mcuboot_private_key=args.mcuboot_key,
            bl1_elf=args.bl1_elf,
            bl2_elf=args.bl2_elf,
            version=args.version,
            security_counter=args.security_counter,
            bl1_security_counter=args.bl1_security_counter,
            official_imgtool=(
                REPOSITORY.parent / "apps/boot/mcuboot/mcuboot/scripts/imgtool.py"
            ),
            openssl=args.openssl,
            objcopy=toolchain / "arm-none-eabi-objcopy",
            nm=toolchain / "arm-none-eabi-nm",
        )
        image_set = release.image_set
        trust_evidence = release.evidence.manifest()
    elif args.ota_apps:
        required = (
            args.mcuboot_key, args.bl2_elf, args.openssl,
            args.version, args.security_counter,
        )
        forbidden = (
            args.bl1_key, args.bl1_elf, args.bl1_security_counter,
        )
        if any(value is None for value in required) \
                or any(value is not None for value in forbidden) \
                or args.preserve_external:
            raise trust_domain.TrustError(
                "apps-only OTA requires MCUboot key/BL2/version/counter inputs "
                "and forbids BL1 or preserved-release inputs"
            )
        toolchain = build_domain.toolchain_root(REPOSITORY) / "bin"
        release = trust_domain.signed_ota_pair(
            layout=selected_layout,
            artifacts=artifact_paths,
            mcuboot_private_key=args.mcuboot_key,
            bl2_elf=args.bl2_elf,
            version=args.version,
            security_counter=args.security_counter,
            official_imgtool=(
                REPOSITORY.parent / "apps/boot/mcuboot/mcuboot/scripts/imgtool.py"
            ),
            openssl=args.openssl,
            objcopy=toolchain / "arm-none-eabi-objcopy",
        )
        image_set = release.image_set
        trust_evidence = release.evidence.manifest()
    else:
        if any(value is not None for value in signed_inputs):
            raise trust_domain.TrustError(
                "unsigned package must not receive signing inputs"
            )
        artifacts = image_domain.read_artifacts(artifact_paths)
        image_set = image_domain.finalized(
            selected_layout,
            artifacts,
            preserved_external=tuple(args.preserve_external),
        )
        trust_evidence = trust_domain.unsigned().manifest()
    member_names = _pairs(args.member, "member")
    sdk_evidence: dict[str, str] = {}
    for name in args.sdk_profile:
        row = sdk_domain.verify(REPOSITORY, name)
        sdk_evidence[name] = row.tree_hash
    report = package_domain.create(
        image_set,
        member_names,
        sdk_evidence,
        trust_evidence,
        args.output,
        catalog_signer=(
            (lambda catalog: trust_domain.sign_catalog(
                catalog, args.mcuboot_key, args.openssl
            )) if args.ota_apps else None
        ),
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
    elif args.verify_command == "package":
        result = package_domain.verify(args.package)
        security = (
            "signed-evidence" if result["security"] == "signed"
            else result["security"]
        )
        print(
            f"bk7258 verify package: PASS images={result['images']} "
            f"security={security} sha256={result['sha256']}"
        )
    else:
        evidence, layout, images, catalog, catalog_signature = \
            package_domain.trust_material(args.package)
        trust_domain.verify_signed_material(
            security=evidence,
            layout=layout,
            images=images,
            official_imgtool=(
                REPOSITORY.parent / "apps/boot/mcuboot/mcuboot/scripts/imgtool.py"
            ),
            openssl=args.openssl,
            catalog=catalog,
            catalog_signature=catalog_signature,
        )
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
