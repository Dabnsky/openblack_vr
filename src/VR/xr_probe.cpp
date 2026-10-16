// Graphics-specific OpenXR declarations reference Vulkan types, so include Vulkan
// before openxr_platform.h. This exposes declarations; it creates no GPU objects.
#define XR_USE_GRAPHICS_API_VULKAN
#include <vulkan/vulkan.h>
#include <openxr/openxr.h>
#include <openxr/openxr_platform.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>


//let the probe observe events for a bounded period without busy-spinning.
#include <chrono>
#include <thread>

namespace
{
// Before instance creation and after destruction, only numeric diagnostics are
// safe. With a live instance, also ask the runtime for the symbolic result name.
// XR_SUCCEEDED accepts non-error status codes as well as XR_SUCCESS.
bool CheckXr(XrInstance instance, XrResult result, const char* operation)
{
    if (XR_SUCCEEDED(result))
    {
        return true;
    }
    if (instance == XR_NULL_HANDLE)
    {
        std::fprintf(stderr, "%s failed: %d\n", operation, static_cast<int>(result));
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
                     operation, static_cast<int>(result), static_cast<int>(conversion));
    }
    return false;
}

// Extension commands are obtained from the live, extension-enabled instance.
// Keep the generic-to-typed function-pointer conversion here rather than
// repeating it at every call site. Loading a function does not invoke it.
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

// These fields contain OpenXR-packed versions, even though they describe Vulkan
// requirements. GPU properties below instead use Vulkan's version macros.
void PrintXrVersion(const char* label, XrVersion version)
{
    std::printf("%s: %u.%u.%u\n", label,
                XR_VERSION_MAJOR(version),
                XR_VERSION_MINOR(version),
                XR_VERSION_PATCH(version));
}

// Enumeration precedes instance creation because extensions must be selected
// at creation time. First query the count, then provide initialized output
// structures. Retry if the available list grows between those two calls.
bool CheckRequiredExtension()
{
    std::vector<XrExtensionProperties> extensions;
    uint32_t count = 0;
    XrResult result;
    do
    {
        if (!CheckXr(XR_NULL_HANDLE,
                     xrEnumerateInstanceExtensionProperties(nullptr, 0, &count, nullptr),
                     "xrEnumerateInstanceExtensionProperties(count)"))
        {
            return false;
        }

        // A zero-capacity call only queries the count; no entries can be read.
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
            nullptr, static_cast<uint32_t>(extensions.size()), &count, extensions.data());
        if (result != XR_ERROR_SIZE_INSUFFICIENT &&
            !CheckXr(XR_NULL_HANDLE, result, "xrEnumerateInstanceExtensionProperties(list)"))
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
        if (std::strcmp(extension.extensionName, XR_KHR_VULKAN_ENABLE2_EXTENSION_NAME) == 0)
        {
            available = true;
        }
    }
    std::printf("XR_KHR_vulkan_enable2 advertised: %s\n", available ? "yes" : "no");
    if (!available)
    {
        std::fprintf(stderr, "This Vulkan probe requires XR_KHR_vulkan_enable2.\n");
    }
    return available;
}

// Find a family that can execute graphics commands on the runtime-selected GPU.
// This only inspects capabilities; queues are requested during device creation.
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
                    i, family.queueCount,
                    supportsGraphics ? "yes" : "no");

        if (family.queueCount > 0 && supportsGraphics)
        {
            graphicsQueueFamily = i;
            return true;
        }
    }

    std::fprintf(stderr, "No graphics-capable queue family was found.\n");
    return false;
}

// Create a logical device on the GPU selected by OpenXR, requesting one
// graphics queue. The caller owns the device; the queue belongs to that device.
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

    // Vulkan requires a priority for each requested queue. This is a relative
    // scheduling priority, not permission to preempt the runtime's compositor.
    const float queuePriority = 1.0f;

    VkDeviceQueueCreateInfo queueInfo{};
    queueInfo.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
    queueInfo.queueFamilyIndex = graphicsQueueFamily;
    queueInfo.queueCount = 1;
    queueInfo.pQueuePriorities = &queuePriority;

    // No optional application features or device extensions are requested yet.
    // OpenXR incorporates the requirements needed by its Vulkan integration.
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

    // As with instance creation, check both the OpenXR result and the
    // underlying Vulkan result before using the output device.
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

    // Queue index 0 means the first queue within our selected family.
    // vkGetDeviceQueue returns void; the queue needs no separate destruction.
    vkGetDeviceQueue(device, graphicsQueueFamily, 0, &queue);

    if (queue == VK_NULL_HANDLE)
    {
        std::fprintf(stderr, "The graphics queue handle is null.\n");
        return false;
    }

    std::puts("Graphics queue retrieved.");
    return true;
}


// Describe the Vulkan resources the runtime will share with our application.
// The binding is only needed during creation, but the referenced Vulkan
// objects must remain alive until the OpenXR session has been destroyed.
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

    // The next chain attaches graphics-specific information to the otherwise
    // graphics-independent session request. OpenXR identifies it using type.
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

// Check the runtime's supported view configurations before choosing the
// configuration that will be passed to xrBeginSession.
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


// Inspect image-sizing recommendations for the supported stereo configuration.
// This does not allocate swapchains or choose the final rendering settings.
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
        std::fprintf(stderr,
                     "Expected two primary stereo views; got %u.\n", count);
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
                     static_cast<uint32_t>(views.size()),
                     &count, views.data()),
                 "xrEnumerateViewConfigurationViews(list)"))
    {
        return false;
    }

    if (count != 2)
    {
        std::fprintf(stderr,
                     "Stereo view query returned %u views, expected two.\n",
                     count);
        return false;
    }

    for (uint32_t i = 0; i < count; ++i)
    {
        const auto& view = views[i];
        std::printf(
            "View %u: recommended=%ux%u, maximum=%ux%u, samples=%u (max %u)\n",
            i,
            view.recommendedImageRectWidth,
            view.recommendedImageRectHeight,
            view.maxImageRectWidth,
            view.maxImageRectHeight,
            view.recommendedSwapchainSampleCount,
            view.maxSwapchainSampleCount);
    }

    return true;
}

// Check composition support for our chosen stereo configuration.
// This query does not begin the session or change its rendering settings.
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
                     static_cast<uint32_t>(modes.size()),
                     &count, modes.data()),
                 "xrEnumerateEnvironmentBlendModes(list)"))
    {
        return false;
    }

    bool supportsOpaque = false;
    for (uint32_t i = 0; i < count; ++i)
    {
        const bool isOpaque = modes[i] == XR_ENVIRONMENT_BLEND_MODE_OPAQUE;

        std::printf("Environment blend mode: %d%s\n",
                    static_cast<int>(modes[i]),
                    isOpaque ? " (OPAQUE)" : "");

        supportsOpaque = supportsOpaque || isOpaque;
    }

    if (!supportsOpaque)
    {
        std::fprintf(stderr,
                     "This VR probe requires OPAQUE environment blending.\n");
        return false;
    }

    std::puts("Opaque environment blend mode supported.");
    return true;
}


// All resources are owned by main. Returning early here skips dependent work,
// but main still destroys any Vulkan instance created before a later failure.
// The system ID and physical-device handle are borrowed identifiers, not
// independently owned objects that require their own destruction calls.
bool ProbeGraphics(XrInstance instance,
                   VkInstance& vulkanInstance,
                   VkDevice& device,
                   XrSession& session)
{
    // Report the actual selected runtime, rather than assuming that installing
    // SteamVR or finding its manifest means this process connected to it.
    XrInstanceProperties properties{};
    properties.type = XR_TYPE_INSTANCE_PROPERTIES;
    if (!CheckXr(instance, xrGetInstanceProperties(instance, &properties),
                 "xrGetInstanceProperties"))
    {
        return false;
    }
    std::printf("Runtime: %s\n", properties.runtimeName);
    PrintXrVersion("Version", properties.runtimeVersion);

    // An instance can exist even when an HMD is unavailable. System discovery
    // is a separate checkpoint and supplies the ID needed for graphics queries.
    XrSystemGetInfo systemInfo{};
    systemInfo.type = XR_TYPE_SYSTEM_GET_INFO;
    systemInfo.formFactor = XR_FORM_FACTOR_HEAD_MOUNTED_DISPLAY;
    XrSystemId system = XR_NULL_SYSTEM_ID;
    if (!CheckXr(instance, xrGetSystem(instance, &systemInfo, &system), "xrGetSystem"))
    {
        return false;
    }
    std::puts("HMD system discovered.");

    if (!CheckStereoViewConfiguration(instance, system))
    {
        return false;
    }

    if (!InspectStereoViews(instance, system))
    {
        return false;
    }

    if (!CheckOpaqueBlendMode(instance, system))
    {   
        return false;
    }

    // Query runtime graphics requirements before creating Vulkan resources.
    // Only inspect the returned range after the query succeeds.
    const auto getRequirements = LoadXrFunction<PFN_xrGetVulkanGraphicsRequirements2KHR>(
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

    // This probe deliberately requires Vulkan 1.2, with no silent fallback.
    // OpenXR and Vulkan encode versions differently: compare XrVersion values
    // here, but pass VK_API_VERSION_1_2 to Vulkan's application description.
    const XrVersion requestedVersion = XR_MAKE_VERSION(1, 2, 0);
    if (requestedVersion < requirements.minApiVersionSupported ||
        requestedVersion > requirements.maxApiVersionSupported)
    {
        std::fprintf(stderr, "This probe requires Vulkan 1.2 within the runtime's range.\n");
        return false;
    }

    const auto createVulkanInstance = LoadXrFunction<PFN_xrCreateVulkanInstanceKHR>(
        instance, "xrCreateVulkanInstanceKHR");
    if (createVulkanInstance == nullptr)
    {
        return false;
    }

    // Describe our Vulkan request, then let OpenXR incorporate its requirements.
    // No SDL window or desktop surface is needed for this discovery exercise.
    // Zero-initialization leaves optional next pointers, flags, and lists empty.
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

    // This command has two result channels: OpenXR dispatch and underlying
    // Vulkan creation. Both must succeed before the instance can be used.
    VkResult vulkanResult = VK_SUCCESS;
    if (!CheckXr(instance,
                 createVulkanInstance(instance, &xrVulkanInfo, &vulkanInstance, &vulkanResult),
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

    // Ask the runtime which GPU is compatible with this HMD system instead of
    // picking the first Vulkan GPU. The returned handle belongs to vulkanInstance.
    const auto getGraphicsDevice = LoadXrFunction<PFN_xrGetVulkanGraphicsDevice2KHR>(
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
    if (!CheckXr(instance, getGraphicsDevice(instance, &deviceInfo, &physicalDevice),
                 "xrGetVulkanGraphicsDevice2KHR"))
    {
        return false;
    }
    if (physicalDevice == VK_NULL_HANDLE)
    {
        std::fprintf(stderr, "OpenXR returned a null physical device.\n");
        return false;
    }

    // This Vulkan query returns void. Its API version describes GPU capability,
    // not the narrower range supported by the runtime's graphics integration.
    VkPhysicalDeviceProperties deviceProperties{};
    vkGetPhysicalDeviceProperties(physicalDevice, &deviceProperties);
    std::printf("OpenXR-selected GPU: %s\n", deviceProperties.deviceName);
    std::printf("GPU Vulkan API: %u.%u.%u\n",
                VK_VERSION_MAJOR(deviceProperties.apiVersion),
                VK_VERSION_MINOR(deviceProperties.apiVersion),
                VK_VERSION_PATCH(deviceProperties.apiVersion));

    uint32_t graphicsQueueFamily = 0;
    if (!FindGraphicsQueueFamily(physicalDevice, graphicsQueueFamily))
    {
        return false;
    }

    std::printf("Selected graphics queue family: %u\n", graphicsQueueFamily);
    VkQueue graphicsQueue = VK_NULL_HANDLE;
    if (!CreateGraphicsDevice(
        instance, system, physicalDevice, graphicsQueueFamily,
        device, graphicsQueue))
    {
        return false;
    }
    return CreateGraphicsSession(
        instance, system, vulkanInstance, physicalDevice, 
        device, graphicsQueueFamily, session);
}
} // namespace


// Observe lifecycle events without beginning the session. A bounded observation
// period keeps this diagnostic from waiting forever if READY never arrives.
bool ObserveSessionEvents(XrInstance instance, XrSession session)
{
    using Clock = std::chrono::steady_clock;
    const auto deadline = Clock::now() + std::chrono::seconds(5);
    bool sawReady = false;

    while (Clock::now() < deadline)
    {
        // xrPollEvent overwrites type with the returned event's type.
        // Initialize a fresh buffer before every poll.
        XrEventDataBuffer event{};
        event.type = XR_TYPE_EVENT_DATA_BUFFER;

        const XrResult result = xrPollEvent(instance, &event);

        // This is a successful status, not an error. No event was written,
        // so don't interpret the buffer. Briefly sleep before trying again.
        if (result == XR_EVENT_UNAVAILABLE)
        {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
            continue;
        }

        if (!CheckXr(instance, result, "xrPollEvent"))
        {
            return false;
        }

        // Inspect the type before interpreting the buffer as a specific event.
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
            // Other event types are outside this lesson, but remain visible
            // in diagnostics rather than being silently discarded.
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


int main()
{
    if (!CheckRequiredExtension())
    {
        return EXIT_FAILURE;
    }

    // Advertising an extension does not enable it. Explicitly request the
    // Vulkan bridge while creating the OpenXR instance, using core API 1.0.
    // The fixed application-name literal fits OpenXR's destination array.
    const char* enabledExtensions[] = {XR_KHR_VULKAN_ENABLE2_EXTENSION_NAME};
    XrInstanceCreateInfo createInfo{};
    createInfo.type = XR_TYPE_INSTANCE_CREATE_INFO;
    std::strcpy(createInfo.applicationInfo.applicationName, "OpenBlack XR Probe");
    createInfo.applicationInfo.applicationVersion = 1;
    createInfo.applicationInfo.apiVersion = XR_MAKE_VERSION(1, 0, 0);
    createInfo.enabledExtensionCount = 1;
    createInfo.enabledExtensionNames = enabledExtensions;

    XrInstance instance = XR_NULL_HANDLE;
    if (!CheckXr(XR_NULL_HANDLE, xrCreateInstance(&createInfo, &instance), "xrCreateInstance"))
    {
        return EXIT_FAILURE;
    }

    // Keep ownership in one place. ProbeGraphics may fail at any stage; neither
    // success nor failure bypasses cleanup once the OpenXR instance exists.
    VkInstance vulkanInstance = VK_NULL_HANDLE;
    VkDevice device = VK_NULL_HANDLE;
    XrSession session = XR_NULL_HANDLE;
    bool success = ProbeGraphics(instance, vulkanInstance, device, session);

    if (success)
    {
        success = ObserveSessionEvents(instance, session);
    }

    // The session references our Vulkan resources, so release it first.
    // Check cleanup even when an earlier probe step failed.
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

    // No GPU work has been submitted in this exercise, so nothing needs waiting
    // for. Later, submitted work must finish before its resources are destroyed.
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

    // Destroy graphics resources before the OpenXR instance. Do not use the
    // destroyed instance for error-string conversion, and preserve prior errors
    // while still checking cleanup even if ProbeGraphics already failed.
    const XrResult destroyResult = xrDestroyInstance(instance);
    const bool destroyed = CheckXr(XR_NULL_HANDLE, destroyResult, "xrDestroyInstance");
    success = success && destroyed;
    return success ? EXIT_SUCCESS : EXIT_FAILURE;
}
