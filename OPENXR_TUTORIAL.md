# Learning OpenXR on Steam Frame: From Discovery to a Vulkan Session

Living tutorial for the native OpenBlack VR project. Updated: 2026-10-15.

This document explains the standalone probe one block at a time, including why
each block exists, how its parameters work, and where to find the official rules.
It presents one coherent implementation, not a history of code changes.

Use the [VR roadmap](STEAM_FRAME_VR_GUIDE.md) for project architecture and engine
integration planning. Use this tutorial to understand and extend the
[standalone probe](src/VR/xr_probe.cpp).

## Contents

1. [Goal and current checkpoint](#1-goal-and-current-checkpoint)
2. [The mental model](#2-the-mental-model)
3. [Development environment and build command](#3-development-environment-and-build-command)
4. [Headers and common API conventions](#4-headers-and-common-api-conventions)
5. [Shared diagnostic and lookup helpers](#5-shared-diagnostic-and-lookup-helpers)
6. [Discover the Vulkan extension](#6-discover-the-vulkan-extension)
7. [Find a graphics queue family](#7-find-a-graphics-queue-family)
8. [Create a Vulkan logical device](#8-create-a-vulkan-logical-device)
9. [Create the graphics-bound OpenXR session](#9-create-the-graphics-bound-openxr-session)
10. [Connect the graphics setup stages](#10-connect-the-graphics-setup-stages)
11. [Observe session lifecycle events](#11-observe-session-lifecycle-events)
12. [Create the OpenXR instance and coordinate cleanup](#12-create-the-openxr-instance-and-coordinate-cleanup)
13. [Build, run, and interpret the result](#13-build-run-and-interpret-the-result)
14. [Next lessons and how to extend this document](#14-next-lessons-and-how-to-extend-this-document)
15. [Documentation bookshelf](#15-documentation-bookshelf)

## 1. Goal and current checkpoint

The eventual goal is to render OpenBlack in the headset and use tracked
controllers to interact with the island. This probe tests platform boundaries
before introducing them into the engine.

The probe does not load game assets, use SDL, or initialize bgfx.

### Recorded results

| Checkpoint | Status on 2026-10-15 |
| --- | --- |
| Native OpenBlack build and seven Release tests | Passed previously |
| Desktop Vulkan rendering, camera, and interaction through RDP/X11 | Confirmed |
| Clean desktop window close | Known segmentation fault; cause not diagnosed |
| OpenXR instance and HMD discovery | Confirmed |
| Runtime identity | `SteamVR/OpenXR`, version `2.17.10` |
| `XR_KHR_vulkan_enable2` advertised and enabled | Confirmed |
| OpenXR Vulkan graphics requirements | Minimum `1.0.0`, maximum `1.2.0` |
| Vulkan instance created through OpenXR | Confirmed |
| Runtime-selected GPU | `Turnip Adreno (TM) 750` |
| GPU-reported Vulkan API | `1.4.359` |
| Graphics queue selection | Family `0`, one queue, graphics supported |
| Logical device creation and queue retrieval | Confirmed |
| Vulkan-backed OpenXR session creation and destruction | Confirmed |
| Session-event observation reaching `READY` | Confirmed: `IDLE` (1), then `READY` (2); session deliberately not begun |
| Primary stereo view configuration | Confirmed: `PRIMARY_STEREO` (2) |
| Recommended stereo image dimensions | Confirmed for both views: 1728x1728; maximum 8192x8192; recommended/max swapchain samples 1 |
| Environment blend modes | Confirmed: OPAQUE (1) and ALPHA_BLEND (3); initial probe requires OPAQUE |
| Session beginning, frame submission, tracking, controller actions | Not yet verified |
| bgfx/OpenXR graphics interoperation | Not yet implemented |

The successful event-observation output confirms that the helper is now called.
Section 12 shows its placement between successful setup and cleanup. The latest
reported run includes `PRIMARY_STEREO` support, `READY`, and the expected session,
device, and Vulkan-instance destruction messages. No exit status was supplied.

### Desktop baseline versus headset rendering

The SSH shell had no `DISPLAY` or `WAYLAND_DISPLAY`. SDL chose KMSDRM, and
OpenBlack failed with `Unsupported platform or window manager: 13`.

The RDP terminal had `DISPLAY=:10.0` and no Wayland display. OpenBlack rendered
the island and accepted camera/input there. Therefore, Wayland is not required
for the established desktop baseline.

A desktop window working does not prove headset rendering. The VR path will
render into OpenXR swapchains, rather than depend on an unsupported KMSDRM
desktop surface. Keep the window-close crash recorded separately; the final
audio log messages do not identify its cause.

## 2. The mental model

### Who does what?

```text
OpenBlack
  Gameplay, assets, world simulation, and interaction rules
       |
       +-- bgfx / Vulkan: render the world
       |
       +-- OpenXR: session, timing, poses, actions, swapchains, composition
                    |
               OpenXR loader
                    |
               SteamVR runtime
                    |
              Headset/controllers
```

OpenXR is our primary headset API. It does not draw terrain, simulate creatures,
or implement game-specific grabbing behavior. Vulkan performs graphics work;
OpenXR coordinates that work with the headset runtime.

The loader dispatches API calls to a runtime. Linking the loader is not the
same as hard-coding SteamVR's runtime manifest.

### Objects introduced in this tutorial

| Object | Role | Ownership in this probe |
| --- | --- | --- |
| `XrInstance` | Application connection to OpenXR | Destroy with `xrDestroyInstance` |
| `XrSystemId` | Runtime's HMD system identifier | No separate destruction |
| `VkInstance` | Vulkan initialization and GPU discovery | Destroy with `vkDestroyInstance` |
| `VkPhysicalDevice` | Handle identifying a GPU within the Vulkan instance | No separate destruction |
| Queue family index | Identifies a group of queues with shared capabilities | An integer, not an owned resource |
| `VkDevice` | Logical device used to work with the chosen GPU | Destroy with `vkDestroyDevice` |
| `VkQueue` | Queue for submitting GPU work | Owned by the logical device |
| `XrSession` | Runtime session connected to our graphics resources | Destroy before those graphics resources |

Creation is not activation: `xrCreateSession` does not begin a session or submit
an image.

## 3. Development environment and build command

Run on the ARM64 Steam Frame itself. This is native Linux development, not an
Android build and not cross-compilation.

```sh
pkg-config --modversion openxr vulkan
pkg-config --cflags --libs openxr vulkan
printf 'XR_RUNTIME_JSON=<%s>\n' "$XR_RUNTIME_JSON"
```

Observed package versions were OpenXR `1.1` and Vulkan `1.4.309`. The advertised
link flags were `-lopenxr_loader -lvulkan`.

- Package metadata identifies development files, not the active runtime.
- An empty `XR_RUNTIME_JSON` does not mean runtime discovery must fail.
- Do not append `~` after the quoted variable: it becomes literal output, not
  evidence that the variable contains a tilde.
- The runtime's reported version, requested API version, and package version
  are different things.

### The working device-specific build

From the directory containing `xr_probe.cpp`:

```sh
g++ -std=c++17 -Wall -Wextra -Wpedantic -pthread \
  xr_probe.cpp -o xr_probe \
  /opt/steamvr/bin/linuxarm64/libopenxr_loader.so \
  -lvulkan \
  -Xlinker -rpath -Xlinker /opt/steamvr/bin/linuxarm64
```

| Argument | Meaning |
| --- | --- |
| `g++` | Compile and link C++ |
| `-std=c++17` | Use the C++17 language standard |
| `-Wall -Wextra -Wpedantic` | Enable compiler diagnostics |
| `-pthread` | Enable compiler/linker threading support for event polling sleeps |
| `xr_probe.cpp` | Source file |
| `-o xr_probe` | Output executable |
| Explicit loader path | Use this verified ARM64 shared OpenXR loader |
| `-lvulkan` | Resolve direct Vulkan calls used by the probe |
| `-Xlinker -rpath -Xlinker ...` | Give the linker a runtime shared-library search path |

The shorter equivalent rpath flag is
`-Wl,-rpath,/opt/steamvr/bin/linuxarm64`. It must be one argument; the `l` in
`-Wl,` is a lowercase L. `-rpath,...` by itself is not a GCC option.

### Why not just use the pkg-config link flags?

On this device, the default `-lopenxr_loader` link failed with undefined
references to core OpenXR functions. Inspection found a static
`/usr/lib/libopenxr_loader.a` supplied by `gfxreconstruct`, with LTO data that
the symbol tools could not read without a plugin. The exact archive/toolchain
issue was not fully diagnosed.

SteamVR's shared loader exported the needed functions, and the explicit link
above worked. This is a local diagnostic workaround, not permanent engine
dependency configuration. Do not replace system libraries or put the loader
path into game source.

An include provides declarations for compilation. A linked library supplies
implementations. Runtime discovery happens only after the executable runs.

## 4. Headers and common API conventions

### Assembling the program

The C++ blocks in sections 4 through 12 describe one teaching program.
Place helper definitions before their callers. In section 10, the numbered
setup stages belong inside the same `ProbeGraphics` function until its closing
brace, except for the explicitly labeled helper definitions in sections
10.2a through 10.2c: place those before `ProbeGraphics` and insert only their
call-site blocks into its body. Explanation tables and output examples are not
source code.

The helpers are ordinary file-level functions in this teaching version. They
may also live together in an anonymous namespace, as in the workspace probe.
If using that namespace, close it before `main`; `main` must be at global scope.

### Header block

```cpp
#define XR_USE_GRAPHICS_API_VULKAN
#include <vulkan/vulkan.h>
#include <openxr/openxr.h>
#include <openxr/openxr_platform.h>

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <thread>
#include <vector>
```

| Line or group | Explanation |
| --- | --- |
| `XR_USE_GRAPHICS_API_VULKAN` | Exposes Vulkan-specific OpenXR declarations |
| `vulkan.h` | Defines Vulkan types referenced by the graphics binding |
| `openxr.h` | Core OpenXR handles, structures, results, and functions |
| `openxr_platform.h` | Graphics/platform-specific integration declarations |
| `<chrono>` and `<thread>` | Bounded event observation and short sleeps |
| `<cstdio>` | Printing and standard output/error streams |
| `<cstdlib>` | `EXIT_SUCCESS` and `EXIT_FAILURE` |
| `<cstring>` | String comparison and fixed application-name copying |
| `<vector>` | Storage for enumerated extensions and queue families |

These includes do not create any API objects.

### Structure conventions

```text
OpenXR: type, next
Vulkan: sType, pNext
```

Zero-initialize structures with `{}`, then set the required type discriminator.
The next pointer can attach optional typed structures. Zero does not select a
valid type automatically.

Not every structure has a discriminator: `VkQueueFamilyProperties`, for
example, has no `sType`.

Reference: [OpenXR specification: API conventions][xr-spec].

## 5. Shared diagnostic and lookup helpers

### 5.1 Check OpenXR results

```cpp
bool CheckXr(XrInstance instance, XrResult result, const char* operation)
{
    if (XR_SUCCEEDED(result))
    {
        return true;
    }
    if (instance == XR_NULL_HANDLE)
    {
        std::fprintf(stderr, "%s failed: %d\n",
                     operation, static_cast<int>(result));
        return false;
    }

    char text[XR_MAX_RESULT_STRING_SIZE]{};
    const XrResult conversion = xrResultToString(instance, result, text);
    if (XR_SUCCEEDED(conversion))
    {
        std::fprintf(stderr, "%s failed: %s (%d)\n",
                     operation, text, static_cast<int>(result));
    }
    else
    {
        std::fprintf(stderr, "%s failed: %d; xrResultToString failed: %d\n",
                     operation, static_cast<int>(result),
                     static_cast<int>(conversion));
    }
    return false;
}
```

| Part | Explanation |
| --- | --- |
| `instance` | Live instance used for symbolic error descriptions, or null when unavailable |
| `result` | Result of the OpenXR operation being checked |
| `operation` | Label naming the failing call |
| `XR_SUCCEEDED` | Accepts non-error statuses as well as `XR_SUCCESS` |
| Null-instance branch | Numeric diagnostics before creation or after destruction |
| `text` | Output buffer sized by the API |
| `xrResultToString` | Converts the result to a symbolic description |
| Conversion-failure branch | Reports both errors rather than hiding the original |

Never pass a destroyed non-null instance just to obtain a prettier error.
Use `std::` with functions from C++ standard headers.

Reference: [XrResult][xr-result], [xrResultToString][xr-result-string].

### 5.2 Load a typed extension function

```cpp
template<typename Function>
Function LoadXrFunction(XrInstance instance, const char* name)
{
    PFN_xrVoidFunction function = nullptr;
    if (!CheckXr(instance, xrGetInstanceProcAddr(instance, name, &function), name))
    {
        return nullptr;
    }
    if (function == nullptr)
    {
        std::fprintf(stderr, "%s resolved to a null function pointer.\n", name);
        return nullptr;
    }
    return reinterpret_cast<Function>(function);
}
```

- `template<typename Function>` lets each caller supply its exact `PFN_...`
  function-pointer type.
- `PFN_xrVoidFunction` is OpenXR's generic function-pointer representation.
- `&function` lets OpenXR write the retrieved address.
- `CheckXr` reports lookup failures.
- The null guard prevents a later call through a null pointer.
- `reinterpret_cast` converts the generic function pointer to the requested
  signature. The supplied name and `PFN_...` type must match.
- Returning a pointer does not execute the command or create resources.

We use this after instance creation, with the required extension enabled.
Do not use a loaded pointer after destroying its associated instance.

Reference: [xrGetInstanceProcAddr][xr-get-proc].

### 5.3 Print OpenXR-packed versions

```cpp
void PrintXrVersion(const char* label, XrVersion version)
{
    std::printf("%s: %u.%u.%u\n", label,
                XR_VERSION_MAJOR(version),
                XR_VERSION_MINOR(version),
                XR_VERSION_PATCH(version));
}
```

`XrVersion` packs major, minor, and patch into one value. These macros unpack it.
Use this helper for runtime versions and OpenXR's Vulkan requirements fields.
Use Vulkan's version macros for `VkPhysicalDeviceProperties::apiVersion`.

## 6. Discover the Vulkan extension

```cpp
bool CheckRequiredExtension()
{
    std::vector<XrExtensionProperties> extensions;
    uint32_t count = 0;
    XrResult result = XR_SUCCESS;
    do
    {
        if (!CheckXr(XR_NULL_HANDLE,
                     xrEnumerateInstanceExtensionProperties(
                         nullptr, 0, &count, nullptr),
                     "xrEnumerateInstanceExtensionProperties(count)"))
        {
            return false;
        }

        if (count == 0)
        {
            break;
        }
        extensions.assign(count, XrExtensionProperties{});
        for (auto& extension : extensions)
        {
            extension.type = XR_TYPE_EXTENSION_PROPERTIES;
        }

        result = xrEnumerateInstanceExtensionProperties(
            nullptr, static_cast<uint32_t>(extensions.size()),
            &count, extensions.data());

        if (result != XR_ERROR_SIZE_INSUFFICIENT &&
            !CheckXr(XR_NULL_HANDLE, result,
                     "xrEnumerateInstanceExtensionProperties(list)"))
        {
            return false;
        }
    } while (result == XR_ERROR_SIZE_INSUFFICIENT);

    bool available = false;
    for (uint32_t i = 0; i < count; ++i)
    {
        const auto& extension = extensions[i];
        std::printf("Extension: %s (revision %u)\n",
                    extension.extensionName, extension.extensionVersion);
        if (std::strcmp(extension.extensionName,
                        XR_KHR_VULKAN_ENABLE2_EXTENSION_NAME) == 0)
        {
            available = true;
        }
    }

    std::printf("XR_KHR_vulkan_enable2 advertised: %s\n",
                available ? "yes" : "no");
    if (!available)
    {
        std::fprintf(stderr,
                     "This Vulkan probe requires XR_KHR_vulkan_enable2.\n");
    }
    return available;
}
```

### The two-call enumeration pattern

1. Pass capacity `0` and a null output array to query the count.
2. Allocate that many entries.
3. Initialize each entry's `type`.
4. Call again with storage and capacity.
5. Inspect only the entries returned.

The first `nullptr` means we are not requesting extensions for a particular
named API layer. The call can execute before an instance exists, which lets us
select extensions during creation.

`extensions.assign(...)` produces fresh zero-initialized entries, including
null `next` pointers. A zero count is handled before the fill call: a
zero-capacity call only queries a count, not array contents.

If the list grows between calls, `XR_ERROR_SIZE_INSUFFICIENT` requests a retry.
Other errors are reported and stop the exercise.

`std::strcmp(...) == 0` means the names match. The extension revision is not a
Vulkan API version.

**Advertised is not enabled.** We will explicitly enable the extension in
section 12. An advertised extension also does not establish every
hardware-specific feature's availability.

Checkpoint: `XR_KHR_vulkan_enable2 advertised: yes`.

References: [xrEnumerateInstanceExtensionProperties][xr-enumerate],
[XR_KHR_vulkan_enable2][xr-vulkan].

## 7. Find a graphics queue family

This helper is defined here so later setup can call it. It runs only after
OpenXR selects the physical GPU in section 10.

```cpp
bool FindGraphicsQueueFamily(VkPhysicalDevice physicalDevice,
                             uint32_t& graphicsQueueFamily)
{
    uint32_t familyCount = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(
        physicalDevice, &familyCount, nullptr);

    if (familyCount == 0)
    {
        std::fprintf(stderr, "The selected GPU reports no queue families.\n");
        return false;
    }

    std::vector<VkQueueFamilyProperties> families(familyCount);
    vkGetPhysicalDeviceQueueFamilyProperties(
        physicalDevice, &familyCount, families.data());

    for (uint32_t i = 0; i < familyCount; ++i)
    {
        const auto& family = families[i];
        const bool supportsGraphics =
            (family.queueFlags & VK_QUEUE_GRAPHICS_BIT) != 0;

        std::printf("Queue family %u: queues=%u, graphics=%s\n",
                    i, family.queueCount, supportsGraphics ? "yes" : "no");

        if (family.queueCount > 0 && supportsGraphics)
        {
            graphicsQueueFamily = i;
            return true;
        }
    }

    std::fprintf(stderr, "No graphics-capable queue family was found.\n");
    return false;
}
```

| Part | Explanation |
| --- | --- |
| `VkPhysicalDevice` | The runtime-selected GPU, not an independently chosen one |
| `uint32_t&` | Output reference for the selected family index |
| Count/fill calls | Query storage requirements, then obtain properties |
| `queueFlags & VK_QUEUE_GRAPHICS_BIT` | Bitmask test for graphics support |
| `queueCount > 0` | Ensures a queue can be requested |
| Early success | Selects the first suitable family; it does not print remaining families |

These Vulkan calls return `void`, so do not pass them to `CheckXr`.
A queue family is a group of queues with shared capabilities. Family index `0`
and queue index `0` are different concepts, even when both happen to be zero.

The output index is meaningful only after a `true` return. Finding a family
does not create a logical device or request a queue.

Checkpoint on this device:

```text
Queue family 0: queues=1, graphics=yes
Selected graphics queue family: 0
```

Reference: [vkGetPhysicalDeviceQueueFamilyProperties][vk-queue-properties].

## 8. Create a Vulkan logical device

```cpp
bool CreateGraphicsDevice(XrInstance instance,
                          XrSystemId system,
                          VkPhysicalDevice physicalDevice,
                          uint32_t graphicsQueueFamily,
                          VkDevice& device,
                          VkQueue& queue)
{
    const auto createDevice = LoadXrFunction<PFN_xrCreateVulkanDeviceKHR>(
        instance, "xrCreateVulkanDeviceKHR");
    if (createDevice == nullptr)
    {
        return false;
    }

    const float queuePriority = 1.0f;
    VkDeviceQueueCreateInfo queueInfo{};
    queueInfo.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
    queueInfo.queueFamilyIndex = graphicsQueueFamily;
    queueInfo.queueCount = 1;
    queueInfo.pQueuePriorities = &queuePriority;

    VkDeviceCreateInfo deviceInfo{};
    deviceInfo.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
    deviceInfo.queueCreateInfoCount = 1;
    deviceInfo.pQueueCreateInfos = &queueInfo;

    XrVulkanDeviceCreateInfoKHR xrDeviceInfo{};
    xrDeviceInfo.type = XR_TYPE_VULKAN_DEVICE_CREATE_INFO_KHR;
    xrDeviceInfo.systemId = system;
    xrDeviceInfo.pfnGetInstanceProcAddr = vkGetInstanceProcAddr;
    xrDeviceInfo.vulkanPhysicalDevice = physicalDevice;
    xrDeviceInfo.vulkanCreateInfo = &deviceInfo;

    VkResult vulkanResult = VK_SUCCESS;
    if (!CheckXr(instance,
                 createDevice(instance, &xrDeviceInfo, &device, &vulkanResult),
                 "xrCreateVulkanDeviceKHR"))
    {
        return false;
    }
    if (vulkanResult != VK_SUCCESS)
    {
        std::fprintf(stderr, "Vulkan device creation failed: VkResult %d\n",
                     static_cast<int>(vulkanResult));
        return false;
    }
    std::puts("Vulkan logical device created through OpenXR.");

    vkGetDeviceQueue(device, graphicsQueueFamily, 0, &queue);
    if (queue == VK_NULL_HANDLE)
    {
        std::fprintf(stderr, "The graphics queue handle is null.\n");
        return false;
    }
    std::puts("Graphics queue retrieved.");
    return true;
}
```

### Queue request

- `queueFamilyIndex` is the discovered family, not a hard-coded assumption.
- `queueCount = 1` requests one queue.
- `pQueuePriorities` points to one priority value for that queue.
- Priority `1.0f` is a relative queue scheduling priority, not a guarantee that
  our work preempts the compositor.

### Vulkan request inside an OpenXR request

`VkDeviceCreateInfo` describes our requested queues, features, and extensions.
We request no optional application features/extensions yet.

`XrVulkanDeviceCreateInfoKHR` identifies the HMD system and physical GPU and
passes our Vulkan configuration to OpenXR. `pfnGetInstanceProcAddr` supplies
Vulkan's function lookup entry point. The runtime incorporates its integration
requirements; this does not automatically enable arbitrary optional features
our application may want later.

The local priority and configuration structures need to remain alive during
the creation call, not for the lifetime of the created device.

### Two result channels

`xrCreateVulkanDeviceKHR` returns an `XrResult` and writes a `VkResult`.
Both must succeed. Check the OpenXR result before interpreting the Vulkan
creation outcome. Do not run `CheckXr` on `VkResult`; the APIs have different
result domains.

### Retrieve the queue

`vkGetDeviceQueue` returns `void`. Queue index `0` means the first queue within
the selected family. The queue belongs to the logical device and needs no
separate destruction.

The caller owns `device`, including if a later queue check fails.
Keeping that output in `main` lets cleanup run after an early return.

References: [xrCreateVulkanDeviceKHR][xr-create-device],
[XrVulkanDeviceCreateInfoKHR][xr-device-info],
[VkDeviceQueueCreateInfo][vk-queue-info], [vkGetDeviceQueue][vk-get-queue].

## 9. Create the graphics-bound OpenXR session

```cpp
bool CreateGraphicsSession(XrInstance instance,
                           XrSystemId system,
                           VkInstance vulkanInstance,
                           VkPhysicalDevice physicalDevice,
                           VkDevice device,
                           uint32_t graphicsQueueFamily,
                           XrSession& session)
{
    XrGraphicsBindingVulkan2KHR binding{};
    binding.type = XR_TYPE_GRAPHICS_BINDING_VULKAN2_KHR;
    binding.instance = vulkanInstance;
    binding.physicalDevice = physicalDevice;
    binding.device = device;
    binding.queueFamilyIndex = graphicsQueueFamily;
    binding.queueIndex = 0;

    XrSessionCreateInfo sessionInfo{};
    sessionInfo.type = XR_TYPE_SESSION_CREATE_INFO;
    sessionInfo.next = &binding;
    sessionInfo.systemId = system;

    if (!CheckXr(instance, xrCreateSession(instance, &sessionInfo, &session),
                 "xrCreateSession"))
    {
        return false;
    }
    std::puts("Vulkan-backed OpenXR session created.");
    return true;
}
```

### Bind the correct resources

| Member | Meaning |
| --- | --- |
| `binding.instance` | The Vulkan instance, not the OpenXR instance |
| `physicalDevice` | GPU selected by the runtime |
| `device` | Logical device created on that GPU |
| `queueFamilyIndex` | Graphics queue's family |
| `queueIndex` | Queue within that family; we requested only index zero |

### Use the extension structure chain

`sessionInfo.next = &binding` attaches graphics-specific data to the otherwise
graphics-independent session request. The runtime recognizes the linked
structure using its `type`.

The local binding structure only needs to survive the creation call. The
referenced Vulkan objects must remain alive while the session uses them.
Destroy the session before destroying the logical device and Vulkan instance.

**Session created does not mean session running.** This code neither calls
`xrBeginSession` nor submits a frame.

Checkpoint: `Vulkan-backed OpenXR session created.`

References: [xrCreateSession][xr-create-session],
[XrSessionCreateInfo][xr-session-info],
[XrGraphicsBindingVulkan2KHR][xr-binding].

## 10. Connect the graphics setup stages

The following consecutive blocks form one `ProbeGraphics` function. Its
output references leave resource ownership with `main`, even if setup fails.
Each failed prerequisite returns immediately; cleanup remains in `main`.

### 10.1 Identify the selected runtime

```cpp
bool ProbeGraphics(XrInstance instance,
                   VkInstance& vulkanInstance,
                   VkDevice& device,
                   XrSession& session)
{
    XrInstanceProperties properties{};
    properties.type = XR_TYPE_INSTANCE_PROPERTIES;
    if (!CheckXr(instance, xrGetInstanceProperties(instance, &properties),
                 "xrGetInstanceProperties"))
    {
        return false;
    }
    std::printf("Runtime: %s\n", properties.runtimeName);
    PrintXrVersion("Version", properties.runtimeVersion);
```

`xrGetInstanceProperties` fills an output structure. Read it only after success.
The reported runtime version is not our application version or requested
OpenXR API version.

This is direct evidence of the runtime selected by this process, stronger than
finding a manifest on disk. Recorded result: `SteamVR/OpenXR`, `2.17.10`.

Reference: [xrGetInstanceProperties][xr-instance-properties].

### 10.2 Discover an HMD system

```cpp
    XrSystemGetInfo systemInfo{};
    systemInfo.type = XR_TYPE_SYSTEM_GET_INFO;
    systemInfo.formFactor = XR_FORM_FACTOR_HEAD_MOUNTED_DISPLAY;
    XrSystemId system = XR_NULL_SYSTEM_ID;
    if (!CheckXr(instance, xrGetSystem(instance, &systemInfo, &system),
                 "xrGetSystem"))
    {
        return false;
    }
    std::puts("HMD system discovered.");
```

- `formFactor` requests a head-mounted-display system.
- `XR_NULL_SYSTEM_ID` initializes the identifier to an invalid value.
- `&system` is the output location.
- An instance can exist while an HMD is unavailable, so this is a separate
  checkpoint.
- There is no `xrDestroySystem`: this ID is not separately owned.

Reference: [xrGetSystem][xr-get-system], [XrSystemGetInfo][xr-system-info].

### 10.2a Verify stereo support

Define this helper before `ProbeGraphics`. It is a core OpenXR command, so it
does not need `LoadXrFunction`.

```cpp
bool CheckStereoViewConfiguration(XrInstance instance, XrSystemId system)
{
    uint32_t count = 0;
    if (!CheckXr(instance,
                 xrEnumerateViewConfigurations(
                     instance, system, 0, &count, nullptr),
                 "xrEnumerateViewConfigurations(count)"))
    {
        return false;
    }
    if (count == 0)
    {
        std::fprintf(stderr, "The HMD system reports no view configurations.\n");
        return false;
    }

    std::vector<XrViewConfigurationType> configurations(count);
    if (!CheckXr(instance,
                 xrEnumerateViewConfigurations(
                     instance, system,
                     static_cast<uint32_t>(configurations.size()),
                     &count, configurations.data()),
                 "xrEnumerateViewConfigurations(list)"))
    {
        return false;
    }

    bool supportsStereo = false;
    for (uint32_t i = 0; i < count; ++i)
    {
        const auto configuration = configurations[i];
        const bool isStereo =
            configuration == XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO;
        std::printf("View configuration: %d%s\n",
                    static_cast<int>(configuration),
                    isStereo ? " (PRIMARY_STEREO)" : "");
        supportsStereo = supportsStereo || isStereo;
    }
    if (!supportsStereo)
    {
        std::fprintf(stderr,
                     "This probe requires the PRIMARY_STEREO configuration.\n");
        return false;
    }
    std::puts("Primary stereo view configuration supported.");
    return true;
}
```

Call it immediately after HMD discovery, before querying Vulkan requirements:

```cpp
    if (!CheckStereoViewConfiguration(instance, system))
    {
        return false;
    }
```

The count/fill calls enumerate arrangements of views supported by this system.
The entries are enum values, not structures, so they need no `type` fields.
The comparison checks for ordinary binocular VR, rather than assuming support.
We will later pass this configuration to `xrBeginSession`.

This helper reports enumeration errors, including insufficient capacity, and
stops rather than reading failed output. It does not change the session state.

Verified output:

```text
View configuration: 2 (PRIMARY_STEREO)
Primary stereo view configuration supported.
```

Reference: [xrEnumerateViewConfigurations](https://registry.khronos.org/OpenXR/specs/1.1/man/html/xrEnumerateViewConfigurations.html).

### 10.2b Query recommended eye-image dimensions

Status: verified on 2026-10-15. Define this helper before
`ProbeGraphics`, then call it after `CheckStereoViewConfiguration` succeeds.
These are additional helper definitions, not code to paste inside the
`ProbeGraphics` body.

```cpp
bool InspectStereoViews(XrInstance instance, XrSystemId system)
{
    constexpr auto configuration = XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO;
    uint32_t count = 0;
    if (!CheckXr(instance,
                 xrEnumerateViewConfigurationViews(
                     instance, system, configuration, 0, &count, nullptr),
                 "xrEnumerateViewConfigurationViews(count)"))
    {
        return false;
    }
    if (count != 2)
    {
        std::fprintf(stderr, "Expected two primary stereo views; got %u.\n", count);
        return false;
    }

    std::vector<XrViewConfigurationView> views(count);
    for (auto& view : views)
    {
        view.type = XR_TYPE_VIEW_CONFIGURATION_VIEW;
    }
    if (!CheckXr(instance,
                 xrEnumerateViewConfigurationViews(
                     instance, system, configuration,
                     static_cast<uint32_t>(views.size()), &count, views.data()),
                 "xrEnumerateViewConfigurationViews(list)"))
    {
        return false;
    }
    if (count != 2)
    {
        std::fprintf(stderr, "Stereo view query returned %u views, expected two.\n", count);
        return false;
    }

    for (uint32_t i = 0; i < count; ++i)
    {
        const auto& view = views[i];
        std::printf(
            "View %u: recommended=%ux%u, maximum=%ux%u, samples=%u (max %u)\n",
            i, view.recommendedImageRectWidth, view.recommendedImageRectHeight,
            view.maxImageRectWidth, view.maxImageRectHeight,
            view.recommendedSwapchainSampleCount, view.maxSwapchainSampleCount);
    }
    return true;
}
```

Call site:

```cpp
    if (!InspectStereoViews(instance, system))
    {
        return false;
    }
```

| Part | Explanation |
| --- | --- |
| `configuration` | Ask specifically about the stereo configuration already verified |
| Count/fill calls | Discover the view count, then retrieve each view's configuration |
| Two-view checks | Reject an unexpected shape for this binocular exercise |
| `XrViewConfigurationView` | Per-view image size and sample-count recommendations/limits |
| `view.type` | Identifies each output structure; zero-initialization leaves `next` null |
| Recommended dimensions | Starting point for later eye-image sizing, not desktop-window dimensions |
| Maximum dimensions | Limits, not a recommendation to allocate the largest images |
| Sample counts | Recommended and maximum multisampling counts, not the number of eyes |

This query needs no Vulkan objects or session. It supplies sizing information,
not eye poses, FOV angles, swapchain formats, or images. Those come from later
queries. We have not selected a rendering sample count or allocated textures.
Swapchain choices must also satisfy the graphics API's format/usage capabilities.

Build with the existing command. Expect two `View ...` lines; record the actual
values instead of assuming they match panel resolution or each other.
The existing READY observation and cleanup should still follow.

Recorded result for both views: recommended 1728x1728, maximum 8192x8192,
recommended swapchain sample count 1, maximum swapchain sample count 1.
The run also reached READY and printed the expected cleanup messages.
These are the settings reported by this runtime at the time of the query,
not immutable headset specifications. A sample count of 1 means these
swapchain images are not multisampled; it does not prohibit every other
anti-aliasing technique or a separately validated intermediate render target.

Reference: [xrEnumerateViewConfigurationViews](https://registry.khronos.org/OpenXR/specs/1.1/man/html/xrEnumerateViewConfigurationViews.html),
[XrViewConfigurationView](https://registry.khronos.org/OpenXR/specs/1.1/man/html/XrViewConfigurationView.html).

### 10.2c Query environment blend modes

Status: verified on 2026-10-15: OPAQUE (1) and ALPHA_BLEND (3) were advertised.
Define this helper before
`ProbeGraphics`, then call it after `InspectStereoViews` succeeds.

An environment blend mode describes how submitted imagery combines with the
environment. It is not ordinary material alpha blending within the scene.

| Mode | Meaning |
| --- | --- |
| `OPAQUE` | Application imagery replaces the environment |
| `ADDITIVE` | Application color is added to the environment |
| `ALPHA_BLEND` | Application alpha controls blending with the environment |

For this initial VR probe, explicitly require `OPAQUE`. Enumerate instead of
assuming the headset/runtime supports it. The query does not select a mode;
later, `xrEndFrame` specifies the selected mode on each submitted frame.

```cpp
bool CheckOpaqueBlendMode(XrInstance instance, XrSystemId system)
{
    constexpr auto configuration = XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO;
    uint32_t count = 0;
    if (!CheckXr(instance,
                 xrEnumerateEnvironmentBlendModes(
                     instance, system, configuration, 0, &count, nullptr),
                 "xrEnumerateEnvironmentBlendModes(count)"))
    {
        return false;
    }
    if (count == 0)
    {
        std::fprintf(stderr, "Stereo configuration reports no blend modes.\n");
        return false;
    }

    std::vector<XrEnvironmentBlendMode> modes(count);
    if (!CheckXr(instance,
                 xrEnumerateEnvironmentBlendModes(
                     instance, system, configuration,
                     static_cast<uint32_t>(modes.size()), &count, modes.data()),
                 "xrEnumerateEnvironmentBlendModes(list)"))
    {
        return false;
    }

    bool supportsOpaque = false;
    for (uint32_t i = 0; i < count; ++i)
    {
        const bool isOpaque = modes[i] == XR_ENVIRONMENT_BLEND_MODE_OPAQUE;
        std::printf("Environment blend mode: %d%s\n",
                    static_cast<int>(modes[i]), isOpaque ? " (OPAQUE)" : "");
        supportsOpaque = supportsOpaque || isOpaque;
    }
    if (!supportsOpaque)
    {
        std::fprintf(stderr, "This VR probe requires OPAQUE environment blending.\n");
        return false;
    }
    std::puts("Opaque environment blend mode supported.");
    return true;
}
```

Call site inside `ProbeGraphics`, immediately after the eye-size query:

```cpp
    if (!CheckOpaqueBlendMode(instance, system))
    {
        return false;
    }
```

The first call obtains a count, the vector allocates enum entries, and the
second call fills them. Enum entries need no `type` fields. The system and view
configuration scope the query; it needs no Vulkan objects or OpenXR session.
The loop prints the advertised modes and checks for opaque support. Failure,
including insufficient array capacity, is reported before reading output.

Expected new output includes `Environment blend mode: 1 (OPAQUE)` and
`Opaque environment blend mode supported.` Other modes may also be listed.
Use the existing build command; READY observation and cleanup still follow.
Neither this query nor the image-size query begins a session or displays an image.

References: [xrEnumerateEnvironmentBlendModes](https://registry.khronos.org/OpenXR/specs/1.1/man/html/xrEnumerateEnvironmentBlendModes.html),
[XrEnvironmentBlendMode](https://registry.khronos.org/OpenXR/specs/1.1/man/html/XrEnvironmentBlendMode.html).

### 10.3 Query Vulkan requirements and choose a version

```cpp
    const auto getRequirements =
        LoadXrFunction<PFN_xrGetVulkanGraphicsRequirements2KHR>(
            instance, "xrGetVulkanGraphicsRequirements2KHR");
    if (getRequirements == nullptr)
    {
        return false;
    }

    XrGraphicsRequirementsVulkan2KHR requirements{};
    requirements.type = XR_TYPE_GRAPHICS_REQUIREMENTS_VULKAN2_KHR;
    if (!CheckXr(instance, getRequirements(instance, system, &requirements),
                 "xrGetVulkanGraphicsRequirements2KHR"))
    {
        return false;
    }
    PrintXrVersion("Minimum Vulkan API", requirements.minApiVersionSupported);
    PrintXrVersion("Maximum Vulkan API", requirements.maxApiVersionSupported);

    const XrVersion requestedVersion = XR_MAKE_VERSION(1, 2, 0);
    if (requestedVersion < requirements.minApiVersionSupported ||
        requestedVersion > requirements.maxApiVersionSupported)
    {
        std::fprintf(stderr,
                     "This probe requires Vulkan 1.2 within the runtime's range.\n");
        return false;
    }
```

The lookup requests the exact extension function signature. The query supplies
requirements for this HMD system.

Only compare fields after a successful query. The range is represented with
OpenXR's version encoding even though it describes Vulkan support.

This exercise explicitly requires Vulkan 1.2 and reports failure if it falls
outside the runtime's range. There is no silent fallback.

The recorded `1.0.0` to `1.2.0` range does not say the GPU only supports Vulkan
1.2. The runtime's graphics integration and the GPU driver can expose different
version limits.

Reference: [xrGetVulkanGraphicsRequirements2KHR][xr-requirements].

### 10.4 Create the Vulkan instance through OpenXR

```cpp
    const auto createVulkanInstance =
        LoadXrFunction<PFN_xrCreateVulkanInstanceKHR>(
            instance, "xrCreateVulkanInstanceKHR");
    if (createVulkanInstance == nullptr)
    {
        return false;
    }

    VkApplicationInfo applicationInfo{};
    applicationInfo.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    applicationInfo.pApplicationName = "OpenBlack XR Probe";
    applicationInfo.applicationVersion = 1;
    applicationInfo.apiVersion = VK_API_VERSION_1_2;

    VkInstanceCreateInfo vulkanInfo{};
    vulkanInfo.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    vulkanInfo.pApplicationInfo = &applicationInfo;

    XrVulkanInstanceCreateInfoKHR xrVulkanInfo{};
    xrVulkanInfo.type = XR_TYPE_VULKAN_INSTANCE_CREATE_INFO_KHR;
    xrVulkanInfo.systemId = system;
    xrVulkanInfo.pfnGetInstanceProcAddr = vkGetInstanceProcAddr;
    xrVulkanInfo.vulkanCreateInfo = &vulkanInfo;

    VkResult vulkanResult = VK_SUCCESS;
    if (!CheckXr(instance,
                 createVulkanInstance(instance, &xrVulkanInfo,
                                      &vulkanInstance, &vulkanResult),
                 "xrCreateVulkanInstanceKHR"))
    {
        return false;
    }
    if (vulkanResult != VK_SUCCESS)
    {
        std::fprintf(stderr, "Vulkan instance creation failed: VkResult %d\n",
                     static_cast<int>(vulkanResult));
        return false;
    }
    std::puts("Vulkan instance created through OpenXR.");
```

| Block | Explanation |
| --- | --- |
| Function lookup | Retrieves the enabled extension's creation command |
| `VkApplicationInfo` | Names the application and requests Vulkan API 1.2 |
| `VkInstanceCreateInfo` | Points to our Vulkan application description |
| OpenXR wrapper | Identifies the HMD system and supplies Vulkan's lookup function |
| Creation call | Lets the runtime incorporate its Vulkan instance requirements |
| Two checks | Require both OpenXR and underlying Vulkan success |

`XR_MAKE_VERSION(1, 2, 0)` and `VK_API_VERSION_1_2` express the same version using
different encodings. Do not assign the OpenXR-packed value to Vulkan's
`applicationInfo.apiVersion`.

No desktop surface or SDL window is needed. The eventual target is an OpenXR
swapchain. Although OpenXR assists with creation, our application owns the
Vulkan instance and must destroy it.

References: [xrCreateVulkanInstanceKHR][xr-create-instance-vk],
[XrVulkanInstanceCreateInfoKHR][xr-instance-info-vk],
[VkInstanceCreateInfo][vk-instance-info].

### 10.5 Ask OpenXR which physical GPU to use

```cpp
    const auto getGraphicsDevice =
        LoadXrFunction<PFN_xrGetVulkanGraphicsDevice2KHR>(
            instance, "xrGetVulkanGraphicsDevice2KHR");
    if (getGraphicsDevice == nullptr)
    {
        return false;
    }

    XrVulkanGraphicsDeviceGetInfoKHR deviceInfo{};
    deviceInfo.type = XR_TYPE_VULKAN_GRAPHICS_DEVICE_GET_INFO_KHR;
    deviceInfo.systemId = system;
    deviceInfo.vulkanInstance = vulkanInstance;

    VkPhysicalDevice physicalDevice = VK_NULL_HANDLE;
    if (!CheckXr(instance,
                 getGraphicsDevice(instance, &deviceInfo, &physicalDevice),
                 "xrGetVulkanGraphicsDevice2KHR"))
    {
        return false;
    }
    if (physicalDevice == VK_NULL_HANDLE)
    {
        std::fprintf(stderr, "OpenXR returned a null physical device.\n");
        return false;
    }

    VkPhysicalDeviceProperties deviceProperties{};
    vkGetPhysicalDeviceProperties(physicalDevice, &deviceProperties);
    std::printf("OpenXR-selected GPU: %s\n", deviceProperties.deviceName);
    std::printf("GPU Vulkan API: %u.%u.%u\n",
                VK_VERSION_MAJOR(deviceProperties.apiVersion),
                VK_VERSION_MINOR(deviceProperties.apiVersion),
                VK_VERSION_PATCH(deviceProperties.apiVersion));
```

This asks: "For this HMD system and this Vulkan instance, which GPU should the
application use?"

Do not independently select the first device from Vulkan enumeration.
The physical-device handle belongs to the Vulkan instance and has no separate
destruction call.

`vkGetPhysicalDeviceProperties` returns `void`. Its `apiVersion` uses Vulkan's
version encoding, hence `VK_VERSION_*`.

Recorded output:

```text
OpenXR-selected GPU: Turnip Adreno (TM) 750
GPU Vulkan API: 1.4.359
```

References: [xrGetVulkanGraphicsDevice2KHR][xr-get-gpu],
[vkGetPhysicalDeviceProperties][vk-properties].

### 10.6 Connect queue, device, and session creation

```cpp
    uint32_t graphicsQueueFamily = 0;
    if (!FindGraphicsQueueFamily(physicalDevice, graphicsQueueFamily))
    {
        return false;
    }
    std::printf("Selected graphics queue family: %u\n", graphicsQueueFamily);

    VkQueue graphicsQueue = VK_NULL_HANDLE;
    if (!CreateGraphicsDevice(instance, system, physicalDevice,
                              graphicsQueueFamily, device, graphicsQueue))
    {
        return false;
    }

    return CreateGraphicsSession(instance, system, vulkanInstance,
                                 physicalDevice, device,
                                 graphicsQueueFamily, session);
}
```

The initial family value `0` is not evidence that it is suitable; only a
successful query makes the output valid.

Each stage runs only after its prerequisites succeed. `graphicsQueue` is
retrieved and validated but not used to submit work yet. The session binding
identifies its family and index rather than passing the queue handle itself.

Later rendering lessons will need to retain more graphics state beyond this
function. For this setup-only probe, the owned objects return through references
and the remaining identifiers are used locally.

## 11. Observe session lifecycle events

Verified on 2026-10-15: the probe reported `IDLE` (1), then `READY` (2), and
deliberately did not begin the session. The following code implements that
observation exercise.

### The states we are learning

| State | Meaning for the next application step |
| --- | --- |
| `IDLE` | Session exists but is not ready to begin |
| `READY` | Application may call `xrBeginSession` |
| `SYNCHRONIZED` | Running session with runtime timing coordination |
| `VISIBLE` | Application output is visible |
| `FOCUSED` | Session has input focus |
| `STOPPING` | Application must end the running session |
| `LOSS_PENDING` | Session loss is pending |
| `EXITING` | Runtime requests application exit |

This is a learning summary, not a guaranteed linear sequence. Read the state
machine rules before implementing a running-session loop.

We only observe here. We do not begin a session and then leave it without a
frame loop.

```cpp
bool ObserveSessionEvents(XrInstance instance, XrSession session)
{
    using Clock = std::chrono::steady_clock;
    const auto deadline = Clock::now() + std::chrono::seconds(5);
    bool sawReady = false;

    while (Clock::now() < deadline)
    {
        XrEventDataBuffer event{};
        event.type = XR_TYPE_EVENT_DATA_BUFFER;
        const XrResult result = xrPollEvent(instance, &event);

        if (result == XR_EVENT_UNAVAILABLE)
        {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
            continue;
        }
        if (!CheckXr(instance, result, "xrPollEvent"))
        {
            return false;
        }

        if (event.type == XR_TYPE_EVENT_DATA_SESSION_STATE_CHANGED)
        {
            const auto& changed =
                reinterpret_cast<const XrEventDataSessionStateChanged&>(event);
            if (changed.session != session)
            {
                std::fprintf(stderr,
                             "Received a state event for an unexpected session.\n");
                return false;
            }

            std::printf("Session state changed: %d\n",
                        static_cast<int>(changed.state));
            if (changed.state == XR_SESSION_STATE_READY)
            {
                sawReady = true;
                std::puts("Session is READY; not beginning it in this exercise.");
            }
            if (changed.state == XR_SESSION_STATE_EXITING ||
                changed.state == XR_SESSION_STATE_LOSS_PENDING)
            {
                std::fprintf(stderr,
                             "Runtime requested exit or reported session loss.\n");
                return false;
            }
        }
        else if (event.type == XR_TYPE_EVENT_DATA_INSTANCE_LOSS_PENDING)
        {
            std::fprintf(stderr, "Runtime reported pending instance loss.\n");
            return false;
        }
        else
        {
            std::printf("Other OpenXR event type: %d\n",
                        static_cast<int>(event.type));
        }
    }

    if (!sawReady)
    {
        std::fprintf(stderr,
                     "Session did not reach READY during the observation period.\n");
        return false;
    }
    return true;
}
```

### Timing and polling

- `steady_clock` measures elapsed time without wall-clock adjustments.
- The five-second deadline bounds this diagnostic; it is not an OpenXR rule.
- `xrPollEvent` returns immediately, including when no event exists.
- A ten-millisecond sleep avoids a busy loop when the queue is empty.
- A fresh buffer is initialized on every iteration because polling overwrites
  `type` with the returned event's type.

### Successful status does not always mean output exists

`XR_EVENT_UNAVAILABLE` is a non-error result, so `CheckXr` would accept it.
However, it means there is no event to interpret. Handle it before inspecting
the buffer.

This is why result checking cannot replace command-specific semantics.

### Interpret the event only after checking its type

The generic buffer can hold different event structures. Check `event.type`
before casting it to `XrEventDataSessionStateChanged`.

The session comparison verifies that the event belongs to our session. A
multi-session application would route events instead of treating another
session as unexpected.

The helper reports loss/exit conditions and returns failure for this exercise.
Other event types are logged rather than silently ignored; a production loop
will need to interpret relevant ones, including event-loss notifications.

### What counts as success?

Potential output:

```text
Session state changed: 1
Session state changed: 2
Session is READY; not beginning it in this exercise.
```

`1` is `IDLE`, and `2` is `READY`. Do not require that exact sequence.
The helper continues observing until its deadline. Failure to see `READY`
within five seconds is a diagnostic timeout, not proof of a permanent runtime
failure.

References: [xrPollEvent][xr-poll], [XrSessionState][xr-state],
[XrEventDataSessionStateChanged][xr-state-event].

## 12. Create the OpenXR instance and coordinate cleanup

All helper definitions above precede this entry point.

```cpp
int main()
{
    if (!CheckRequiredExtension())
    {
        return EXIT_FAILURE;
    }

    const char* enabledExtensions[] = {
        XR_KHR_VULKAN_ENABLE2_EXTENSION_NAME
    };

    XrInstanceCreateInfo createInfo{};
    createInfo.type = XR_TYPE_INSTANCE_CREATE_INFO;
    std::strcpy(createInfo.applicationInfo.applicationName, "OpenBlack XR Probe");
    createInfo.applicationInfo.applicationVersion = 1;
    createInfo.applicationInfo.apiVersion = XR_MAKE_VERSION(1, 0, 0);
    createInfo.enabledExtensionCount = 1;
    createInfo.enabledExtensionNames = enabledExtensions;

    XrInstance instance = XR_NULL_HANDLE;
    if (!CheckXr(XR_NULL_HANDLE, xrCreateInstance(&createInfo, &instance),
                 "xrCreateInstance"))
    {
        return EXIT_FAILURE;
    }

    VkInstance vulkanInstance = VK_NULL_HANDLE;
    VkDevice device = VK_NULL_HANDLE;
    XrSession session = XR_NULL_HANDLE;
    bool success = ProbeGraphics(instance, vulkanInstance, device, session);

    if (success)
    {
        success = ObserveSessionEvents(instance, session);
    }

    if (session != XR_NULL_HANDLE)
    {
        const bool destroyed = CheckXr(
            instance, xrDestroySession(session), "xrDestroySession");
        if (destroyed)
        {
            std::puts("OpenXR session destroyed.");
        }
        success = success && destroyed;
    }

    if (device != VK_NULL_HANDLE)
    {
        vkDestroyDevice(device, nullptr);
        std::puts("Vulkan logical device destroyed.");
    }
    if (vulkanInstance != VK_NULL_HANDLE)
    {
        vkDestroyInstance(vulkanInstance, nullptr);
        std::puts("Vulkan instance destroyed.");
    }

    const XrResult destroyResult = xrDestroyInstance(instance);
    const bool destroyed = CheckXr(
        XR_NULL_HANDLE, destroyResult, "xrDestroyInstance");
    success = success && destroyed;
    return success ? EXIT_SUCCESS : EXIT_FAILURE;
}
```

### Instance configuration

| Member | Explanation |
| --- | --- |
| `type` | Identifies the creation structure |
| `applicationName` | Name reported to the runtime; this fixed literal fits the array |
| `applicationVersion = 1` | Our probe's version, not the OpenXR version |
| `apiVersion` | Requests OpenXR 1.0 core functionality |
| `enabledExtensionCount` | Number of names in our requested extension array |
| `enabledExtensionNames` | Explicitly enables the Vulkan integration extension |

Requesting OpenXR 1.0 is intentional: this probe uses core 1.0 functionality
plus the Vulkan extension. Newer installed headers do not guarantee newer
runtime core API support.

Other zero-initialized members leave API layers, flags, and the instance
creation `next` chain unspecified/empty. Extension selection identifies a
capability, not a hard-coded runtime.

`xrCreateInstance(&createInfo, &instance)` passes configuration in and receives
the handle through an output pointer. A failure here leaves no successfully
created instance to clean up.

### Coordinate the exercise

`ProbeGraphics` performs setup, storing created handles in `main`'s variables.
`ObserveSessionEvents` runs only if setup succeeded.

Keep the cleanup blocks outside that success condition. A failed setup or
observation must not skip cleanup of objects already created.

### Ownership and teardown

```text
XrSession
    ↓ destroy before its graphics resources
VkDevice
    ↓ destroy before its parent Vulkan instance
VkInstance
    ↓
XrInstance
```

- There are no separate destruction calls for `XrSystemId`, `VkPhysicalDevice`,
  or `VkQueue`.
- `vkDestroyDevice` and `vkDestroyInstance` return `void`.
- We have not submitted GPU work, so there is no application GPU work to wait
  for in this exercise.
- We have not begun the session, so no running session needs `xrEndSession`.
- Once rendering starts, synchronization and session shutdown need additional
  handling; this setup-only teardown is not a complete rendering shutdown plan.

Compute cleanup results before accumulating `success`. Writing
`success && cleanupCall()` would skip the call if `success` were already false.
Here, `destroyed` is evaluated first, so earlier failure does not bypass cleanup.

After `xrDestroyInstance`, pass `XR_NULL_HANDLE` to the checker rather than
use the destroyed handle for symbolic error conversion.

References: [xrCreateInstance][xr-create-instance],
[XrInstanceCreateInfo][xr-instance-info], [XrApplicationInfo][xr-app-info],
[xrDestroySession][xr-destroy-session], [vkDestroyDevice][vk-destroy-device],
[vkDestroyInstance][vk-destroy-instance], [xrDestroyInstance][xr-destroy-instance].

## 13. Build, run, and interpret the result

Build with the command in section 3, then:

```sh
./xr_probe
printf 'Exit status: %s\n' "$?"
```

Capture the exit status immediately; another command would replace it.
Exit status zero means this exercise's checks completed successfully, not that
VR rendering is implemented.

### Last verified setup output

The full extension list is omitted here for readability:

```text
XR_KHR_vulkan_enable2 advertised: yes
Runtime: SteamVR/OpenXR
Version: 2.17.10
HMD system discovered.
Minimum Vulkan API: 1.0.0
Maximum Vulkan API: 1.2.0
Vulkan instance created through OpenXR.
OpenXR-selected GPU: Turnip Adreno (TM) 750
GPU Vulkan API: 1.4.359
Queue family 0: queues=1, graphics=yes
Selected graphics queue family: 0
Vulkan logical device created through OpenXR.
Graphics queue retrieved.
Vulkan-backed OpenXR session created.
OpenXR session destroyed.
Vulkan logical device destroyed.
Vulkan instance destroyed.
```

The event exercise added the following verified messages after session creation:

```text
Session state changed: 1
Session state changed: 2
Session is READY; not beginning it in this exercise.
```

The subsequent run also verified `PRIMARY_STEREO` support and included session,
logical-device, and Vulkan-instance destruction messages after `READY`. Its
numeric exit status was not included.

`fdm_injection: XR layer loaded` also appeared in prior output. It is a layer's
diagnostic message, not an error; its purpose has not been established.
Standard output and error can interleave, so its position can vary.

### Troubleshooting by stage

| Symptom | Stage and next check |
| --- | --- |
| Header not found | Compilation: verify installed development headers |
| Undefined reference to `xr...` | Linking: use the verified loader command; runtime overrides cannot fix a link error |
| GCC rejects `-rpath,...` | Driver arguments: use the full `-Wl,...` token or `-Xlinker` form |
| `xrCreateInstance` fails | Runtime discovery/initialization: retain the exact result and launch context |
| Runtime properties work but `xrGetSystem` fails | HMD availability: do not assume instance creation proves headset readiness |
| Function lookup fails | Check extension enabling and the exact function name/type |
| Vulkan creation fails | Preserve both API result domains; do not confuse `VkResult` with `XrResult` |
| No event messages | Ensure `ObserveSessionEvents` is actually called after successful setup |
| No `READY` before timeout | Investigate runtime/headset/launch context; five seconds is a diagnostic policy |
| No headset picture | Expected: this tutorial has not begun rendering |

The tested terminal launch is not yet proof that the final SteamVR app launch
route, reboot/relaunch behavior, or suspend/resume works.

## 14. Next lessons and how to extend this document

### Frame-loop building block: submit an empty frame

Status: teaching code only, not yet executed. Define this helper before the
future running-session loop. Do not call it from setup or the current
observation-only helper: it requires a successfully begun session.

```cpp
bool SubmitEmptyFrame(XrInstance instance, XrSession session)
{
    XrFrameWaitInfo waitInfo{};
    waitInfo.type = XR_TYPE_FRAME_WAIT_INFO;
    XrFrameState frameState{};
    frameState.type = XR_TYPE_FRAME_STATE;

    if (!CheckXr(instance, xrWaitFrame(session, &waitInfo, &frameState),
                 "xrWaitFrame"))
    {
        return false;
    }

    XrFrameBeginInfo beginInfo{};
    beginInfo.type = XR_TYPE_FRAME_BEGIN_INFO;
    if (!CheckXr(instance, xrBeginFrame(session, &beginInfo), "xrBeginFrame"))
    {
        return false;
    }

    XrFrameEndInfo endInfo{};
    endInfo.type = XR_TYPE_FRAME_END_INFO;
    endInfo.displayTime = frameState.predictedDisplayTime;
    endInfo.environmentBlendMode = XR_ENVIRONMENT_BLEND_MODE_OPAQUE;
    endInfo.layerCount = 0;
    endInfo.layers = nullptr;

    return CheckXr(instance, xrEndFrame(session, &endInfo), "xrEndFrame");
}
```

| Block | Meaning |
| --- | --- |
| `xrWaitFrame` | Runtime-paced wait that supplies the predicted display time and rendering guidance |
| `XrFrameState` | Output state, including `predictedDisplayTime`, `predictedDisplayPeriod`, and `shouldRender` |
| `xrBeginFrame` | Marks the beginning of this application's frame submission |
| `displayTime` | Uses the runtime's predicted timestamp, not wall-clock time |
| `environmentBlendMode` | Uses the opaque mode already verified for primary stereo |
| Zero layers | Ends a frame without submitting application imagery |
| `xrEndFrame` | Completes the begun frame and returns control of its submission to the runtime |

An empty frame is valid even when `shouldRender` is true: the application
chooses to submit no imagery. When `shouldRender` is false, the eventual
renderer must skip expensive rendering but still follow the frame lifecycle.
Do not return early on that flag after beginning a frame.

`XR_FRAME_DISCARDED` from `xrBeginFrame` is a non-error status. The frame still
needs its matching end call. Other non-error statuses, such as pending session
loss, also require interpretation by the session lifecycle owner; `CheckXr`
does not implement that policy.

This helper has no intervening rendering operations that could fail after
begin. Adding rendering later requires preserving end-frame pairing on those
additional failure paths. A failed frame operation is reported and must stop
normal submission; do not blindly continue with another frame.

No swapchain, eye images, view poses, or Vulkan command submission is needed
for this particular diagnostic. No application picture is expected, and the
runtime may continue showing its own loading or environment presentation.

The next lesson must wire this helper into a bounded session loop:

1. Poll events; begin with `PRIMARY_STEREO` only after `READY`.
2. Track whether `xrBeginSession` succeeded, rather than equating READY with running.
3. Submit frames while the session is running, including when not visible/focused.
4. Request exit after the diagnostic duration using `xrRequestExitSession`.
5. Continue handling lifecycle events until `STOPPING`, then call `xrEndSession`.
6. Handle runtime exit, loss, errors, and timeout explicitly, then release resources.

References: [xrWaitFrame](https://registry.khronos.org/OpenXR/specs/1.1/man/html/xrWaitFrame.html),
[xrBeginFrame](https://registry.khronos.org/OpenXR/specs/1.1/man/html/xrBeginFrame.html),
[xrEndFrame](https://registry.khronos.org/OpenXR/specs/1.1/man/html/xrEndFrame.html).

The next lesson begins after event observation succeeds:

1. Enumerate supported view configurations and environment blend modes.
2. Begin the session when the runtime reports `READY`.
3. Introduce a correctly paired `xrWaitFrame` / `xrBeginFrame` / `xrEndFrame`
   loop, including frames where rendering should be skipped.
4. Handle `STOPPING`, session loss, and exit without leaving a running session
   or a begun frame unfinished.
5. Create reference spaces and locate predicted views.
6. Create swapchains, acquire/wait/release images, and render a simple scene.
7. Verify both eyes and head tracking in the headset.
8. Add controller actions and pose spaces.
9. Prove a compatible bgfx/OpenXR ownership and synchronization model.
10. Integrate optional VR engine services while preserving desktop behavior.

Do not create an unrelated second graphics device inside OpenBlack and assume
bgfx can render into its images. The standalone probe establishes runtime
requirements; the later renderer bridge must establish shared-resource access
and synchronization.

### Maintenance format for future lessons

For each new lesson, add:

- The goal and prerequisites.
- A focused helper or setup block, with exact placement/call-site instructions.
- Explanations of parameters, output structures, result semantics, and ownership.
- Official reference links.
- Build changes, if any.
- Expected output, labeled as expected until observed.
- The actual recorded result and remaining limits after a successful run.

Update the checkpoint table and roadmap as results are verified. Keep this
document focused on the API learning path, not implementation-history notes.

## 15. Documentation bookshelf

- [OpenXR Tutorial](https://openxr-tutorial.com/): Introduction, Setup, Graphics
  using Vulkan, then Actions. Its graphics abstraction is instructional, not
  a drop-in bgfx bridge.
- [OpenXR specification][xr-spec]: authoritative API rules, lifecycle, ownership,
  and extensions.
- [OpenXR API reference](https://registry.khronos.org/OpenXR/specs/1.1/man/html/):
  individual commands and structures.
- [OpenXR loader specification](https://registry.khronos.org/OpenXR/specs/1.1/loader.html):
  discovery, dispatch, and loader/runtime separation.
- [OpenXR tutorial source](https://github.com/KhronosGroup/OpenXR-Tutorials).
- [OpenXR SDK source and samples](https://github.com/KhronosGroup/OpenXR-SDK-Source).
- [Vulkan documentation](https://docs.vulkan.org/): graphics-side concepts and
  command reference.

[xr-spec]: https://registry.khronos.org/OpenXR/specs/1.1/html/xrspec.html
[xr-result]: https://registry.khronos.org/OpenXR/specs/1.1/man/html/XrResult.html
[xr-result-string]: https://registry.khronos.org/OpenXR/specs/1.1/man/html/xrResultToString.html
[xr-get-proc]: https://registry.khronos.org/OpenXR/specs/1.1/man/html/xrGetInstanceProcAddr.html
[xr-enumerate]: https://registry.khronos.org/OpenXR/specs/1.1/man/html/xrEnumerateInstanceExtensionProperties.html
[xr-vulkan]: https://registry.khronos.org/OpenXR/specs/1.1/html/xrspec.html#XR_KHR_vulkan_enable2
[vk-queue-properties]: https://docs.vulkan.org/refpages/latest/refpages/source/vkGetPhysicalDeviceQueueFamilyProperties.html
[xr-create-device]: https://registry.khronos.org/OpenXR/specs/1.1/man/html/xrCreateVulkanDeviceKHR.html
[xr-device-info]: https://registry.khronos.org/OpenXR/specs/1.1/man/html/XrVulkanDeviceCreateInfoKHR.html
[vk-queue-info]: https://docs.vulkan.org/refpages/latest/refpages/source/VkDeviceQueueCreateInfo.html
[vk-get-queue]: https://docs.vulkan.org/refpages/latest/refpages/source/vkGetDeviceQueue.html
[xr-create-session]: https://registry.khronos.org/OpenXR/specs/1.1/man/html/xrCreateSession.html
[xr-session-info]: https://registry.khronos.org/OpenXR/specs/1.1/man/html/XrSessionCreateInfo.html
[xr-binding]: https://registry.khronos.org/OpenXR/specs/1.1/man/html/XrGraphicsBindingVulkan2KHR.html
[xr-instance-properties]: https://registry.khronos.org/OpenXR/specs/1.1/man/html/xrGetInstanceProperties.html
[xr-get-system]: https://registry.khronos.org/OpenXR/specs/1.1/man/html/xrGetSystem.html
[xr-system-info]: https://registry.khronos.org/OpenXR/specs/1.1/man/html/XrSystemGetInfo.html
[xr-requirements]: https://registry.khronos.org/OpenXR/specs/1.1/man/html/xrGetVulkanGraphicsRequirements2KHR.html
[xr-create-instance-vk]: https://registry.khronos.org/OpenXR/specs/1.1/man/html/xrCreateVulkanInstanceKHR.html
[xr-instance-info-vk]: https://registry.khronos.org/OpenXR/specs/1.1/man/html/XrVulkanInstanceCreateInfoKHR.html
[vk-instance-info]: https://docs.vulkan.org/refpages/latest/refpages/source/VkInstanceCreateInfo.html
[xr-get-gpu]: https://registry.khronos.org/OpenXR/specs/1.1/man/html/xrGetVulkanGraphicsDevice2KHR.html
[vk-properties]: https://docs.vulkan.org/refpages/latest/refpages/source/vkGetPhysicalDeviceProperties.html
[xr-poll]: https://registry.khronos.org/OpenXR/specs/1.1/man/html/xrPollEvent.html
[xr-state]: https://registry.khronos.org/OpenXR/specs/1.1/man/html/XrSessionState.html
[xr-state-event]: https://registry.khronos.org/OpenXR/specs/1.1/man/html/XrEventDataSessionStateChanged.html
[xr-create-instance]: https://registry.khronos.org/OpenXR/specs/1.1/man/html/xrCreateInstance.html
[xr-instance-info]: https://registry.khronos.org/OpenXR/specs/1.1/man/html/XrInstanceCreateInfo.html
[xr-app-info]: https://registry.khronos.org/OpenXR/specs/1.1/man/html/XrApplicationInfo.html
[xr-destroy-session]: https://registry.khronos.org/OpenXR/specs/1.1/man/html/xrDestroySession.html
[vk-destroy-device]: https://docs.vulkan.org/refpages/latest/refpages/source/vkDestroyDevice.html
[vk-destroy-instance]: https://docs.vulkan.org/refpages/latest/refpages/source/vkDestroyInstance.html
[xr-destroy-instance]: https://registry.khronos.org/OpenXR/specs/1.1/man/html/xrDestroyInstance.html
