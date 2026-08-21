# BK7258 CP/AP configuration seeds

Each maintained role seed contains only:

- `defconfig`: application, physical-board capability and system policy;
- `profile.conf`: schema, physical board, role, runnable class, CP/AP
  compatibility and SDK profile.

Boot mode is not duplicated in the profile. Every build selects it explicitly:

```bash
tools/bk7258/bk7258.py build \
  --cp-config board/bk7258/configs/t5ai_core_cp_base \
  --ap-config board/bk7258/configs/t5ai_core_ap_base \
  --boot direct \
  --partition board/bk7258/partitions/bk7258/bk7258_ab_onchip_persistent.csv \
  --jobs 8
```

`--boot mcuboot` derives private build-local defconfigs with
`CONFIG_BK7258_MCUBOOT_IMAGE=y`; it does not require another pair of tracked
configuration directories. It additionally requires explicit BL1 and MCUboot
public PEM files, OpenSSL and a rollback floor.

Persistent storage is a system topology, not an application or board name:

- `CONFIG_BK7258_STORAGE_ONCHIP_PERSISTENT`;
- `CONFIG_BK7258_STORAGE_REMOVABLE_BLOCK`;
- `CONFIG_BK7258_STORAGE_FIXED_BLOCK`.

Exactly one topology across the resolved CP/AP pair must equal the selected
CSV's `STORAGE_TOPOLOGY`. Board bindings expose only electrical capability.
Applications consume the mounted storage service and do not select Flash
geometry, filesystems or cross-core transport.

Do not add product catalogs, generated full configs, boot-mode copies or
feature-specific profile directories. Add a persistent seed only for a real
board/role compatibility boundary.
