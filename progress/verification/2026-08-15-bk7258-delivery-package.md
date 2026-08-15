# BK7258 additive delivery package verification

- Date: 2026-08-15
- Base: `eecfc7dda46d2f2eefb2af59c67cc96028eb41d9`
- Branch: `agent/bk7258-bkpack`
- Status: host `CODE_PASS`; fresh build and hardware download not run

## Contract

The [openvela platform guide](https://doc.openvela.com/document?id=602&version=dev&language=cn)
identifies `libarch.a`, `libboards.a`, and the final runtime image as normal
build artifacts; its build example names the runtime output `vela_ap.bin`.
This dual-core port retains the two archives in the NuttX build and exposes
logical role images as `vela_cp.bin` and `vela_ap.bin`. They are not
CRC-expanded Flash payloads.

`bootloader.bin` and `bl2.bin` are Beken boot-chain artifacts.
`firmware.bkpack` is a ZIP-compatible Beken delivery extension, not an
official openvela package format. It contains no static archives, loader or
private key and does not sign or access hardware.

## Implemented

- Signed dual builds with embedded BL1 manifests add a deterministic payload
  container while retaining all existing directory artifacts. The optional
  raw-manifest-page mode fails closed instead of emitting an incomplete
  Windows Flash plan.
- Raw and signed builds expose byte-exact CP/AP logical aliases.
- The container verifies member names, sizes, SHA-256 values, exact Flash
  plans, public trust contract, and factory layout before creation and again
  before extraction.
- `WINDOWS_FLASH.txt` gives `apps`, `normal`, and explicitly destructive
  `factory` `bk_loader.exe` arguments with `<PORT_NUMBER>`/`<BAUD>`
  placeholders; for example, Windows COM9 is passed as `-p 9`.
  It requires target public-root preflight and forbids directly flashing the
  logical `vela_cp.bin`/`vela_ap.bin` files.
- The factory manifest now includes both BL2 Flash segments in its own loader
  argument list instead of relying on downloader-side reconstruction.

## Evidence

```text
python3 -m py_compile ...                                  PASS
bash -n board/bk7258/scripts/build_dual_image.sh           PASS
python3 -m unittest -v \
  board/bk7258/tests/test_bk7258_bkpack.py \
  board/bk7258/tests/test_bk7258_bkpack_container.py \
  board/bk7258/tests/test_bk7258_trust_chain.py            PASS (16)
git diff --check                                           PASS
```

A host-only repack of an existing signed 18.6.80 directory produced a
container with 30 payload members plus its manifest that passed creation and
deep verification:

```text
SHA-256 8fe38a44ba1b63e9bd3805214eb9331e19c2a4e4182784f98dc131c4275fc0a1
plans   apps, normal, factory
trust   authenticated=false, target_preflight_required=true
```

The generated Windows plan names only the CRC-expanded Flash files and the
manifest-owned offsets. No Flash, serial port, J-Link, signing key, network,
or hardware was accessed.

## Not yet proven

- A clean full dual-image build with this exact source revision.
- Extraction with Windows 7-Zip and a real manual Beken-loader invocation.
- Hardware boot or runtime behavior of a package produced by this revision.
- Native Linux/macOS download support; this phase deliberately does not add
  it.
