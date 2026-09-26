# NFC binding store contract (S39, 2026-09-27)

This is the persistence component for NFC-02, not a working scene-card UI or
an authorized product operation. Both NFC worker and focus owner actually
run on AP; direct AP ownership will be used. S38's optional internal CP sample
protocol is not required for scene execution. No new worker or timer is enabled.

One filesystem worker owns a zero-initialized bindings context. Its root is
a caller-owned private directory on the existing internal configuration
filesystem; no mount, format, implicit directory creation or SD fallback.
The future product adapter must use its fixed user-data namespace, never a
client-supplied path, and must implement power/reset drain before activation.
An existing directory with no active file is empty; malformed records or I/O
failures are errors, never empty/default data. Cached lookup performs no I/O.

Eight slots are a bounded storage capacity, not a timing threshold. Each stores
a complete 4/7/10-byte UID and a nonzero focus duration in milliseconds. UID
matches are size+bytes (SAK is validated metadata, not ownership). Duplicate
UIDs across slots are rejected. No reset, owner, credential, arbitrary action
or shell operation is expressible. Duration is passed to the existing focus
service, whose start/overflow/admission contracts still apply when integrated.

The 200-byte bundle is NCB1 (4 bytes), four zero reserved bytes, then eight
24-byte slots: size/SAK/UID[10], BE64 duration, four zero bytes. An empty slot
is all zero. Tail bytes beyond UID length are zero. Independent golden fixtures
come from this layout. The store's existing SCF1 checksum/durable-revision
transaction envelope is retained. A 16-byte transaction is NCB1, action u8
(1=set,2=remove), slot u8, two zeros and a nonzero BE64 operation ID.

Set/remove require expected revision. Exact last operation retries are
idempotent; reused operation IDs with different content conflict. Old revisions
cannot overwrite newer records. Only a successful durable commit updates
cached bindings and revision. A prepublication failure retains old data. An
unknown publication outcome latches EINPROGRESS, disabling lookup and writes;
a readable new file does not resolve that barrier within the same owner.
A fresh process may load the selected valid durable record. Reset handling
must clear this namespace only under the existing authorized reset transaction.

S39 implements this storage contract and real filesystem tests only. Authentication
routing, asynchronous worker jobs, cancellation/exit acknowledgements, App UI,
card enrollment, ambient dwell/reentry and actual focus dispatch are NOT_WIRED.
No software gap is reclassified as merely waiting for a board.
