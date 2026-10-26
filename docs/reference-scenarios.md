# Reference scenarios

These scenarios define integration intent; they are not implemented tests in D0.

## Character publishes a post

An application validates UTF-8 text and facets, obtains an account session,
uploads optional media, creates one post, and records the returned StrongRef.
LLM generation, persona rules, and scheduling remain application concerns.

## Reply to a mention and continue a thread

The application reads a notification, fetches the thread, chooses a response,
and submits a reply with the original root and current parent StrongRefs. A
long thread uses `ThreadBuilder`; each step is reconciled independently and
partial success is visible during the process. Restart-safe recovery requires
the M4 durable journal/outbox capability described below.

## Publish an image

The application supplies bytes, MIME, dimensions, and alt text. The media
policy validates size/type, sanitizes metadata, uploads a blob, and attaches
the resulting reference to a post. No image/UI framework is required.

## Respond to a private message

The application routes through the Chat Service namespace and account scope,
not the public notification stream. Authorization and message delivery errors
are distinct from public AppView errors.

## Manage multiple accounts

Each account has an isolated identity, credential reference, rate/concurrency
budget, and optional persistence namespace. A refresh race is single-flight per
account; logout clears access to that account without affecting another.

## Resume after restart (M4 capability)

Once M4 durable journal/outbox support is implemented and configured, the
application stores operation state and stream checkpoints through the
backend-neutral persistence interface. On restart it reconciles unknown network
outcomes before retrying and treats replay-capable stream delivery as
at-least-once. Before that milestone, an application may provide its own
durability; D0 and M2 do not claim restart-safe recovery from the SDK.
