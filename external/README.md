# External dependencies

Approved managed dependencies are added as flat submodules under
`external/<dependency>/`. The core `bsky_cpp` target remains buildable without
optional transport, event, or persistence integrations.

The opt-in `bsky_cpp::runtime` component uses the direct `event-hub-cpp`
submodule, pinned to `2d4b09763603db2e5f9c5ca5bce7de3ffc9332b0` (MIT).
Initialize only that dependency with
`git submodule update --init external/event-hub-cpp`, without recursive checkout.
See the [runtime contract](../docs/contracts/OPERATION_RUNTIME.md) for lifetime
and managed timed-awaiter limitations; upstream PR #13 is not integrated.
