# FAT allocation errors

Baseline: OpenVela NuttX `76354c637858ecb0aa4601629327acb6f44a26bb`.
License: Apache-2.0, inherited from the patched sources.

`0001-preserve-fat-allocation-errors.patch` returns ENOSPC when cluster
allocation reports no free clusters and preserves negative transport errors.
Invalid existing chains remain EIO; they must not be misreported as full.
Both allocation branches in fat_get_sectors are covered. Apply only in the
isolated NuttX validation tree; the official checkout remains untouched.

`0002-drain-block-writes-before-sync-unmount.patch` calls the standard
`BIOC_FLUSH` after FAT file/metadata synchronization and before unmount closes
the block reference. Flush failure keeps the mount available for cleanup retry.
The existing MMCSD 0003/0004 patches wait for TRAN plus READY_FOR_DATA.
AIDK therefore uses journaled UnQLite file sync without directory descriptors,
which NuttX FAT rejects with EISDIR. File fsync flushes FAT directory metadata;
successful unlink flushes its directory entry, and unmount drains the final card
write. Unsupported block flush ioctls retain the existing driver contract.
Hardware power-cut recovery is not asserted by this source change.

`0003-support-fat-open-file-path.patch` retains the open relative path in the
FAT file allocation, copies it on dup, and implements `FIOC_FILEPATH` with
bounded mount-path joining. Both the NuttX file-lock implementation and the
pinned UnQLite VFS require this ioctl; mountpoints have no generic fallback.
The database and journal use stable names while open; renaming an open FAT
database is not supported by this integration.
