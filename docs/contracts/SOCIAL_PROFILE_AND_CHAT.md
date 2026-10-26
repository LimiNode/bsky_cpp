# Social graph, profile, notifications, and chat contract

## Responsibility

Expose typed record-backed social actions, service-level moderation actions,
profile reads/patches, notifications, and private messages.

## Inputs and outputs

Record-backed operations (`like`, `unlike`, `follow`, `unfollow`, `block`,
`unblock`, `repost`, and `unrepost`) accept a target record/identity and, for
reversal, the authenticated caller's own record URI or StrongRef. Their
creation/deletion and conflict behavior follows the corresponding Lexicon
record. Service-level operations (`muteActor` and `unmuteActor`) accept the
target actor DID and do not require a caller-owned record URI. Profile updates
use patch semantics and preserve unknown fields unless a field is explicitly
cleared. Chat requests use the dedicated chat Lexicon namespace and return
typed conversations/messages.

## Guarantees and errors

Reversal of record-backed actions identifies the authenticated account's own
record; a target DID alone is insufficient. Mute/unmute is the exception: the
target DID is the service-level identifier. Public replies/notifications are
separate from private chat. Permission, moderation, stale version, and service
routing errors are distinguishable. Polling is not described as push delivery.

## Concurrency and lifetime

Account-scoped mutations are serialized when required to avoid duplicate record
creation. Profile patch conflict behavior is explicit (etag/version or a
protocol-defined equivalent). Message DTOs own their text and attachments.

## Integration boundary

AppView, PDS, and Chat endpoints are selected by discovery and policy. Chat
credentials and data never enter a public notification stream.
