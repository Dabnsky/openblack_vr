# OpenBlack Native Steam Frame VR Roadmap

This guide describes the current OpenBlack code and SteamOS headset environment, the components needed for native VR, and a staged implementation path. The target is this machine: it reports `aarch64`, runs SteamOS `VARIANT_ID=vr`, and has SteamVR running. SteamVR supplies an ARM64 OpenXR runtime, so the planned app interface is OpenXR through the runtime already installed on the device. The first engineering risk to resolve is sharing graphics resources between OpenXR and OpenBlack's bgfx Vulkan renderer.

## Goal and Current Status

The goal is to build OpenBlack natively on the headset, launch it through the supported SteamVR app path, render the game in both headset eyes, and use tracked controllers to interact with the island while preserving desktop input and rendering.

The starting points are:

- The machine already has an ARM64 compiler/build environment, Vulkan support, the OpenXR loader development files, and SteamVR's ARM64 OpenXR runtime.
- OpenBlack currently builds as an SDL2 desktop application and renders through bgfx. It has no OpenXR dependency, session lifecycle, stereo view family, or tracked-controller input path.
- The game loop and current mouse-driven Hand interaction live in `src/Game.cpp`. Renderer ownership is centered in `src/Graphics/Renderer.cpp` and the `RendererInterface` contract.
- The base game requires legally obtained original Black & White data. Keep those files outside the repository and pass their directory using `-g`.

The implementation should keep non-VR behavior intact, verify each platform boundary on-device, and only add engine integration after a small OpenXR application can enter the headset and graphics interop is understood.

## What Exists in This Checkout

| Component | Current responsibility | Design implication |
| --- | --- | --- |
| [`src/main.cpp`](src/main.cpp) | Parses command-line options, creates `Game`, and starts it. `-g`/`--game-path` points to the original game data. | Keep headset lifecycle in an engine service; add only mode/configuration options here if needed. |
| [`src/Game.cpp`](src/Game.cpp) and [`src/Game.h`](src/Game.h) | Initializes services and game data, pumps SDL events, updates game systems, prepares one scene draw, draws the GUI, and advances the renderer frame. | This is the frame-loop integration point, but it currently has no OpenXR frame lifecycle. |
| [`src/Windowing/`](src/Windowing/) | SDL2 creates the app window and exposes native window/display handles to bgfx. | Must be checked against the device's VR app/window model. A headset runtime may require more than a normal desktop SDL window. |
| [`src/Graphics/RendererInterface.h`](src/Graphics/RendererInterface.h) and [`src/Graphics/Renderer.cpp`](src/Graphics/Renderer.cpp) | Renderer abstraction and its bgfx implementation; selects a bgfx backend and owns bgfx initialization, render passes, framebuffers, and frame submission. | The renderer owns critical graphics state. OpenXR must share or receive compatible graphics resources; a parallel, unrelated Vulkan device will not make its swapchain images renderable by bgfx. |
| [`src/Graphics/RenderPass.h`](src/Graphics/RenderPass.h) | Assigns bgfx view IDs for footprint, reflection, main scene, ImGui, and mesh viewer passes. | Reserve new IDs centrally; existing IDs 0 and 1 are already used. |
| [`src/Graphics/ShaderManager.cpp`](src/Graphics/ShaderManager.cpp) and [`src/Camera/Camera.h`](src/Camera/Camera.h) | Supplies the current camera's view/projection matrices to a bgfx view; the camera also provides screen/world projection and raycast helpers. | Per-eye poses and asymmetric OpenXR projections need to reach the renderer without corrupting the existing desktop camera path. |
| [`src/Game.cpp`](src/Game.cpp) and [`src/ECS/Systems/Implementations/HandSystem.cpp`](src/ECS/Systems/Implementations/HandSystem.cpp) | Game input uses SDL mouse state, deprojects the mouse through the camera, raycasts into the world, and moves the left-hand entity to the intersection. The hand ECS system updates hand-related world effects. | Reuse the world raycast and hand entity concepts, but introduce a tracked-controller input source and an explicit tracking-to-game-world mapping. |
| [`components/`](components/) and [`src/`](src/) | `components/` contains format/library targets; `src/` contains the game executable/library and runtime systems. | Put VR integration in a small runtime subsystem and renderer adapter, rather than mixing OpenXR calls into asset-format libraries. |
| [`CMakeLists.txt`](CMakeLists.txt), [`CMakePresets.json`](CMakePresets.json), and [`vcpkg.json`](vcpkg.json) | Configure required libraries, platform presets, and vcpkg dependencies. | Add OpenXR as an optional native Linux dependency; use the Linux preset, not the Android ARM64 preset. |
| [`readme.md`](readme.md) | Build instructions and lawful extraction guidance for original game assets. | The game assets remain a separate prerequisite; do not bundle copyrighted files with builds. The `-g` path must resolve the expected game directories. |

The project currently selects Vulkan by default on non-Apple systems, but that only selects the bgfx renderer. It does not initialize OpenXR or imply compatibility with a headset runtime.

## Tools Present on This Machine

The following were observed on the SteamOS target on 2026-10-12:

| Tool or capability | Observed result | How it is used here |
| --- | --- | --- |
| Architecture and OS | `aarch64`; SteamOS, `VARIANT_ID=vr` | Native configure/build is appropriate; do not select the Android ARM64 preset. |
| CMake | 3.29.2 | Reads this repository's configure/build presets and drives configure/build. |
| Ninja | Installed | Used by the vcpkg Ninja preset; the system-deps preset uses Unix Makefiles. |
| GCC/G++ and Clang/Clang++ | Installed | Native C++ compilers. CMake selects its compiler at first configure; set `CC`/`CXX` before configuring only if intentionally choosing another one. |
| pkg-config | Installed | Queries installed system library metadata and emits include/link flags for small probes. |
| SDL2 development metadata | 2.30.2 | OpenBlack's window, event, and desktop input dependency. |
| OpenXR development metadata | 1.1 | Headers/loader development integration is discoverable with pkg-config; this is not evidence of an active headset runtime. |
| Vulkan tools/driver | `vulkaninfo` reports Vulkan 1.4 and Turnip/Mesa on Qualcomm Adreno 750 | Confirms a Vulkan device/driver is visible. The SSH/no-display invocation skips display-surface information. |
| SteamVR/OpenXR runtime | `/opt/steamvr/steamxr_linuxarm64.json` names SteamVR; its relative `bin/linuxarm64/vrclient.so` library exists. SteamVR's `vrserver` and `vrcompositor` processes are running. | Use the native SteamVR-provided OpenXR runtime; do not install or hard-code a second runtime. Verify that the supported app launch context selects it. |
| `openxrinfo` utility | Not found on PATH | Do not use instructions that assume this diagnostic program is installed; the Stage 1 probe is still needed. |

Reference the installed tools from the repository root with:

```sh
cd /home/steamos/david/openblack
uname -m
cmake --version
ninja --version
g++ --version
pkg-config --modversion sdl2
pkg-config --modversion openxr
pkg-config --cflags --libs openxr
vulkaninfo --summary
cmake --list-presets
```

`pkg-config --cflags --libs openxr` shows the compiler and linker flags for a standalone OpenXR probe. The project itself uses CMake `find_package` for its dependencies, so a pkg-config result is a diagnostic and not a replacement for a successful project configure. In particular, bgfx does not provide pkg-config metadata in this environment; CMake must still find the bgfx package expected by this checkout.

The conventional `/usr/share/openxr/1` directory contains API-layer data but no active-runtime manifest, `/etc/openxr/1` is absent, and `XR_RUNTIME_JSON` is unset in this shell. Separately, `/opt/steamvr/steamxr_linuxarm64.json` registers SteamVR's native ARM64 OpenXR runtime, and SteamVR is currently running. Steam's Lepton container overlay also has an `active_runtime.json` for SteamVR, so runtime selection may be supplied through its managed app environment. Verify which runtime manifest a native OpenBlack process sees when launched through the supported headset path; do not hard-code the container path. `vulkaninfo --summary` was run without `DISPLAY`, so run surface diagnostics from the supported graphical/headset session when needed.

## System and Frame Flow

```mermaid
flowchart LR
    UserInput[SDL mouse and keyboard] --> Game[Game update loop]
    Game --> Camera[Camera and world raycast]
    Camera --> Hand[Hand ECS entity]
    Game --> Scene[DrawScene description]
    Scene --> Renderer[RendererInterface]
    Renderer --> Bgfx[bgfx passes, shaders, framebuffers]
    Bgfx --> Window[SDL window and native handles]
    XRRuntime[OpenXR loader and device runtime] --> XRSession[OpenXR instance, session, actions]
    XRSession --> XRFrame[Predicted frame time and per-eye views]
    XRFrame --> Renderer
    XRSession --> InputMap[Controller pose and action mapping]
    InputMap --> Hand
    Renderer --> XRSwapchain[OpenXR swapchain images]
    XRSwapchain --> XRRuntime
```

The diagram shows the desired connections, not functionality already present. In the current code, SDL and bgfx drive one desktop window, `Game` draws one main scene camera, and OpenXR has no role.

## Build OpenBlack on the Headset

Build directly on this Steam Frame environment. It is already the native ARM64 target, so no cross toolchain, sysroot, or Android preset is needed. The repository has no generic Linux ARM64 cross preset; the Android ARM64 preset is not a substitute.

1. Confirm the checkout and available presets:

   ```sh
   git submodule update --init --recursive
   cmake --list-presets
   ```

   The checked-in submodule is vcpkg. bgfx is obtained through the dependency setup, not as a declared git submodule.

2. Use the tools already present on this target first. The inventory above confirms CMake, compilers, Ninja, SDL2 metadata, OpenXR metadata, and Vulkan tools. Configure the system-dependency preset; if CMake identifies a missing project dependency, use the SteamOS-supported development/package workflow for this image or the repository's vcpkg preset rather than installing packages blindly. The base project does not yet require OpenXR.

3. Configure and build the current, non-VR application using the system dependency preset:

   ```sh
   cmake --preset linux-system-deps
   cmake --build --preset linux-system-deps-release --parallel
   ```

   This preset uses system-installed project dependencies and produces a native AArch64 binary. Or use the vcpkg preset where appropriate, after confirming vcpkg's target triplet resolves to native ARM64 on this build:

   ```sh
   cmake --preset ninja-multi-vcpkg
   cmake --build --preset ninja-multi-vcpkg-release --parallel
   ```

   `linux-system-deps` is a configure preset; use the corresponding build preset, such as `linux-system-deps-release` or `linux-system-deps-debug`. Because configure runs on the target, this build uses its native compiler and ABI. Inspect the vcpkg triplet before relying on vcpkg; do not assume it selected ARM64 solely from a preset name.

4. Verify the executable and CLI, then test with legally obtained original game data:

   ```sh
   ./cmake-build-presets/linux-system-deps/bin/openblack --help
   ./cmake-build-presets/linux-system-deps/bin/openblack -g /path/to/Black-and-White
   ```

   The game path must contain the expected `Data` and `Scripts` trees; audio files are also used by the game. Start by validating the normal SDL windowed build and logs on this device before introducing headset-mode launch settings. A successful windowed launch validates the native build, not VR compositor integration.

5. Run tests when they are configured. The top-level CMake only enables tests when GTest is found and the build is not cross-compiling. The checked-in test presets cover the vcpkg Ninja configurations; for a system-dependency build, use CTest directly:

   ```sh
   ctest --test-dir cmake-build-presets/linux-system-deps --output-on-failure
   ```

## Recommended VR Design

Keep the current desktop route working. Add an optional OpenXR path behind a CMake option, but only after the runtime and graphics interop have been demonstrated. Prefer normal CMake conditionals and a runtime mode/configuration over scattering `#ifdef` blocks across game and renderer code.

### Runtime and graphics ownership

OpenXR has distinct loader and runtime roles. The loader dispatches calls; SteamVR supplies the headset, tracking, and compositor through the native ARM64 OpenXR runtime installed at `/opt/steamvr`. Installing headers or linking the loader does not prove the native OpenBlack process is launched with that runtime selected or that the runtime supports the graphics API path the application uses. Do not hard-code a runtime manifest path or the Lepton container overlay path. Verify runtime discovery using the same Steam/SteamVR launch route intended for OpenBlack and a small OpenXR test program.

Before project integration, make a small native ARM64 OpenXR probe that:

- Runs from the device's supported SteamVR/VR application context, creates an instance and HMD system, enumerates supported view configurations and graphics requirements, and prints the runtime/version and extensions. Confirm it reports the SteamVR runtime named in `/opt/steamvr/steamxr_linuxarm64.json`.
- Creates and runs a session using the graphics API the device runtime actually supports; handles session-state events rather than assuming it is immediately running.
- Calls the frame lifecycle in the required order (`xrWaitFrame`, `xrBeginFrame`, view location, swapchain image acquire/wait/release, and `xrEndFrame`).
- Renders a simple test scene to the runtime's swapchain and verifies both eye images and head tracking in the headset.

Then prove that bgfx can render into those swapchain images on that target. OpenXR Vulkan session creation must use a Vulkan device compatible with the runtime's requirements. The current bgfx factory initializes bgfx itself from SDL native window handles; it does not expose an OpenXR-owned Vulkan device or a swapchain-image import abstraction. If safe, supported interop cannot be demonstrated, choose deliberately between extending the bgfx backend integration and using a dedicated VR-capable rendering backend. Avoid maintaining two competing graphics devices or copying full-resolution eye images every frame as a first design.

### Suggested module boundaries

The eventual implementation can live in a new `src/VR/` (or `src/XR/`) subsystem with these responsibilities. These are design roles, not existing classes:

- **Session/runtime service:** OpenXR instance/system/session creation, extension selection, event polling, session state, reference spaces, and orderly shutdown.
- **Frame/view provider:** predicted display time, located views, per-eye FOV/projection, and a frame-scoped description passed to rendering. Keep OpenXR timing and frame begin/end calls in one owner.
- **Swapchain/graphics bridge:** swapchain formats, image acquisition and release, and the chosen Vulkan/bgfx interop. Runtime images are borrowed resources with lifecycle constraints; do not treat them as ordinary `FrameBuffer` objects without validating ownership and synchronization.
- **Input adapter:** OpenXR action sets, interaction profiles, controller action/pose spaces, and conversion into game actions. Keep physical device bindings out of core gameplay logic.
- **Tracking-to-world mapper:** configurable origin, scale, orientation, and play-space policy. Convert meters in the OpenXR reference space into the game's world units and keep camera movement comfortable.

Let `Game` coordinate these interfaces. Keep the existing non-VR `RendererInterface` path and add a renderer-facing view family or equivalent input describing one or more camera views and their render targets. `Renderer::DrawPass` and `ShaderManager::SetCamera` are closer to the point that must consume per-eye matrices than `main.cpp` or a hypothetical `Camera.cpp` at `src/camera/`.

### Stereo and the existing render graph

OpenXR supplies each view's pose and FOV; derive the eye view/projection matrices from that data. Do not use a symmetric desktop projection or simply offset the existing camera. OpenBlack's GLM configuration is left-handed and its camera supports reversed-Z, so projection conversion and depth conventions need deliberate validation against the runtime.

Allocate distinct bgfx view IDs for every pass that must be rendered per eye. In the current [`src/Graphics/RenderPass.h`](src/Graphics/RenderPass.h), IDs 0 and 1 are Footprint and Reflection, ID 2 is Main, ID 3 is ImGui, and ID 4 is MeshViewer. Reserve IDs centrally and test the maximum bgfx view ID. Decide explicitly whether footprint/reflection are shared mono passes or also rendered per eye; do not duplicate everything blindly.

The initial VR milestone should render the main world into both eyes while retaining correct depth, culling, sky, terrain, entities, and frame submission. The current desktop ImGui and other screen-space UI cannot simply be copied in front of both eyes. Initially hide nonessential debug UI in VR, then design stable-distance world-space UI or a comfortable head-locked panel. Validate reflection/water behavior separately.

### Controller and Black & White interaction

OpenXR controller poses are relative to a reference space and measured in meters; Black & White uses its own large world coordinate scale. The current hand is a world entity placed at the terrain/water intersection of a camera ray. A compatible first interaction design is:

- Use the dominant controller as a pointing ray. Transform it into game space, raycast against terrain/entities using the existing dynamics/world systems, and place the hand cursor at the hit point.
- Map trigger press/release to the existing grip/grab interaction state, then map squeeze or another action to gestures only after the basic pickup interaction is reliable.
- Use the other controller for recentering, camera movement/rotation, or menus. Avoid continuous artificial locomotion as the only way to inspect a god-game island.
- Make head translation scale and camera pivot configurable. A small tabletop/island-diorama view is a better initial comfort baseline than mapping room-scale movement directly to thousands of game-world units.
- Preserve mouse/keyboard behavior so desktop mode remains a working fallback and an easy way to compare results.

## Staged Changes and Acceptance Checks

### Stage 0: Baseline and target facts

- Record the upstream commit, compiler, target triple, bgfx backend, device OS/runtime version, and a successful non-VR build/run.
- Verify how applications are launched in headset mode, confirm SteamVR is selected as the OpenXR runtime for the native process, check the runtime's supported graphics API, and determine whether SDL windows are expected.
- Keep game data outside the repository and verify the `-g` asset path.

**Done when:** the current game builds for native ARM64 and its normal path is reproducible; device runtime facts are written down rather than inferred from the OS name.

### Stage 1: OpenXR runtime probe

- Build a tiny independent ARM64 executable using the target's OpenXR loader and SteamVR runtime, launched through the supported SteamVR app/session route.
- Test session state transitions, both view poses, controller actions, and swapchain rendering.
- Measure frame timing and verify runtime selection after reboot/relaunch.

**Done when:** the test scene is actually composited in the headset, not just rendered to a desktop window, and tracking/input are visible in logs.

### Stage 2: bgfx/OpenXR interop spike

- Test the runtime's preferred API against the current bgfx backend.
- Establish which subsystem creates and owns the instance/device/context, how swapchain textures become render targets, and how synchronization is performed.
- Check image formats, color space, dimensions, sample counts, depth, orientation, and lifetime. Add an explicit error path if the runtime or required extension is unavailable.

**Done when:** bgfx renders a known test image to each OpenXR view without a second GPU device, invalid handles, synchronization errors, or per-frame CPU readback.

### Stage 3: Optional engine plumbing

Specific likely changes after the spike:

1. Add an `OPENBLACK_ENABLE_OPENXR` CMake option, default OFF. Add the OpenXR dependency only inside that option, using the imported target actually exported by the selected vcpkg port/system package. Link it to the engine target that owns the VR code (`openblack_lib`), not just the CLI executable.
2. Add a focused `src/VR/` subsystem for session lifecycle, frame state/views, graphics interop, action input, and tracking-space mapping. Keep the OpenXR headers out of unrelated game and asset-format components.
3. Extend renderer-facing scene input to carry a view family and render-target mapping. Add centrally allocated view IDs; preserve the existing mono desktop path and its render passes.
4. Integrate OpenXR event/frame ordering into `Game::Run`/`Game::Update` at one clear boundary. Ensure every begun frame is ended, including frames where rendering is skipped or session state changes.
5. Replace the mouse-only hand-placement source with an input abstraction that can provide either mouse ray or controller ray. Route controller press/release through a game action instead of mutating mouse state.
6. Add tests for tracking-space conversion, action-to-game-action mapping, render-view ID allocation, and session/frame state handling where these can be tested without a headset. Keep device smoke tests as a separate manual test checklist.

**Done when:** desktop builds with VR disabled; a VR build starts and exits cleanly when a runtime exists; loss/pause of the session does not leave bgfx or OpenXR in an invalid frame state.

### Stage 4: First playable interaction

- Render both eyes from the runtime's located views.
- Add a controller ray cursor and terrain/object selection before attempting articulated hand tracking or gesture recognition.
- Recenter, tune world scale, camera pivot, sensitivity, and comfort; add a no-controller fallback.
- Test menu/debug behavior, water/reflection, resolution changes, suspend/resume, shutdown, and sustained frame timing on the target hardware.

**Done when:** a user can look around comfortably, point at the world, pick up/release a supported object, pause/recenter, and return to desktop mode without regressing it.

## Planned Source Changes

Make these changes only after the OpenXR probe and bgfx interop milestones pass:

| Location | Change |
| --- | --- |
| [`CMakeLists.txt`](CMakeLists.txt), [`vcpkg.json`](vcpkg.json), and [`src/CMakeLists.txt`](src/CMakeLists.txt) | Add `OPENBLACK_ENABLE_OPENXR`, default OFF. Add the loader dependency conditionally using the package target available on SteamOS/vcpkg, and link it to `openblack_lib`, which owns the engine implementation. Keep the normal build independent of OpenXR. |
| New `src/VR/` subsystem | Implement OpenXR instance/system/session lifecycle, session event handling, reference spaces, predicted frame timing, located views, swapchain lifecycle, controller actions, and tracking-to-world conversion. Keep OpenXR types out of asset-format components. |
| [`src/Graphics/RendererInterface.h`](src/Graphics/RendererInterface.h) and [`src/Graphics/Renderer.cpp`](src/Graphics/Renderer.cpp) | Add a renderer-facing set of views and targets for stereo while retaining the current mono route. Implement the Vulkan/bgfx-to-OpenXR swapchain bridge only using a proven ownership and synchronization model. |
| [`src/Graphics/RenderPass.h`](src/Graphics/RenderPass.h) | Reserve collision-free bgfx view IDs for left/right scene passes and any additional VR UI passes. Preserve the existing footprint, reflection, main, ImGui, and mesh-viewer assignments or migrate all IDs together. |
| [`src/Graphics/ShaderManager.cpp`](src/Graphics/ShaderManager.cpp), [`src/Camera/Camera.h`](src/Camera/Camera.h), and camera implementation | Provide per-eye view and projection matrices from OpenXR without changing desktop camera behavior. Validate handedness, asymmetric FOV, and reversed-Z depth. |
| [`src/Game.cpp`](src/Game.cpp) and [`src/Game.h`](src/Game.h) | Coordinate OpenXR event polling and the wait/begin/render/end frame sequence at one boundary. Guarantee frame cleanup on skipped-render, pause, and shutdown paths. |
| [`src/Input/`](src/Input/) and [`src/ECS/Systems/Implementations/HandSystem.cpp`](src/ECS/Systems/Implementations/HandSystem.cpp) | Introduce an input source abstraction for mouse/controller rays and action edges. Map the dominant controller to the existing world raycast and hand entity; preserve mouse/keyboard input. |
| [`test/`](test/) | Add non-device tests for tracking-space conversion, action mapping, frame-state handling, and render-view ID allocation; maintain a separate on-device smoke-test checklist. |

## Development Workflow

Use a dedicated topic branch and keep the desktop baseline easy to rebuild. Split work into runtime probe, renderer bridge, engine lifecycle, and controller interaction changes so each can be reviewed and tested independently. Sync with the upstream default branch when relevant changes land, and discuss the renderer-interoperability direction with OpenBlack maintainers before committing to a long-lived renderer fork.
