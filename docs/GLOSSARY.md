# Glossary

| Term | Meaning in this SDK |
| --- | --- |
| AT Protocol | Federated protocol family used by Bluesky services. |
| XRPC | JSON/CBOR request and response convention for Lexicon-defined queries and procedures. |
| Lexicon | Schema and namespace language defining records and XRPC operations. |
| DID | Decentralized identifier; stable account identity. |
| Handle | Human-readable, changeable account name resolved to a DID. |
| AT URI | `at://` identifier for a repository record. |
| CID | Content identifier for a particular record version. |
| StrongRef | Pair of an AT URI and CID identifying an exact record version. |
| PDS | Personal Data Server; authoritative repository host for an account. |
| AppView | Service aggregating and presenting application-oriented views. |
| Chat Service | Service and Lexicon namespace for private messaging. |
| DID document | Signed/discoverable document describing DID verification and services. |
| App Password | Scoped credential intended for automation or clients that do not use OAuth. |
| AT Protocol OAuth | OAuth profile with identity verification, PAR, PKCE, DPoP, scopes, and refresh. |
| Facet | Rich-text annotation expressed using UTF-8 byte ranges. |
| Blob | Content-addressed uploaded binary referenced by a record. |
| Cursor | Opaque position used to resume a paginated or streaming source. |
| Checkpoint | Persisted consumer progress used for replay/recovery. |
| Outbox | Durable queue of operations awaiting submission or reconciliation. |
| Delivery | Completion of a network operation or observation of a stream event; not necessarily user-visible push. |
