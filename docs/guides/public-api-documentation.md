# Public API documentation guide

Before exposing a type or operation, answer: who owns each value, when can it
be destroyed, what executor invokes completion, which errors are typed, what
can be retried, how cancellation and shutdown behave, and what the server does
or does not guarantee. Link to the relevant contract and Lexicon operation.

Use conceptual snippets only when marked `Conceptual; not implemented in D0`.
Never present a mock or pseudocode type as an available header.
