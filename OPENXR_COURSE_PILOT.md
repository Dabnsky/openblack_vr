# Proposal: an exercise-driven C++ OpenXR learning path

Status: pilot for approval, not a replacement for the current tutorial.
The working probe is unchanged. TODOs and solution code are instructional only.

## Document roles and starting point

- [The roadmap](STEAM_FRAME_VR_GUIDE.md)
  remains the architecture, platform-evidence, integration-risk, and status document.
- [The tutorial](OPENXR_TUTORIAL.md)
  remains the learning-path entry point. After approval, it can link to short
  exercise pages and separate solutions, retaining its existing explanations
  as reference material rather than deleting or rewriting them all at once.
- [The probe](src/VR/xr_probe.cpp) remains working
  C++17 code, not an exercise template. TODOs belong only in instructional
  snippets or a learner-owned copy.

Completed checkpoints stay completed: native build and seven Release tests;
desktop Vulkan/input through RDP X11; runtime/HMD discovery; Vulkan extension
and requirements; runtime-assisted Vulkan instance/device creation; selected
GPU/queue; graphics-bound session creation/cleanup; IDLE-to-READY observation;
PRIMARY_STEREO support; stereo dimensions/sample counts; OPAQUE/ALPHA_BLEND
enumeration. Optional review of these topics never blocks the next lesson.

The existing `SubmitEmptyFrame` helper is complete but uncalled.
`main` still invokes `ObserveSessionEvents`, which deliberately never begins
the session. Session begin/end and actual frame submission remain unverified.

Preserve these findings, without treating them as new test results:

| Evidence | Recorded finding |
| --- | --- |
| Runtime | SteamVR/OpenXR 2.17.10 |
| Graphics integration | Vulkan 1.0.0 through 1.2.0; probe requests 1.2 |
| GPU | Turnip Adreno (TM) 750, GPU Vulkan API 1.4.359 |
| Queue | Family 0, one graphics queue |
| Views | Two; recommended 1728x1728, maximum 8192x8192, samples 1 |
| Blend modes | OPAQUE and ALPHA_BLEND; probe requires OPAQUE |
| Loader | Explicit SteamVR ARM64 shared loader links successfully; default static-loader link failure remains incompletely diagnosed |
| Desktop boundary | RDP X11 works; no-display SSH selects unsupported KMSDRM; Wayland is not required for the desktop baseline |
| Open defects/limits | Desktop window-close crash undiagnosed; final SteamVR app launch route, headset rendering, tracking, and bgfx interop not established |

The roadmap has stale next-step wording for event observation and blend
enumeration. After approval, reconcile it with the tutorial's recorded
completed checks; do not rerun those as compulsory lessons or mark the entire
runtime-probe stage complete.

## Teaching structure reviewed

The [course welcome](https://rust-exercises.com/100-exercises/) describes short
concept sections paired with exercises, tests in companion source packages,
self-paced progression, and solutions kept separately.
Representative lessons reviewed:

- [Syntax](https://rust-exercises.com/100-exercises/01_intro/01_syntax):
  explain only enough new syntax for the next task.
- [While loops](https://rust-exercises.com/100-exercises/02_basic_calculator/06_while):
  introduce a narrow concept, show a diagnostic, and explain the correction.
- [Match](https://rust-exercises.com/100-exercises/05_ticket_v2/02_match)
  and its [companion exercise](https://github.com/mainmatter/100-exercises-to-learn-rust/blob/main/exercises/05_ticket_v2/02_match/src/lib.rs):
  focused TODO implementation with checks covering individual cases.

Adopt the general instructional pattern, not its prose, Rust examples, package
layout, website, or runner. All exercise content here is original C++ tailored
to this probe. Add progressive hints and explicit API/call-site explanations.
Unlike a from-scratch course, recognize already completed work. Unlike a
pure-language exercise, separate deterministic host tests from device evidence.

## Compact lesson outline

| Lesson | Status | Exercise / acceptance boundary |
| --- | --- | --- |
| 0. Platform and desktop baseline | Complete; optional review | Explain loader/runtime/version distinctions from recorded output; no mandatory rebuild |
| 1. Discovery and graphics requirements | Complete; optional review | Trace extension enabling, HMD discovery, and version-domain checks |
| 2. Vulkan binding and session creation | Complete; optional review | Trace ownership from instance to queue/session and reverse teardown |
| 3. Capabilities and event observation | Complete; optional review | Interpret stereo sizing, blend support, and IDLE/READY; do not repeat as prerequisites |
| 4. Begin, empty frames, stop | **Next: this pilot** | READY -> begin -> paced zero-layer frames -> request exit -> STOPPING -> end |
| 5. Spaces and predicted views | Future | Validate pose flags, locate views at predicted display time; distinguish math tests from head-tracking checks |
| 6. Swapchains and first stereo image | Future | Choose formats; acquire/wait/render/release; verify an image in each eye |
| 7. Controller actions and pose spaces | Future | Test action edges independently; verify bindings/tracking on-device |
| 8. bgfx/OpenXR ownership spike | Future, before engine integration | Prove device/image sharing and synchronization; no unrelated second device |
| 9. Optional engine integration | Future, gated by interop | Wire lifecycle/views/input at engine boundaries; preserve desktop behavior |
| 10. Interaction and comfort | Future | Controller ray, world scale/recenter, regression and sustained on-device checks |

Each future lesson follows: goal and credited prerequisites -> concept ->
focused starter -> exact placement/call site -> host checks -> device checks ->
progressive hints -> separate solution with block explanations and official
references. No chat or refactoring chronology belongs in the lessons.

---

# Pilot lesson 4: activate a session without rendering an image

## Goal and prerequisites

Implement one bounded activation of the existing session. Produce at least one
successful zero-layer frame, request exit after two seconds of running, observe
STOPPING, and end the session before normal resource teardown.

Use the already verified graphics setup unchanged. Do not add SDL, bgfx,
swapchains, reference spaces, view location, controller input, or Vulkan GPU
commands. A picture of OpenBlack is **not** an acceptance criterion.

## Concept: two lifecycles, one owner

Session creation only establishes a connection and graphics binding. READY
permits `xrBeginSession`; only a successful begin establishes a running session.
Running is not synonymous with visible or focused. Waiting for FOCUSED before
starting frames can prevent synchronization from completing.

Each frame is `xrWaitFrame` -> `xrBeginFrame` -> `xrEndFrame`. The existing
helper supplies the runtime's `predictedDisplayTime`, the verified OPAQUE
blend mode, and zero layers. It still completes this sequence when
`shouldRender` is false. Zero layers also remain valid when that flag is true:
this diagnostic intentionally supplies no application imagery.

Exit is asynchronous. `xrRequestExitSession` asks the runtime to stop; it does
not end the session. Continue polling and pacing frames until STOPPING, then
stop frame calls and invoke `xrEndSession`. Never call `xrEndSession` merely
because a timer expired, in IDLE, or as unconditional cleanup.

Use three different deadlines: five seconds to reach READY, two seconds from
successful begin before requesting exit, and five seconds after the request
to receive STOPPING. These are diagnostic policies, not OpenXR guarantees.
They are cooperative checks between API calls: `xrWaitFrame` has no timeout
parameter, so these are **not** a hard wall-clock bound on a stalled runtime.

## Work safely and place the code exactly

For this proposed exercise, use a learner-owned copy in a separate directory.
Do not modify or overwrite the working source or its existing executable.
The shared editor snapshot is read-only.

In that copy:

1. Keep `CheckXr`, `ProbeGraphics`, `ObserveSessionEvents`, and
   `SubmitEmptyFrame` intact. Existing chrono/thread headers are sufficient.
2. Insert the new function **after the closing brace of `SubmitEmptyFrame`
   and before `int main()`**. This makes the existing frame helper visible
   without another declaration.
3. In `main`, inside the existing `if (success)` immediately following
   `ProbeGraphics`, replace **only**
   `success = ObserveSessionEvents(instance, session);` with:

   ```cpp
   success = RunEmptyFrameSession(instance, session);
   ```

   Do not call both helpers: the new loop owns event consumption and beginning.
   Do not call the frame helper from `ProbeGraphics` or before READY/begin.
4. Leave cleanup and its accumulated failure result intact:
   session -> Vulkan device -> Vulkan instance -> OpenXR instance.

## Starter: fill the four lifecycle decisions

This is an intentionally incomplete **instructional snippet**, not a
replacement for working source. Remove `#error` only after implementing every
TODO. Preserve diagnostics on all failure paths; do not substitute `return true`.
The supplied skeleton handles event decoding; your work is session policy.

```cpp
bool RunEmptyFrameSession(XrInstance instance, XrSession session)
{
#error Complete lifecycle TODOs before building the learner copy
    using Clock = std::chrono::steady_clock;
    const auto readyDeadline = Clock::now() + std::chrono::seconds(5);
    auto runDeadline = Clock::time_point::max();
    auto stopDeadline = Clock::time_point::max();
    bool running = false;
    bool exitRequested = false;
    uint64_t frames = 0;

    for (;;)
    {
        for (;;)
        {
            XrEventDataBuffer event{};
            event.type = XR_TYPE_EVENT_DATA_BUFFER;
            const XrResult result = xrPollEvent(instance, &event);
            if (result == XR_EVENT_UNAVAILABLE)
            {
                break;
            }
            if (!CheckXr(instance, result, "xrPollEvent"))
            {
                return false;
            }
            if (event.type == XR_TYPE_EVENT_DATA_INSTANCE_LOSS_PENDING ||
                event.type == XR_TYPE_EVENT_DATA_EVENTS_LOST)
            {
                std::fprintf(stderr, "Lifecycle event stream lost or instance loss pending.\n");
                return false;
            }
            if (event.type != XR_TYPE_EVENT_DATA_SESSION_STATE_CHANGED)
            {
                std::printf("Other OpenXR event type: %d\n",
                            static_cast<int>(event.type));
                continue;
            }
            const auto& changed =
                reinterpret_cast<const XrEventDataSessionStateChanged&>(event);
            if (changed.session != session)
            {
                std::fprintf(stderr, "Unexpected session in state event.\n");
                return false;
            }
            std::printf("Session state changed: %d\n",
                        static_cast<int>(changed.state));

            // TODO 1: READY: guard against duplicate begin, initialize
            // XrSessionBeginInfo for PRIMARY_STEREO, check xrBeginSession,
            // handle XR_SESSION_LOSS_PENDING, then set running/runDeadline.

            // TODO 2: STOPPING: require running, check xrEndSession, clear
            // running, print completion evidence, and return whether the
            // requested exit and at least one frame actually completed.

            // TODO 3: EXITING/LOSS_PENDING: diagnose and fail this diagnostic.
            // IDLE/SYNCHRONIZED/VISIBLE/FOCUSED need no activation calls.
            // Reject unexpected states explicitly.
        }

        // TODO 4: when not running, enforce READY timeout and sleep 10 ms.
        // When running, request exit once after runDeadline; start stopDeadline.
        // Fail if STOPPING misses that deadline. Otherwise call the existing
        // SubmitEmptyFrame, fail on error, and increment frames only on success.
        // Print "First empty frame submitted." after the first completed frame.
    }
}
```

## Acceptance checks

### Hardware-independent

1. Build/type-check the completed learner copy as C++17 with
   `-Wall -Wextra -Wpedantic -Werror -pthread -fsyntax-only`. This needs installed
   headers, not an HMD, active runtime, game assets, or a working loader link.
   It does **not** prove runtime behavior.
2. Use ordinary C++ assertions and OpenXR function stubs, not a custom runner.
   The [separate solution](OPENXR_PILOT_SOLUTION.md) describes the reproducible
   host-test setup. The stub must control events, frame results, and call order:

   | Injected condition | Required result |
   | --- | --- |
   | IDLE, then READY | No frame before a checked begin; PRIMARY_STEREO supplied |
   | Running, no event available | Frames continue without requiring VISIBLE/FOCUSED |
   | `shouldRender = XR_FALSE`, then `XR_TRUE` | Both frames end; time copied exactly; zero layers/null layer pointer; OPAQUE |
   | `xrBeginFrame` returns `XR_FRAME_DISCARDED` | Still call `xrEndFrame` |
   | Run duration elapsed | Exactly one exit request; it does not itself end the session |
   | STOPPING after request | End once; no later frame; success requires a completed frame |
   | Begin/wait/begin-frame/end-frame/end-session/poll error | Diagnostic failure, no blind next frame |
   | Session/instance loss, EXITING before normal completion, wrong-session event, lost events | Diagnostic failure, not a passing frame milestone |
   | No READY for five seconds | Timeout failure with no begin or frame |
   | No STOPPING for five seconds after request | Timeout failure without an out-of-state end call |

   The tiny host test is not a simulator for every runtime state transition
   or scheduling condition.

### On-device only

From the learner-copy directory, preserve the verified loader route but choose
a different executable name:

```sh
g++ -std=c++17 -Wall -Wextra -Wpedantic -Werror -pthread \
  xr_probe.cpp -o xr_probe_lesson04 \
  /opt/steamvr/bin/linuxarm64/libopenxr_loader.so \
  -lvulkan \
  -Xlinker -rpath -Xlinker /opt/steamvr/bin/linuxarm64

./xr_probe_lesson04
printf 'Exit status: %s\n' "$?"
```

Expected evidence, **not yet an observed device result**:

- Existing runtime/GPU/capability findings still print.
- READY is followed by `Session begun with PRIMARY_STEREO.`
- `First empty frame submitted.` appears; the completion count is greater than zero.
- About two seconds after begin, `Session exit requested.` appears once.
- STOPPING is followed by `Session ended.` before the existing destruction logs.
- Exit status is zero; no failed OpenXR call or timeout was reported.
- Other intermediate states/order may vary. Do not require FOCUSED or a game
  picture; a runtime environment/loading presentation is compatible with zero
  submitted layers.

Also check runtime-initiated stop, delayed READY, missing STOPPING, and loss if
reproducible using supported device controls. Do not deliberately disconnect
critical system services or mutate system runtime files. An interrupted run
can demonstrate correct handling but does not pass the full milestone.

On errors, loss, or timeout, report failure and let `main` attempt its checked
resource teardown. Destruction is an abnormal escape path, not evidence of
orderly session end. Do not invent STOPPING or call `xrEndSession` in an illegal
state to make the logs look successful. This pilot does not implement
session recreation, restart after a runtime-initiated stop, or engine recovery.

## Progressive hints

<details>
<summary>Hint 1: identify permission versus ownership</summary>

READY permits a begin attempt. Set `running` only after a successful begin.
Do not set it when the event arrives, and do not test for FOCUSED to allow frames.

</details>

<details>
<summary>Hint 2: two similar-looking info structures</summary>

Use `XrSessionBeginInfo` / `XR_TYPE_SESSION_BEGIN_INFO` for activation.
The existing helper uses `XrFrameBeginInfo` for individual frames.
The session begin's `primaryViewConfigurationType` is PRIMARY_STEREO.

</details>

<details>
<summary>Hint 3: draining events must not starve the frame loop</summary>

`XR_EVENT_UNAVAILABLE` breaks the inner poll loop, not the outer session loop.
Sleep only when not running; `xrWaitFrame` supplies pacing while running.
After requesting exit, continue polling and framing until STOPPING.

</details>

<details>
<summary>Hint 4: distinguish a completed test from an interrupted session</summary>

STOPPING requires checked end. A runtime-initiated early stop can be handled
correctly while still failing this exercise's requested-exit criterion.
Positive status codes are not automatically ordinary success: explicitly
handle loss-pending results from lifecycle calls, and consume loss events
before another frame. The existing frame helper still pairs begin/end for
non-error statuses.

</details>

Stop here before opening the
[separate solution and block explanations](OPENXR_PILOT_SOLUTION.md).
Approval of this format precedes any broader tutorial migration.
