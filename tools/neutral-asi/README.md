# Fixed neutral ASI and View identity diagnostic

The [third actual game log and rotation fix](../../docs/AC7-View-Cache-Rotation-Validation.md) confirm native owner matching after ReShade isolation, followed by the old lifetime-capacity stop. The fixed build retains16 recent View uploads and16 mapped records with conservative eviction; dropped data remains unknown until a complete upload. Rotation pressure and central/wide regressions passed. Keep the present environment and use the new local ASI; current observation requires no extra files.

The [first actual game log](../../docs/AC7-Game-Entry-Diagnostics.md) confirms initialization and both target shader creation hooks, but has no successful View association. Use the current local build and [GAME-TEST.md](GAME-TEST.md), without ZIP packaging. Observation accepts the old central rect and the widescreen patch's full view; full-view copying is explicitly refused with error13. Bounded one-shot diagnostics now identify context/CB/upload/layout/association failures. See the [original View/loader validation](../../docs/AC7-View-Identity-ASI-Validation.md) and [ordinary-device copy fix](../../docs/AC7-Neutral-ASI-Ordinary-Fix.md) for historical evidence.

## Build

Use the existing licensed MinHook source tree:

```text
cmake -S . -B build/two-frame-sampler -DAC7_MINHOOK_SOURCE_DIR=E:/CodingProject/AC8_MOZA/AC7-Ultrawide-Fix/third_party/minhook
cmake --build build/two-frame-sampler --config Release --target ac7-neutral-entry validate-neutral-entry validate-neutral-loader
```

Output: build/two-frame-sampler/Release/AC7NativeNeutral.asi. Distributions must include MinHook's LICENSE.txt. No new library download or game DLL replacement is required.

## Entry and ownership

DllMain only disables thread notifications. InitializeASI starts one worker, discovers ordinary/debug public SDK method addresses, installs bounded MinHook targets and pins the module. Shader/device ownership comes from the returned shader's GetDevice, not the callback argument. Stop retains callbacks and the owner until process exit; hot unload is unsupported. Callers must serialize the immediate context.

Two exact captured pixel shaders identify guide and prewarp DrawIndexed(3,6,0). CPU View data comes from successful Map/Unmap and full UpdateSubresource/UpdateSubresource1. Guide depth/velocity/View/viewport must match prewarp depth/View/viewport and the full current View bytes. CopyResource/CopySubresourceRegion/CopySubresourceRegion1 invalidate affected View data; unknown ExecuteCommandList invalidates caches. These are public SDK slots, not engine-private ABI or Present/draw-counter frame sources.

Processing uses one private deferred context and ExecuteCommandList(TRUE), with command-list/scene retention until the retirement fence completes. A qualified real View frame is consumed even when busy, preventing a late same-frame copy. Eight qualified frames stop the trial; in default-off mode they only observe. Zero-jitter 5120×1440 allocation, two known View rectangles and bounded16-entry recent-upload/mapped tracking windows are intentional limits, not lifetime object limits. Full-width5120 View is observation-only; the existing active copy still requires the central2560 layout. Diagnostic reasons/context ownership are one-shot, max32 notes within the overall64-line log.

## Controls and first game check

Follow [GAME-TEST.md](GAME-TEST.md). First copy only the ASI, without AC7NativeNeutral.enable. Existing loader/mod DLLs stay in place. A unique max64-line status log appears beside the ASI; no image/complete View dump or model calls. After first logs qualify actual identity/startup, the optional empty startup marker permits up to8 neutral applications. It is read once at startup. No automatic deployment or game launch.

The status ABI is version2 with size checking. AC7NeutralUseView(BOOL) controls the real-View protocol in independent hosts. AC7NeutralSetFrame(BOOL,uint64_t) remains for the older explicit-token test matrix; it is not the game's frame source and cannot replace an already consumed View stream. AC7NeutralStop never revives. AC7NeutralGetStatus polls retirement without waiting.

## Independent tests

```text
validate-neutral-entry PREWARP_DXBC FRAME0_SCENE NEW_OUTPUT ASI --no-debug --view-identity
validate-neutral-entry PREWARP_DXBC FRAME0_SCENE NEW_OUTPUT ASI --debug --view-identity
validate-neutral-entry PREWARP_DXBC FRAME0_SCENE NEW_OUTPUT ASI --debug --view-disabled
validate-neutral-entry PREWARP_DXBC FRAME0_SCENE NEW_OUTPUT ASI --debug --view-wide
validate-neutral-entry PREWARP_DXBC FRAME0_SCENE NEW_OUTPUT ASI --debug --view-wide-guard
validate-neutral-entry PREWARP_DXBC FRAME0_SCENE NEW_OUTPUT ASI --debug --view-pending
validate-neutral-entry PREWARP_DXBC FRAME0_SCENE NEW_OUTPUT ASI --no-debug
validate-neutral-loader ISOLATED_DINPUT8_DLL PREWARP_DXBC GUIDE_DXBC NEW_REPORT
python tests/d3d11/check_view_identity.py CAPTURE_DIRECTORY VIEW_EVIDENCE_ROOT NEW_REPORT
```

View tests locate the other seven scenes/View files beside FRAME0_SCENE and the exact guide shader beside PREWARP_DXBC. They use captured data in a synthetic host, never an AC7-derived replay. Loader testing copies the installed loader only into its isolated host directory; the distribution package contains no loader DLL. All prior failed experiments/binaries and early diagnostic sources remain in their artifact directories.

No NGX/model/FG/true-upscale, game performance or active game-copy acceptance is implied by these checks.
