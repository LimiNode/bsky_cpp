# Authentication and identity contract

## Responsibility

Resolve DID/Handle identity, discover services, select PDS/AppView/Chat
endpoints, and attach credentials to an authenticated operation.

## Inputs and outputs

Inputs include a DID or Handle, discovery policy, credential reference, scopes,
and endpoint policy. Outputs include verified identity metadata, selected
service endpoint, session state, and refresh outcome. A CredentialStore is an
interface; implementations decide whether protected storage is available.

## Guarantees and errors

DID is the durable identity; Handle resolution is revalidated and never silently
replaces it. Discovery validates DID documents, HTTPS policy, host allow/deny
rules, redirects, token audience, and authenticated proxy boundaries. Errors
identify invalid identity, verification failure, unavailable service,
unauthorized/expired session, consent denial, and unsafe URL.

App Password sessions are scoped and revocable. AT Protocol OAuth requires the
profile's PKCE, PAR, DPoP, identity verification, scopes, and refresh semantics;
a generic OAuth2 helper is not sufficient.

## Concurrency and lifetime

Refresh is single-flight per account; concurrent callers observe the resulting
session or the same failure. Token values are move-only/opaque where possible,
redacted in diagnostics, and cleared on logout. CredentialStore calls are
asynchronous and cannot block callbacks.

## Integration boundary

Secret storage and crypto are injected. The SDK never assumes plaintext MDBX or
filesystem storage is safe. DM authorization uses its own service and scopes.
