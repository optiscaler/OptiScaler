# What to do next, and what has already been settled

Written 2026-09-02, after a strategy note planned on top of three things that are not true. All three
came from reading comments and old notes rather than the code, so this file states the current
position first and the ideas second.

**If you are planning work on this module, read "Already settled" before proposing anything.**

---

## Already settled — do not plan on these

**There is no edit accumulator.** Temporal filtering of the model's answer was built and measured as a
dead end twice, once with a trained DLAA pass, and removed. The comment describing it survived the
removal and has now been corrected; `DispatchPass`'s `InPrevEdit` parameter is vestigial, fed
`nullptr` by every caller. The reason it failed is not incidental: the model re-decides its detail
with the framing, so an old answer does not belong to a new frame, and reprojecting it moves where the
disagreement lands rather than removing it. The composition is re-anchored to the model every frame,
and that is what makes it steady.

**The forwarder-free path has been tried.** `FORWARDER_INVESTIGATION.md` has the log. The proxy route
is already past the snippet's caller check and fails later at `0xBAD0000B FAIL_UnableToInitializeFeature`
during feature creation. The warm-up-retry theory is disproven — twenty attempts over ~1.3 s in a fully
warm core, all failing. Three theories remain untried and are listed there. It is not one call.

**OkLab hue preservation and the AP1 clamp are ported.** `dlssnr.hlsl`, `HueOkLab` and `ClampAp1`, used
in the transfer. The note listing them as outstanding predates the port.

**Nothing in the resolve operates per channel.** That was the green-drift lesson: any per-channel clamp
or curve is a latent hue distorter, because the smallest channel of a saturated pixel reaches the bound
first and an achromatic edit lands as a colour shift. `HueOkLab`, the two-sided guard and
`CubeScaleResidual` all take a single scalar and apply it to the whole triple. Keep it that way.

**Reprojecting the picture is abandoned.** Reflex 2 / frame warp was built, measured at 46% camera-age
reduction, and dropped on structural limits. Its branch and tag are deleted.

---

## What I would do, in order

### 1. Bind `DLSSNR.ControlMask` — highest value per hour on the list

Never bound. Zero references in the tree. It is in NVIDIA's own recovered parameter set *with
subrects*, which means it is a per-evaluate resource rather than a create-time latch — so unlike
Intensity or Style it could follow the scene.

If it is what its name and shape suggest, a per-pixel edit-strength map consumed inside the model, it
hands us in one input the things currently faked downstream or written off:

- UI exclusion, which today is positional only — the pass runs before the HUD is drawn, and a detector
  was measured and abandoned (a static HUD pixel scored 0.31 on the "did not change" test, separation
  from the world 2.5:1, not a detector at any threshold)
- highlight and shadow protection, currently applied after the model rather than inside it
- skin exclusion, pruned explicitly as *"unfixable per-pixel without the model's internal mask, which
  NGX does not expose"* — this may be exactly that door

**Cost to find out: three evaluates.** Bind a constant 1.0, a constant 0.0, then a half-screen ramp, and
look. Do this before anything architectural, because a working ControlMask changes what the amortiser
and the subrect ideas should even be.

### 1b. Measured: model resolution is a weaker lever than it looks

Enshrouded, 1920x1080, same scene and camera, Vulkan through the bridge:

| model resolution | NR cost |
|---|---|
| 100% | 4.10 ms |
| 50%  | 2.24 ms |

Four times fewer model pixels bought **1.83x**, not 4x. Fitting `cost = fixed + k x pixels` to those two
points gives roughly **1.6 ms of fixed cost per evaluate** and 2.5 ms that scales -- so about 40% of the
pass at full resolution is barriers, descriptors, staging copies and cubin launch, none of which care
how big the picture is.

Two consequences:

- **Model resolution saturates.** 25% would be about 1.8 ms, barely better than 50%'s 2.24 -- while the
  Swin grid gets twice as coarse. Past halfway the trade stops paying.
- **Skipping evaluates dominates shrinking them.** A skipped frame removes the fixed cost too, which
  shrinking never does. This is the argument for temporal amortisation over the subrect ideas, and it is
  now measured rather than reasoned.

It also caps what any of this can do for pre-Blackwell cards running a rebuilt binary: resolution alone
is at best a ~2.3x lever, not the ~4x a pixel count suggests.

### 2. Measure churn as a function of motion

The 22%-re-decided-per-frame figure is real but it was measured on a **static** scene, and every
amortisation argument rests on it. Re-measure with the capture path that already exists, under a slow
pan and under moderate motion.

If it is 22% static and 30% panning, amortisation is nearly free. If it is 60% under motion, a fixed
2:1 schedule will crawl and only an adaptive form is worth building. This is a couple of captures and a
diff, and it decides whether item 4 is a project or a dead end.

### 3. Async compute

NGX evaluate takes a command list, so NR could run on a separate compute queue and overlap the game's
raster work instead of serialising into it. Hiding some of a tensor-heavy pass behind a copy- or
shadow-heavy phase is realistic and costs no image quality.

Two obstacles, one of which is new and not obvious:

- the injection point is inside the game's command list at DLSS evaluate time, so moving off it means a
  separate list, a fence, and an edit that arrives a frame late
- **the pass now runs inside `ScopedNrStateEnvelope`**, which turns root-signature tracking off, skips
  heap capture and restores the game's compute state on exit. On bindless engines that restore is what
  keeps the game alive — 007 First Light device-removed without it. A separate queue has its own state
  and does not need the envelope, but the interaction has to be worked out deliberately rather than
  discovered.

---

## Amortisation — the idea, and the honest case against it

Skip evaluates and reproject the *edit* between them, rather than reprojecting the picture.

**Why it is not the abandoned proposal.** Frame warp reprojected the picture, where an error is a hole,
a smear or a torn HUD. This reprojects a bounded, small-amplitude field whose failure mode is "no
enhancement on these pixels for one frame" — and zero edit is bit-for-bit the original frame by
construction of the compositor. That asymmetry is real and it is the strongest argument for the idea.

**What it would need, none of which exists today:**

- **An accumulator.** Built and removed twice. This is the cost the strategy note missed by reading a
  stale comment: it is not reusing machinery, it is rebuilding something that failed, with a different
  justification. The justification is genuinely different — filtering for stability versus reprojecting
  to enable skipping — but it starts from zero and against a prior result.
- **Chained motion vectors.** The model keeps internal history; it takes depth, motion and Reset for a
  reason. Shown every second frame, the motion between the frames it sees is the composition of two
  frames' vectors. Feeding single-frame vectors at half rate understates motion — the same class of bug
  as passing 1.0 where the game meant 1920, just milder.
- **A validity test to replace rectification.** History is rectified toward the current edit; on a
  skipped frame there is no current edit. A depth test plus a local luma-consistency check, fading the
  edit toward zero where it fails. Fade to zero, never hold: stale edit over a disocclusion is the crawl
  that coring already failed to suppress.

**The counter-case, stated plainly.** NR's output is detail, which is high-frequency by definition.
Reprojecting a high-frequency field with game motion vectors at reduced rate is the recipe that produced
edge crawl before, and coring failed against that class of artifact because the churn amplitude equals
the detail amplitude — a threshold only relocates the noise. Amortisation could reintroduce it under a
new name. Item 2 is what tells you which way it goes, and it comes first.

**Honest ceiling.** Halving evaluates saves half of NR's cost, not half the frame. At the D3D12 ~3.6 ms
readout in a 16.6 ms frame that is ~11%. It becomes material at 4K, on the Vulkan cost figure, or inside
the RR+NR stack (~4.5 + 6.1 + 3.5 ms), which the notes call the biggest remaining prize and which is not
shippable at that cost. Per-evaluate overhead — barriers, descriptors, cubin launch — does not scale with
pixels, so skipping evaluates saves more than shrinking them, which argues for temporal skipping over the
subrect ideas.

**Two adjacent pieces worth having regardless:**

- **Static-camera hold.** Zero camera delta and negligible motion → skip and hold the edit. Menus, photo
  mode, cutscene holds, paused frames. Trivially detectable, guaranteed correct, and the honest lower
  bound of the whole idea. Worth doing even if full amortisation never happens.
- **Spatial subrects.** `DLSSNR.*SubrectBase/Width/Height` exist per resource, so a rotating half-screen
  evaluate is expressible. Expect it to fail — a Swin-block transformer will seam at tile borders, and
  moving the subrect each frame probably invalidates the model's internal history. But pinning a fixed
  half-width subrect and looking at the border is ten minutes, and the answer is worth owning.

**The synthesis worth keeping.** Async compute and cross-adapter offload both hand you an edit that is a
frame late, which is normally disqualifying — and an amortiser is exactly the machine that consumes a
late edit correctly. If amortisation works, getting NR off the critical path stops being a latency
problem and becomes a bandwidth one, and the freed budget buys higher Intensity, full resolution, or the
RR+NR stack. Quality bought with schedule rather than silicon.

---

## Closed

**iGPU offload.** The model is hand-written sm_120-only cubins, FP8 E4M3, no PTX. There is no portable
representation of the weights, so it cannot execute on an Intel or AMD iGPU by any means. A different
device would not improve quality either — the model is the model.

**Offloading the surrounding passes** (proxy encode, the 64×64 meter, the resolve) to another device:
the transfers cost more than the work.
