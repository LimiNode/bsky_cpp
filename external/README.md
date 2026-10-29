# External dependencies

Approved managed dependencies are added as flat submodules under
`external/<dependency>/`. The core `bsky_cpp` target remains buildable without
optional transport, event, or persistence integrations.
