# External dependencies

Approved managed dependencies are added as flat submodules under
`external/<dependency>/`. The core `bsky_cpp` target remains buildable without
optional transport, event, or persistence integrations.

The opt-in `bsky_cpp::runtime` component uses the direct `event-hub-cpp`
submodule, pinned to `e3580057a0273e7004cd4fd91ad9ae7d74d4c5b3` (MIT).
Initialize only that dependency with
`git submodule update --init external/event-hub-cpp`, without recursive checkout.
See the [runtime contract](../docs/contracts/OPERATION_RUNTIME.md) for lifetime
and managed timed-awaiter guarantees. The pinned snapshot includes upstream
PR #13, which aggregates queued awaiter deadlines and wakes loops after
awaiter registration.
