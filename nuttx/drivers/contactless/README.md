# ISO-DEP activation

`CL_ISODEP` builds the independent `isodep.c` protocol module. The current
entry point activates a selected Type A target, advertises FSD 64, validates
ATS lengths and optional fields, and applies the advertised startup guard time.
All activation failures release the caller-owned RF field. The transport is a
callback interface and has no dependency on MFRC522, BK7258 or Android.

Activation parsing follows [ISO/IEC 14443-4:2018, section 5](https://cdn.standards.iteh.ai/samples/73599/1c24245bb3cb4a749fdd467b0c68acc9/ISO-IEC-14443-4-2018.pdf)
and [NXP AN12057](https://www.nxp.com/docs/en/application-note/AN12057.pdf).
The host test covers all FWI/SFGI combinations, all FSCI values, missing optional
fields, opaque historical bytes, transport errors and interrupted guard waits.
APDU exchange now supports both directions of chaining, bounded retransmission,
WTX within a caller deadline and failure cleanup. The AP service's `bknfc hce`
command activates a selected target and selects the Shaniu phone AID. It reports
presence only for the exact protocol-version response. No UID or APDU payload
crosses RPMsg, and the command cannot grant ownership. Physical phone acceptance
is still required. `MFRC522IOC_SET_RF` allows the adapter to restart the field
before selection and drop it on exit; a subsequent scan re-enables the field.
