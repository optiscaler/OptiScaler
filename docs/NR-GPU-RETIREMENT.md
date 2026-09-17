# NR GPU resource lifetime

A Cyberpunk session reached 34.03 GB VRAM use as NR time rose from roughly 4 to 23 ms. Logs showed unresolved recordings at teardown, supporting memory pressure as a contributor without attributing every allocation to NR.

Resources retire only after recordings close and submitted fences complete:

- A private COM notification closes destroyed command-list recordings without retaining/dereferencing the list. Destruction alone does not prove GPU completion.
- Replaced owners keep receiving notifications. NR-owned presentation lists close at retirement; replayable game lists still require reset/destruction and completion.
- Codec-only shaders register for submissions; finished-picture captures share parent tracking. Drained parents release discarded private generations even without a written timestamp. Hooks precede feature-creation recording.

Notifications use stable owner snapshots and defer destruction. Child retirement is allowed; unresolved teardown work survives until process exit.

## Reentrant destruction

A Cyberpunk access violation was traced to retired-vector compaction. Calling NGX destruction inside `std::erase_if` could re-enter hooks and mutate that vector.

The collector now removes completed callbacks into a separate batch before invoking them. Nested collection is guarded; later batches drain callback-created retirements once. An idle query cannot allow tracker destruction while callbacks are active.

The production WARP regression covers 64 destroyed unsubmitted lists, blocked submitted work, later reclamation, replay, wrapped identities, multiple queues and reentrant retirement of 64 callbacks. The old collector fails that reentrant case. These checks do not prove long-session VRAM stability; see [game limits](NR-UPSTREAM-REVIEW.md).

## Concurrent command-list reset

Starfield's v0.8.0 crash report identifies a null recording entry in `ResetRecording`. The ordinary buffer-transition path could mutate the tracker outside the state lock used by reset notifications, including after NR initialization failed.

Tracker operations now serialize internally. Buffer creation/transitions, codec dispatch and frame-hold entry also follow registry -> state -> tracker lock order. No GPU wait was added. A four-worker WARP test races recording/reset/retirement against collection and checks all 8,000 callbacks run exactly once; it crashes before the fix and passes afterward. Starfield itself remains untested locally. Its separate `0xBAD0000B` NR initialization error is not resolved by this fix.
