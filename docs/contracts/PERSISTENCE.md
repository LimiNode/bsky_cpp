# Persistence contract

## Responsibility

Persist optional account session metadata, stream checkpoints, processed event
identifiers, operation journal entries, durable outbox items, and cache values.

## Inputs and outputs

The backend-neutral interface uses namespaced keys, versioned values, atomic
compare-and-set where needed, and explicit expiry. It returns typed not-found,
conflict, unavailable, corruption, and protected-storage errors.

## Guarantees and errors

In-memory operation is valid and is the default for tests. Durable stores must
document crash consistency, encryption/key management, migration, retention,
and recovery. Tokens/secrets are never persisted unless an explicit protected
credential policy says how they are encrypted and rotated. An outbox entry is
not proof of server acceptance; reconciliation remains necessary.

## Concurrency and lifetime

Backends are safe for their documented concurrent operations and expose atomic
checkpoint/outbox transitions. Values are copied or owned by the call; no
returned view outlives the backend operation. Shutdown flushes or abandons
pending writes according to a visible policy.

## Integration boundary

MDBX is the preferred future adapter, but it is optional. Custom and future
SQLite adapters implement this contract without entering core DTOs.
