# Documentation governance

## Normative hierarchy

Current AT Protocol and Lexicon specifications define wire truth. The contract
documents define the SDK's public behavior. ADRs record design choices and their
trade-offs. Guides define contributor practice. Roadmap and status describe
scope and evidence, not API behavior.

When sources conflict, record the conflict as an open question, link the
authoritative source, and do not hide an assumption in a normative statement.

## Contract requirements

Every public operation documents responsibility, inputs, outputs, typed errors,
timeouts, cancellation, retries, concurrency, ownership/lifetime, side effects,
and integration boundaries. Security-sensitive behavior additionally documents
redaction, storage, audience, and rotation.

## Change control

Changes to identity, authentication, delivery, persistence, public naming, or a
dependency boundary require an ADR or an update to the relevant ADR. A contract
change updates `IMPLEMENTATION_STATUS.md` and at least one acceptance test plan.

## Link and freshness checks

Reviewers run `git diff --check`, verify every relative Markdown link, search for
stale terminology, and compare protocol claims with current official
specifications. External links are references, not a substitute for a local
contract. Examples must be marked conceptual until they compile.

## Review gates

1. Scope gate: docs-only work does not introduce production code or dependencies.
2. Boundary gate: optional integrations remain optional and transport types do
   not leak into DTOs.
3. Safety gate: credentials, redirects, retries, offsets, and partial progress
   have explicit semantics.
4. Evidence gate: implementation status is backed by files and tests.
5. Release gate: a reviewer confirms Definition of Done before a draft PR is
   marked ready; D0 is never merged without independent review.
