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

The syntax fixtures are vendored from upstream commit
`54f54ea831f37fe9f722844177f7e1bed8ae8bdc` under
[`tests/fixtures/upstream`](../../tests/fixtures/upstream). The deterministic
CID fixtures are constructed in `tests/identifiers.cpp`.

The fixture test currently reports the following exact valid/invalid totals:

| Type | Fixture cases passed |
| --- | ---: |
| DID | 24 / 24 valid, 18 / 18 invalid |
| Handle | 71 / 71 valid, 48 / 48 invalid |
| NSID | 25 / 25 valid, 27 / 27 invalid |
| Record key | 16 / 16 valid, 11 / 11 invalid |
| AT URI | 23 / 23 valid, 72 / 72 invalid |

The aggregate is 335 / 335 cases. Fixture files are treated as syntax
acceptance data; CID and StrongRef semantics have additional focused tests.

## Public types

- `Did::parse()` accepts syntactically valid `did:<method>:<id>` values. Method
  support is reported separately; `plc` and `web` are the methods recognized by
  this release. DID values remain case-sensitive and are not resolved.
- `Handle::parse()` validates DNS-style labels, rejects the UI `@` prefix and
  an all-numeric final label, and stores an ASCII-lowercase canonical form.
- `Nsid::parse()` validates a three-or-more-component, DNS-label-style
  collection name.
- `RecordKey::parse()` preserves case, validates the protocol's restricted
  character set, and rejects `%`, `.` and `..` path controls.
- `AtUri::parse()` accepts `at://` URIs with a DID or handle authority, an
  optional NSID collection, an optional record key, and optional non-empty
  query/fragment components. Userinfo, escaped authorities, trailing/extra
  path segments, and empty query/fragment components are rejected.
- `Cid::parse()` structurally decodes CIDv0 and CIDv1 multibase forms. CIDv1
  varints and the multihash length are checked before canonicalizing to
  lowercase base32. `is_atproto_record()` recognizes CIDv1 + dag-cbor and
  `is_atproto_blob()` recognizes CIDv1 + raw; both require sha2-256 with a
  32-byte digest, and `is_atproto()` covers either form.
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
