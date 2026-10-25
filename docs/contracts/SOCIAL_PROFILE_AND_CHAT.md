# Social graph, profile, notifications, and chat contract

## Responsibility

Expose typed like/unlike, follow/unfollow, block/unblock, mute/unmute,
repost/unrepost, profile reads/patches, notifications, and private messages.

## Inputs and outputs

Mutating operations accept a target identity/record and, for reversal, the
identifier of the caller's own record. Profile updates use patch semantics and
preserve unknown fields unless a field is explicitly cleared. Chat requests use
the dedicated chat Lexicon namespace and return typed conversations/messages.

## Guarantees and errors

Unlikes, unfollows, and unblocks identify the authenticated account's own
record; a target DID alone is insufficient. Public replies/notifications are
separate from private chat. Permission, moderation, stale version, and service
routing errors are distinguishable. Polling is not described as push delivery.

## Concurrency and lifetime

Account-scoped mutations are serialized when required to avoid duplicate record
creation. Profile patch conflict behavior is explicit (etag/version or a
protocol-defined equivalent). Message DTOs own their text and attachments.

## Integration boundary

AppView, PDS, and Chat endpoints are selected by discovery and policy. Chat
credentials and data never enter a public notification stream.
