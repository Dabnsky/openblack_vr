# Pilot lesson 4: separate solution

Read the [exercise](OPENXR_COURSE_PILOT.md) first. This is proposed teaching code,
not an implementation installed into the working probe.

## Completed lifecycle owner

Insert this **after the existing `SubmitEmptyFrame` and before `main`**, in the
learner copy only. Keep that existing frame helper unchanged.
Replace the observation call in `main` with
`success = RunEmptyFrameSession(instance, session);` as specified in the exercise.

```cpp
bool RunEmptyFrameSession(XrInstance instance, XrSession session)
{
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

            switch (changed.state)
            {
            case XR_SESSION_STATE_READY:
            {
                if (running)
                {
                    std::fprintf(stderr, "Duplicate READY while session is running.\n");
                    return false;
                }
                XrSessionBeginInfo beginInfo{};
                beginInfo.type = XR_TYPE_SESSION_BEGIN_INFO;
                beginInfo.primaryViewConfigurationType =
                    XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO;
                const XrResult begun = xrBeginSession(session, &beginInfo);
                if (!CheckXr(instance, begun, "xrBeginSession"))
                {
                    return false;
                }
                if (begun == XR_SESSION_LOSS_PENDING)
                {
                    std::fprintf(stderr, "Session loss pending during begin.\n");
                    return false;
                }
                running = true;
                runDeadline = Clock::now() + std::chrono::seconds(2);
                std::puts("Session begun with PRIMARY_STEREO.");
                break;
            }
            case XR_SESSION_STATE_STOPPING:
            {
                if (!running)
                {
                    std::fprintf(stderr, "STOPPING without a running session.\n");
                    return false;
                }
                const XrResult ended = xrEndSession(session);
                if (!CheckXr(instance, ended, "xrEndSession"))
                {
                    return false;
                }
                if (ended == XR_SESSION_LOSS_PENDING)
                {
                    std::fprintf(stderr, "Session loss pending during end.\n");
                    return false;
                }
                running = false;
                std::puts("Session ended.");
                std::printf("Completed empty frames: %llu\n",
                            static_cast<unsigned long long>(frames));
                if (!exitRequested || frames == 0)
                {
                    std::fprintf(stderr, "Session stopped before diagnostic completion.\n");
                    return false;
                }
                return true;
            }
            case XR_SESSION_STATE_EXITING:
            case XR_SESSION_STATE_LOSS_PENDING:
                std::fprintf(stderr, "Runtime exit or session loss interrupted diagnostic.\n");
                return false;
            case XR_SESSION_STATE_IDLE:
            case XR_SESSION_STATE_SYNCHRONIZED:
            case XR_SESSION_STATE_VISIBLE:
            case XR_SESSION_STATE_FOCUSED:
                break;
            default:
                std::fprintf(stderr, "Unexpected session state: %d\n",
                             static_cast<int>(changed.state));
                return false;
            }
        }

        const auto now = Clock::now();
        if (!running)
        {
            if (now >= readyDeadline)
            {
                std::fprintf(stderr, "Timed out waiting for READY.\n");
                return false;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
            continue;
        }
        if (!exitRequested && now >= runDeadline)
        {
            const XrResult requested = xrRequestExitSession(session);
            if (!CheckXr(instance, requested, "xrRequestExitSession"))
            {
                return false;
            }
            if (requested == XR_SESSION_LOSS_PENDING)
            {
                std::fprintf(stderr, "Session loss pending during exit request.\n");
                return false;
            }
            exitRequested = true;
            stopDeadline = Clock::now() + std::chrono::seconds(5);
            std::puts("Session exit requested.");
            continue;
        }
        if (exitRequested && now >= stopDeadline)
        {
            std::fprintf(stderr, "Timed out waiting for STOPPING after exit request.\n");
            return false;
        }
        if (!SubmitEmptyFrame(instance, session))
        {
            return false;
        }
        ++frames;
        if (frames == 1)
        {
            std::puts("First empty frame submitted.");
        }
    }
}
```

## Block explanations

### 1. Time and local ownership

`steady_clock` measures elapsed time without wall-clock adjustments. The READY
deadline starts on entry; the run deadline starts only after successful begin;
the STOPPING deadline starts only after successful exit request. An inactive
deadline starts at `time_point::max()` so it cannot expire accidentally.

`running` records an API outcome, not the latest visibility state.
`exitRequested` prevents repeated requests and distinguishes the intended
diagnostic completion from an external early stop. `frames` counts completed
helper calls, not wait attempts or GPU submissions.

These deadlines cannot interrupt blocking OpenXR calls or an indefinitely
nonempty event stream. They prevent indefinite application polling under normal
API progress, not a hung runtime. A production watchdog/recovery design is
outside this lesson.

### 2. Drain and decode events

Each poll gets a fresh buffer with its input type initialized. Only
`XR_EVENT_UNAVAILABLE` ends the drain; it is not an error and contains no event
to decode. A frame is attempted only after the queued events have been handled.

Check the event type before interpreting the storage as a session-state event,
then check its session handle. Other non-lifecycle events remain visible in
diagnostics. Lost events fail this narrow probe because its lifecycle knowledge
can no longer be trusted. Session/instance loss is not silently ignored and is
not implemented as automatic recreation.

### 3. Begin only at READY

`XrSessionBeginInfo` is zero-initialized; its `type` identifies the structure and
its null `next` indicates no extension chain. `primaryViewConfigurationType`
uses the PRIMARY_STEREO configuration already checked during setup.

A duplicate READY while running is an invalid lifecycle assumption, not a
reason to begin twice. A failed begin never enables the frame path.
`CheckXr` accepts all non-error codes, so a loss-pending success status needs an
additional explicit policy: log it and fail this diagnostic instead of
announcing ordinary activation.

### 4. End only at STOPPING

No frame is open at event-processing boundaries: the existing frame helper is
single-threaded and completes its begin/end sequence before returning normally.
STOPPING therefore permits `xrEndSession` without another frame operation.
After checked end, return immediately; do not wait for FOCUSED, restart on READY,
or require an EXITING event merely to destroy an ended session.

An unsolicited STOPPING is handled by ending the running session but reports an
incomplete exercise unless the planned exit was requested and a frame completed.
A future application can choose restart/recovery behavior; this diagnostic
tests one activation only.

On loss/errors/timeouts, returning false lets the existing `main` attempt
resource destruction. `xrDestroySession` is distinct from `xrEndSession`:
destruction is allowed as an abnormal teardown path, and does not demonstrate
normal STOPPING/end behavior. Never fabricate a state transition or call end
unconditionally in `main`.

### 5. Request exit, then keep servicing the runtime

The two-second duration starts at successful activation, not process startup.
The exit request is checked before marking it requested. `continue` returns to
polling immediately; if STOPPING is not yet queued, subsequent iterations still
submit empty frames. A five-second grace deadline detects lack of shutdown
progress. Neither deadline authorizes an out-of-state `xrEndSession` call.

The loop deliberately does not require visibility/focus. Beginning frames is
part of how the runtime synchronizes a session; gating frames on FOCUSED can
prevent the transition being awaited.

### 6. Reuse the existing frame helper

There is no second empty-frame implementation. In the existing helper:

| Block | Explanation |
| --- | --- |
| `XrFrameWaitInfo`, `XrFrameState` | Initialize both input and output structure types; wait once for timing |
| `xrWaitFrame` | Runtime pacing and predicted display timestamp; not a sleep chosen by the app |
| `XrFrameBeginInfo`, `xrBeginFrame` | Begin the frame after the checked wait |
| `displayTime` | Copy `frameState.predictedDisplayTime` exactly; do not substitute chrono time |
| OPAQUE | Already enumerated for PRIMARY_STEREO; not guessed from the desktop window |
| `layerCount = 0`, `layers = nullptr` | Explicitly submit no application imagery; no swapchain/GPU commands needed |
| `xrEndFrame` | Complete the frame even if rendering was not requested |

`XR_FRAME_DISCARDED` is a non-error begin result and still requires an end
call for the current frame. It is not an expected target of this correctly
paired single-threaded loop. A negative result stops normal submission; do
not fabricate a successful end after a failed begin or retry frames blindly.

The existing boolean helper does not return positive status codes to its
caller. It still pairs frame calls during the loss grace period; a session
loss event is queued before subsequent session calls return
`XR_SESSION_LOSS_PENDING`, so the lifecycle owner handles it on the next event
drain, before starting another frame. If grace-period calls fail instead, the
helper reports failure and stops the loop. Do not infer that `CheckXr` itself
implements loss policy, or that this helper proves recovery from a lost session.

### 7. Preserve cleanup and engine boundaries

`main` owns the objects; the new function borrows handles. Normal success means
session end precedes the existing destroy sequence. No application GPU commands
were submitted, so this lesson introduces no Vulkan fence/device-idle policy.

OpenBlack's `Game::Run` currently draws one camera through
`RendererInterface::DrawScene` and advances bgfx through `Frame`.
`RendererInterface::Create` initializes bgfx using SDL native window/display
handles. None of this lesson wires OpenXR into that loop or proves those
graphics resources can be shared. That remains the later interop gate.

## Reproducible hardware-independent test setup

No website, runner, new test framework, or repository test target is required.
In a temporary local C++ translation unit, include:

```cpp
#include <openxr/openxr.h>
#include <cassert>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <initializer_list>
#include <thread>
```

Then paste the existing `CheckXr` and `SubmitEmptyFrame` definitions and the
completed `RunEmptyFrameSession` above, followed by the ordinary C++ stub tests
below. Compile/run with:

```sh
g++ -std=c++17 -Wall -Wextra -Wpedantic -Werror -pthread \
  lifecycle_test.cpp -o lifecycle_test
./lifecycle_test
```

Do **not** link an OpenXR loader for this test. All called OpenXR functions are
defined by these stubs. No real runtime, HMD, Vulkan object, or fake pointer
dereference is involved. Assertions must remain enabled (no `-DNDEBUG`).
The successful case takes roughly two seconds because it exercises the real
elapsed-time exit policy. Timeout cases take five and seven seconds respectively;
the full suite takes roughly eighteen seconds. The stub waits sleep briefly to
avoid busy-spinning.

```cpp
namespace
{
enum class Scenario
{
    Normal, BeginError, WaitError, FrameBeginError, FrameEndError,
    EndError, PollError, RequestError, WrongSession, SessionLoss,
    InstanceLoss, EventsLost, EarlyExit, EarlyStop, BeginLoss, NoReady, NoStop
};
Scenario scenario;
int token;
XrSession testSession = reinterpret_cast<XrSession>(&token);
int pollStep;
int phase;
int completed;
int begins;
int ends;
int requests;
int framesAtRequest;
bool stoppingSent;
bool terminalSent;
std::chrono::steady_clock::time_point beganAt;
std::chrono::steady_clock::time_point requestedAt;

void Reset(Scenario next)
{
    scenario = next;
    pollStep = phase = completed = begins = ends = requests = framesAtRequest = 0;
    stoppingSent = terminalSent = false;
}
}

XRAPI_ATTR XrResult XRAPI_CALL xrResultToString(
    XrInstance, XrResult, char buffer[XR_MAX_RESULT_STRING_SIZE])
{
    std::strcpy(buffer, "injected test result");
    return XR_SUCCESS;
}

XRAPI_ATTR XrResult XRAPI_CALL xrPollEvent(XrInstance, XrEventDataBuffer* out)
{
    if (scenario == Scenario::NoReady)
    {
        return XR_EVENT_UNAVAILABLE;
    }
    if (scenario == Scenario::PollError)
    {
        return XR_ERROR_RUNTIME_FAILURE;
    }
    XrSessionState state = XR_SESSION_STATE_UNKNOWN;
    if (pollStep == 0)
    {
        state = XR_SESSION_STATE_IDLE;
    }
    else if (pollStep == 1)
    {
        state = XR_SESSION_STATE_READY;
    }
    else if (!terminalSent && completed > 0)
    {
        terminalSent = true;
        if (scenario == Scenario::InstanceLoss || scenario == Scenario::EventsLost)
        {
            out->type = scenario == Scenario::InstanceLoss
                ? XR_TYPE_EVENT_DATA_INSTANCE_LOSS_PENDING
                : XR_TYPE_EVENT_DATA_EVENTS_LOST;
            return XR_SUCCESS;
        }
        if (scenario == Scenario::SessionLoss) state = XR_SESSION_STATE_LOSS_PENDING;
        if (scenario == Scenario::EarlyExit) state = XR_SESSION_STATE_EXITING;
        if (scenario == Scenario::EarlyStop) state = XR_SESSION_STATE_STOPPING;
    }
    if (scenario != Scenario::NoStop &&
        state == XR_SESSION_STATE_UNKNOWN && requests > 0 &&
        completed > framesAtRequest && !stoppingSent)
    {
        stoppingSent = true;
        state = XR_SESSION_STATE_STOPPING;
    }
    if (state == XR_SESSION_STATE_UNKNOWN)
    {
        return XR_EVENT_UNAVAILABLE;
    }
    ++pollStep;
    XrEventDataSessionStateChanged event{};
    event.type = XR_TYPE_EVENT_DATA_SESSION_STATE_CHANGED;
    event.session = scenario == Scenario::WrongSession ? XR_NULL_HANDLE : testSession;
    event.state = state;
    std::memcpy(out, &event, sizeof(event));
    return XR_SUCCESS;
}

XRAPI_ATTR XrResult XRAPI_CALL xrBeginSession(
    XrSession session, const XrSessionBeginInfo* info)
{
    assert(session == testSession && begins == 0 && phase == 0);
    assert(info->type == XR_TYPE_SESSION_BEGIN_INFO && info->next == nullptr);
    assert(info->primaryViewConfigurationType == XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO);
    ++begins;
    beganAt = std::chrono::steady_clock::now();
    if (scenario == Scenario::BeginError) return XR_ERROR_RUNTIME_FAILURE;
    if (scenario == Scenario::BeginLoss) return XR_SESSION_LOSS_PENDING;
    return XR_SUCCESS;
}

XRAPI_ATTR XrResult XRAPI_CALL xrWaitFrame(
    XrSession session, const XrFrameWaitInfo* info, XrFrameState* state)
{
    assert(session == testSession && begins == 1 && ends == 0 && phase == 0);
    assert(!stoppingSent);
    assert(info->type == XR_TYPE_FRAME_WAIT_INFO && info->next == nullptr);
    assert(state->type == XR_TYPE_FRAME_STATE && state->next == nullptr);
    if (scenario == Scenario::WaitError) return XR_ERROR_RUNTIME_FAILURE;
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
    state->predictedDisplayTime = 1000 + completed;
    state->shouldRender = completed % 2 == 0 ? XR_FALSE : XR_TRUE;
    phase = 1;
    return XR_SUCCESS;
}

XRAPI_ATTR XrResult XRAPI_CALL xrBeginFrame(
    XrSession, const XrFrameBeginInfo* info)
{
    assert(phase == 1);
    assert(info->type == XR_TYPE_FRAME_BEGIN_INFO && info->next == nullptr);
    if (scenario == Scenario::FrameBeginError) return XR_ERROR_RUNTIME_FAILURE;
    phase = 2;
    return completed == 0 ? XR_FRAME_DISCARDED : XR_SUCCESS;
}

XRAPI_ATTR XrResult XRAPI_CALL xrEndFrame(
    XrSession, const XrFrameEndInfo* info)
{
    assert(phase == 2);
    assert(info->type == XR_TYPE_FRAME_END_INFO && info->next == nullptr);
    assert(info->displayTime == 1000 + completed);
    assert(info->environmentBlendMode == XR_ENVIRONMENT_BLEND_MODE_OPAQUE);
    assert(info->layerCount == 0 && info->layers == nullptr);
    if (scenario == Scenario::FrameEndError) return XR_ERROR_RUNTIME_FAILURE;
    phase = 0;
    ++completed;
    return XR_SUCCESS;
}

XRAPI_ATTR XrResult XRAPI_CALL xrRequestExitSession(XrSession)
{
    assert(begins == 1 && ends == 0 && requests == 0 && phase == 0);
    assert(completed > 1);
    assert(std::chrono::steady_clock::now() - beganAt >= std::chrono::seconds(2));
    ++requests;
    framesAtRequest = completed;
    requestedAt = std::chrono::steady_clock::now();
    return scenario == Scenario::RequestError ? XR_ERROR_RUNTIME_FAILURE : XR_SUCCESS;
}

XRAPI_ATTR XrResult XRAPI_CALL xrEndSession(XrSession)
{
    assert(begins == 1 && ends == 0 && phase == 0);
    assert(stoppingSent || scenario == Scenario::EarlyStop);
    ++ends;
    return scenario == Scenario::EndError ? XR_ERROR_RUNTIME_FAILURE : XR_SUCCESS;
}

int main()
{
    Reset(Scenario::Normal);
    assert(RunEmptyFrameSession(XR_NULL_HANDLE, testSession));
    assert(begins == 1 && ends == 1 && requests == 1 && completed > 1 && phase == 0);
    assert(completed > framesAtRequest);
    for (Scenario failure : {
        Scenario::BeginError, Scenario::WaitError, Scenario::FrameBeginError,
        Scenario::FrameEndError, Scenario::EndError, Scenario::PollError,
        Scenario::RequestError, Scenario::WrongSession, Scenario::SessionLoss,
        Scenario::InstanceLoss, Scenario::EventsLost, Scenario::EarlyExit,
        Scenario::EarlyStop, Scenario::BeginLoss})
    {
        Reset(failure);
        assert(!RunEmptyFrameSession(XR_NULL_HANDLE, testSession));
    }
    Reset(Scenario::NoReady);
    const auto waitingAt = std::chrono::steady_clock::now();
    assert(!RunEmptyFrameSession(XR_NULL_HANDLE, testSession));
    assert(std::chrono::steady_clock::now() - waitingAt >= std::chrono::seconds(5));
    assert(begins == 0 && completed == 0 && ends == 0 && requests == 0);

    Reset(Scenario::NoStop);
    assert(!RunEmptyFrameSession(XR_NULL_HANDLE, testSession));
    assert(std::chrono::steady_clock::now() - requestedAt >= std::chrono::seconds(5));
    assert(begins == 1 && completed > framesAtRequest && requests == 1 && ends == 0);
    std::puts("Host lifecycle assertions passed (17 scenarios).");
}
```

The synthetic `XR_FRAME_DISCARDED` is deliberate robustness input, not a claim
that the normal paired sequence produces it. The tests do not emulate a full
runtime. Duplicate/unknown states, loss-pending on every individual command,
event floods, and stalls inside API calls are not covered by this small suite.
Review those branches and retain the on-device checklist; do not label host
assertions as headset verification.

## Official API references

| Topic | Authoritative reference and what to check |
| --- | --- |
| Session states | [XrSessionState](https://registry.khronos.org/OpenXR/specs/1.1/man/html/XrSessionState.html): READY, running visibility states, STOPPING, and loss grace-period behavior |
| Polling | [xrPollEvent](https://registry.khronos.org/OpenXR/specs/1.1/man/html/xrPollEvent.html), [XrEventDataSessionStateChanged](https://registry.khronos.org/OpenXR/specs/1.1/man/html/XrEventDataSessionStateChanged.html): buffer/type/session and unavailable status |
| Activation | [xrBeginSession](https://registry.khronos.org/OpenXR/specs/1.1/man/html/xrBeginSession.html), [XrSessionBeginInfo](https://registry.khronos.org/OpenXR/specs/1.1/man/html/XrSessionBeginInfo.html): READY restriction and primary view configuration |
| Timing | [xrWaitFrame](https://registry.khronos.org/OpenXR/specs/1.1/man/html/xrWaitFrame.html), [XrFrameState](https://registry.khronos.org/OpenXR/specs/1.1/man/html/XrFrameState.html): pacing, display time, shouldRender |
| Frame pairing | [xrBeginFrame](https://registry.khronos.org/OpenXR/specs/1.1/man/html/xrBeginFrame.html), [xrEndFrame](https://registry.khronos.org/OpenXR/specs/1.1/man/html/xrEndFrame.html), [XrFrameEndInfo](https://registry.khronos.org/OpenXR/specs/1.1/man/html/XrFrameEndInfo.html): order, discarded status, zero-layer submission |
| Orderly stop | [xrRequestExitSession](https://registry.khronos.org/OpenXR/specs/1.1/man/html/xrRequestExitSession.html), [xrEndSession](https://registry.khronos.org/OpenXR/specs/1.1/man/html/xrEndSession.html): asynchronous request versus STOPPING-only end |
| Destruction | [xrDestroySession](https://registry.khronos.org/OpenXR/specs/1.1/man/html/xrDestroySession.html): resource teardown is not session activation or normal end |
| Result semantics | [XrResult](https://registry.khronos.org/OpenXR/specs/1.1/man/html/XrResult.html): errors versus non-error status codes |

Use these references for normative rules; the pilot's timing, one-activation
scope, and failure criteria are application policy.

## Validation of this draft

On 2026-10-17:

- Extracted the documented solution into an isolated copy of the current
  probe and replaced only the specified call site. Full-copy C++17
  `-Wall -Wextra -Wpedantic -Werror -pthread -fsyntax-only` passed.
- Compiled and ran the documented assertion fixture using the actual existing
  `CheckXr` and `SubmitEmptyFrame` definitions. All 17 scenarios passed,
  including the two-second request threshold, five-second READY timeout,
  five-second STOPPING grace timeout, both shouldRender values, exact frame
  payload, and a frame completed while the exit request was pending.
- Confirmed the repository source, tutorial, and roadmap remained unchanged
  and the working probe still matched the read-only snapshot. The pre-existing
  untracked probe executable was left alone.

These were local compiler/stub checks only. No OpenXR device session was run.
Normal on-device activation, frame submission, orderly end, and the supported
SteamVR launch route remain unverified. Temporary validation copies were removed;
the test is reproducible from the snippets above.
