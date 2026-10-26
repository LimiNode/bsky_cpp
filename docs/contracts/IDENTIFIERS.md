# AT Protocol identifier contract

M1a implements the value-level syntax and canonicalization rules needed by
later transport and XRPC layers. These types validate strings; they do not
perform DNS lookups, DID resolution, repository discovery, or authorization.

The acceptance anchors used for this milestone are the official specifications
reviewed on 2026-10-26:

- [DID](https://atproto.com/specs/did)
- [Handle](https://atproto.com/specs/handle)
- [AT URI scheme](https://atproto.com/specs/at-uri-scheme)
- [Record keys](https://atproto.com/specs/record-key)
- [Data model and CIDs](https://atproto.com/specs/data-model)
- [Lexicon / NSID names](https://atproto.com/specs/lexicon)

The repository does not vendor a generated schema snapshot in M1a; the
deterministic CID fixtures are constructed in `tests/identifiers.cpp`, and the
URLs above are the pinned acceptance anchors for this batch.

## Public types

- `Did::parse()` accepts syntactically valid `did:<method>:<id>` values. Method
  support is reported separately; `plc` and `web` are the methods recognized by
  this release. DID values remain case-sensitive and are not resolved.
- `Handle::parse()` validates DNS-style labels, rejects the UI `@` prefix and
  an all-numeric final label, and stores an ASCII-lowercase canonical form.
- `Nsid::parse()` validates a three-or-more-component, DNS-label-style
  collection name.
- `RecordKey::parse()` preserves case, validates allowed characters and
  percent escapes, and rejects escaped path/query/fragment controls.
- `AtUri::parse()` accepts `at://` URIs with a DID or handle authority, one
  NSID collection, and an optional record key. Query, fragment, userinfo,
  escaped authorities, and extra path segments are rejected.
- `Cid::parse()` structurally decodes CIDv0 and CIDv1 multibase forms. CIDv1
  varints and the multihash length are checked before canonicalizing to
  lowercase base32. `is_atproto()` is true only for CIDv1 + dag-cbor +
  sha2-256 with a 32-byte digest.
- `StrongRef::create()` owns an AT URI/CID pair and requires a record URI and
  an AT Protocol CID.

All parsers return `Result<T>` with `ErrorCategory::InvalidArgument` on
malformed input. Length limits are defensive implementation limits, not a
replacement for protocol validation.

## Deliberately deferred

Identity resolution, DNS/TLS verification, DID document processing, endpoint
selection, XRPC serialization, and network policy belong to later milestones.
The types do not claim that a syntactically valid identifier exists or is
authorized.
