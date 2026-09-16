# Shaniu eye assets

`shaniu-default-v1.json` is the reviewable source for the first 160x160 dual-eye
set.  It contains logical expressions and left/right behavior only.  It must not
contain framebuffer numbers, GPIOs, buses, or the physical left/right mapping;
those remain properties of the AIDK board layer.

The generated `shaniu-eye-pack-v1` file is a product asset for the soldered SD
NAND exposed by NuttX as `/dev/mmcsd0`.  It is not linked into the AP firmware
and generated `.bkep` or preview files are not checked into Git.

## Build and inspect

Run the repository-owned public workflow from the repository root.  The CLI
also resolves `--source` relative to that root when invoked through an absolute
script path from another working directory:

```sh
python3 tools/bk7258/bk7258.py package eye-pack \
  --source app/bk7258/assets/display/shaniu-default-v1.json \
  --output out/shaniu-display/shaniu-default-v1.bkep \
  --preview-dir out/shaniu-display/previews

python3 tools/bk7258/bk7258.py verify eye-pack \
  --package out/shaniu-display/shaniu-default-v1.bkep
```

The builder is deterministic and refuses to replace an existing output.  The
pack uses a bounded table of contents, RGB565 palette, indexed 160x160 frames,
optional RLE8 storage, per-entry CRC32, whole-payload CRC32, and source/package
SHA-256 identities.  These checks detect corruption; version 1 does not yet
claim publisher authenticity or replace the signed firmware release chain.

## Initial SD NAND provisioning contract

The intended FAT layout is:

```text
/shaniu/display/packs/shaniu-default-v1.bkep
/shaniu/display/staging/
/shaniu/display/active.json
```

For initial factory/developer provisioning, keep `/dev/mmcsd0` unmounted on the
AP, run `usbmode msc`, copy the verified pack through the host-mounted disk,
safely eject that disk, and then run `usbmode cdc`.  Never mount the FAT volume
on the AP while USB MSC owns it.  The display service holds a block-device lease
for each short AP mount, so `usbmode msc` can transiently return `-EBUSY`; retry
the command rather than forcing either owner.

The AIDK AP runtime now waits for `/dev/mmcsd0`, `/dev/fb0`, and `/dev/fb1`,
mounts the FAT volume only while leased, fully validates the selected pack, and
renders `neutral` identically to both displays.  If `active.json` is absent, it
accepts only the verified default filename above.  A staging install is checked
before it is renamed into `packs`; the active marker is fsynced through a
temporary file and the successful unmount supplies the final filesystem flush.
On NuttX FAT, replacement rename removes the previous marker before installing
the new one, so a power loss can deliberately fall back to the default pack; it
must not select a partially copied custom pack.

The transport-neutral runtime API exposes expression, install, activate, and
status calls.  The CP operator adapter is available as `bkdisplay status` and
`bkdisplay mood <expression>` over the dedicated `bkdisplay-v1` RPMsg endpoint;
the phone/Gateway protocol is still not installed.  No physical left/right
mapping is claimed yet.  Until board calibration establishes that mapping, only
shared expressions are accepted and both fitted panels receive the same image.

## Palette and logical states

Palette indexes are stable within the source: background, sclera, cyan iris,
pupil, highlight, blush, speaking amber, error red, offline shell, cyan glow,
and muted blue.  The baseline states are `neutral`, `happy`, `shy`, `sad`,
`surprised`, `thinking`, `listening`, `speaking`, `offline`, and `error`.

Shared frames may request horizontal mirroring for the logical right eye.
Side-specific frames are also supported, but a source must provide both `left`
and `right` variants for a state so an incomplete face cannot be packaged.
