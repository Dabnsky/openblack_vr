# Lesson 1 — What are we building, and what does the first OpenXR call do?

Keep the [OpenXR tutorial](https://openxr-tutorial.com/linux/vulkan/1-introduction.html)
as the reference, but work through its setup in smaller pieces. **You don't need
to finish extension enumeration yet.**

## 1. Our immediate goal

We're building a small **standalone C++ program that talks to OpenXR**.

It is not yet part of OpenBlack's game loop. For this first step, it won't render
anything, read tracking, or use controllers.

Eventually:

```text
OpenXR: headset lifecycle, tracking, input, and image submission
bgfx:   rendering OpenBlack's world into compatible images
```

The [OpenXR-bgfx example](https://github.com/VirtualGeo/OpenXR-bgfx) gives us a
useful architectural reference, but its Windows/D3D11 implementation does not
supply our Linux/Vulkan bridge. We can leave that problem aside while learning
the basics.

## 2. Three different things called "OpenXR"

Understanding these roles makes the prerequisites less mysterious:

| Component | Purpose |
| --- | --- |
| **Headers** | Tell the C++ compiler which OpenXR types and functions exist |
| **Loader library** | Provides the entry points our executable links against and routes calls |
| **Runtime** | Implements OpenXR for the headset; our intended runtime is SteamVR |

Think of these as **declarations, dispatch, and implementation**, not three
interchangeable installations.

This line supplies declarations:

```cpp
#include <openxr/openxr.h>
```

It does not start SteamVR or connect to a headset. Including a header is part of
compiling the program, not a runtime action.

Our earlier check found the headers usable, but the default loader link failed.
That is a separate build issue, not evidence that your C++ call is wrong. We'll
handle executable linking before asking you to run the probe.

For this lesson, you need a C++ compiler, the OpenXR headers, a linkable loader,
and an available runtime for the query. You do not need Vulkan, SDL, bgfx, game
data, or an OpenXR session.

## 3. Where this fits in the tutorial

The tutorial's [project setup](https://openxr-tutorial.com/linux/vulkan/1-introduction.html#project-setup)
prepares the build and supporting files.
[Chapter 2.1](https://openxr-tutorial.com/linux/vulkan/2-setup.html#creating-an-instance)
then introduces instance creation, including checking available extensions.

We're examining one small piece of that setup without introducing its entire
class or graphics helpers.

An **extension** is a named addition to OpenXR's core functionality. For example,
graphics integration uses extensions.

Two operations are different:

- **Enumerate:** discover what is available.
- **Enable:** request particular extensions when creating an instance.

Today's call only discovers availability. It enables nothing.

## 4. One C++ idea: passing a value versus passing an address

Consider:

```cpp
uint32_t extensionCount = 0;
```

This creates a variable:

- `uint32_t`: an unsigned, exactly 32-bit integer type used by this API.
- `extensionCount`: the name we chose.
- `= 0`: its initial value.
- `;`: the end of the statement.

OpenXR needs a way to write a new value into that variable.

```cpp
&extensionCount
```

Here, `&` means **"the address of `extensionCount`."** Passing this address lets
the function locate and modify the original variable.

The variable still belongs to our program. Passing its address does **not**
transfer ownership or require us to allocate memory manually.

## 5. Read just the first API call

```cpp
const XrResult countResult =
    xrEnumerateInstanceExtensionProperties(
        nullptr,
        0,
        &extensionCount,
        nullptr);
```

The function takes four arguments:

| Position | Parameter | Our argument | Meaning |
| --- | --- | --- | --- |
| 1 | `layerName` | `nullptr` | Request global extensions, not extensions for one named API layer |
| 2 | `propertyCapacityInput` | `0` | We are providing no space for extension records yet |
| 3 | `propertyCountOutput` | `&extensionCount` | Write the required number of records into this variable |
| 4 | `properties` | `nullptr` | We are not supplying an array of extension records |

`nullptr` means **a null pointer**: it does not point to an object. Its meaning
depends on the parameter; it is allowed in these positions for this count-only
query.

This call has **two outputs**:

```text
Function return value --> countResult       Did the query succeed?
Write through pointer --> extensionCount    How many records are needed?
```

`XrResult` is the API's result-code type. `const` prevents us from changing the
stored `countResult` afterward.

**Only trust the count after checking the result.** A failed query is not the
same as a successful query reporting zero extensions.

## 6. Ownership and cleanup at this point

This call creates no OpenXR instance or session.

- `extensionCount` and `countResult` are local variables.
- Their lifetimes end automatically when the function exits.
- There is no OpenXR handle to destroy.
- No Vulkan or bgfx resources are involved.

We'll discuss the vector and the second call separately. You can leave the
existing scaffold in place without needing to understand the rest today.

Official reference:
[`xrEnumerateInstanceExtensionProperties`](https://registry.khronos.org/OpenXR/specs/1.1/man/html/xrEnumerateInstanceExtensionProperties.html).

## Your one task — no code changes yet

Explain this in your own words:

> Why does the third argument use `&extensionCount` rather than `extensionCount`?

One or two sentences is enough. We'll review that before moving on.

## Lesson 1 completion summary

**Completed on 2026-10-18:** the standalone [probe](../src/OpenXR/probe.cpp)
compiled, linked, and printed the global OpenXR extension names and versions
in the learner's terminal launch environment. The sections above preserve
the original starting lesson; this summary records the subsequent work.

### Concepts learned

- `XrResult` reports the operation's status, not the extension count.
  `XR_FAILED(result)` tests whether the result is negative. Zero is
  `XR_SUCCESS`; positive values are non-failure statuses whose meaning depends
  on the function.
- Passing `&extensionCount` gives OpenXR the address at which it can write
  the count. It does not transfer ownership of the variable.
- `std::vector<XrExtensionProperties> extensions(extensionCount)` creates
  that many structures in contiguous, caller-owned storage. It does not
  retrieve extension details.
- A range-based loop with `XrExtensionProperties&` modifies the actual
  vector elements. Without `&`, it would modify local copies.
- Every output record needs `type = XR_TYPE_EXTENSION_PROPERTIES` and
  `next = nullptr`. The repeated type identifies the structure format;
  different records describe different extensions.
- `next` does not link vector elements. For this structure it must be null;
  OpenXR accesses the array using its pointer and capacity.
- `extensions.data()` provides a pointer to the first element;
  the capacity argument separately specifies how many elements may be written.
- A `const XrExtensionProperties&` in the printing loop avoids copying and
  prevents modification through that reference.
- The vector releases its storage automatically when it goes out of scope.
  No OpenXR instance, session, or other handle was created or needs destroying.

### The completed two-call flow

1. Query the required count using zero capacity and a null properties pointer.
2. Check the result. Report failure and return a nonzero exit status if needed.
3. If the successful query reports zero extensions, print a normal message
   and return success. Zero extensions is not itself an API failure.
4. Allocate the vector and initialize every record's `type` and `next`.
5. Query again with the allocated capacity, `&returnedCount`, and
   `extensions.data()`.
6. Check the second result before using the records.
7. Resize the vector to `returnedCount`, then print the name and version of
   each record using a read-only range-based loop.

The zero-count early exit prevents treating a later zero-capacity count query
as though it had filled records. If the required count grows beyond a nonzero
capacity between calls, `XR_ERROR_SIZE_INSUFFICIENT` is handled by the current
error path. Automatic retry is not implemented.

### Build and observed output

The learner successfully built and ran the probe with:

```sh
g++ -std=c++17 -Wall -Wextra -Wpedantic \
  src/OpenXR/probe.cpp \
  /opt/steamvr/bin/linuxarm64/libopenxr_loader.so \
  -Wl,-rpath,/opt/steamvr/bin/linuxarm64 \
  -o src/OpenXR/openblack-xr-probe

./src/OpenXR/openblack-xr-probe
```

`-o` selects the executable to create; run that same executable rather than an
older binary. The explicit shared-loader path is a device-specific diagnostic
workaround for the earlier default-loader link failure, not permanent engine
dependency configuration. Linking a loader does not select the runtime.

The reported output included:

| Extension | Reported revision | Relevance |
| --- | --- | --- |
| `XR_KHR_vulkan_enable2` | 4 | Candidate OpenXR/Vulkan integration path |
| `XR_EXT_debug_utils` | 5 | Diagnostic messaging and object labeling |
| `XR_VALVE_frame_controller_interaction` | 1 | Frame-controller interaction profile |

An extension's revision is not the Vulkan version or overall OpenXR API
version. Enumeration advertises availability; it does not enable an extension
or verify its features in use.

### What this verifies, and what remains

This is new evidence from the current probe: compilation, explicit loader
linking, and global extension enumeration worked in the tested terminal
environment.

It does **not** verify runtime identity, instance creation, headset session
lifecycle, tracking, controller input, rendered images, Vulkan/bgfx resource
interop, or the final SteamVR application launch route. OpenBlack's desktop
rendering path was not changed by this exercise.

### Official references

- [`xrEnumerateInstanceExtensionProperties`](https://registry.khronos.org/OpenXR/specs/1.1/man/html/xrEnumerateInstanceExtensionProperties.html)
- [`XrExtensionProperties`](https://registry.khronos.org/OpenXR/specs/1.1/man/html/XrExtensionProperties.html)
- [`XrResult`](https://registry.khronos.org/OpenXR/specs/1.1/man/html/XrResult.html)
- [C++ standard draft: `std::vector`](https://eel.is/c++draft/vector)

### Next concept, not yet implemented

Following the tutorial's instance-creation section, learn how an `XrInstance`
establishes application information, the requested OpenXR API version, enabled
extensions, and enabled API layers. A successfully created instance requires
`xrDestroyInstance`; the handle variable alone does not clean it up.

Before writing that code, explain the difference between an extension appearing
in the printed list and the application enabling it during instance creation.
