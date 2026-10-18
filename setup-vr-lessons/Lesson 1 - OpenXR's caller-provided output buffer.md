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
