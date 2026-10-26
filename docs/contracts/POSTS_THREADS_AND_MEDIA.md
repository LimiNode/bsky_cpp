# Posts, threads, rich text, and media contract

## Responsibility

Provide typed post/reply/quote/repost/delete, thread retrieval/building,
rich-text facets, and extensible media upload operations.

## Inputs and outputs

Post input contains UTF-8 text, facets, optional embeds, and labels. A reply
requires both `root` and `parent` StrongRefs. `ThreadBuilder` stores ordered
drafts and publishes each record independently. Results identify successful and
unresolved steps; no multi-record atomicity is promised. Media inputs include
bytes/stream, declared MIME, dimensions, and alt text; outputs are validated
blob references and metadata.

## Guarantees and errors

UTF-8 byte offsets for facets are validated independently from code-point and
grapheme counts. MIME, size, alt-text, and image metadata policy follows the
current Lexicon. Create operations are not invisibly retried. Delete and edit
semantics reflect the protocol; the SDK does not promise native mutation where
the server only supports a new record.

Errors identify invalid facet/rich text, missing StrongRef, unsupported media,
upload failure, authorization, rate limit, and partial publication.

## Concurrency and lifetime

Draft builders are single-owner unless explicitly synchronized. Publication may
run bounded concurrent steps, preserving dependency order for replies. Uploaded
buffers are consumed or copied according to the input contract and never
referenced after completion.

## Integration boundary

Media storage is an AT blob service behind the transport seam. Image sanitizing
and decoding are replaceable policies; no UI or image framework is required.
