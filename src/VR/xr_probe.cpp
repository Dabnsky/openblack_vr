#define XR_USE_GRAPHICS_API_VULKAN // tells the platform header to expose Vulkan-related declarations.
#include <vulkan/vulkan.h>
#include <openxr/openxr.h>
#include <openxr/openxr_platform.h> // provides graphics/platform integration types and functions

#include <cstdio>
#include <cstring>
#include <cstdlib> // lets us use EXIT_FAILURE and EXIT_SUCCESS macros.

#include <vector>
using namespace std;

int main() {

    uint32_t extensionCount = 0;
    XrResult extensionResult = xrEnumerateInstanceExtensionProperties(nullptr, 0, &extensionCount, nullptr);
    if (XR_FAILED(extensionResult)) {
        fprintf(stderr, "Failed to enumerate instance extension properties: %d\n", static_cast<int>(extensionResult));
        return EXIT_FAILURE;
    }

    vector<XrExtensionProperties> extensions(extensionCount);
    for (auto & ext : extensions) {
        ext.type = XR_TYPE_EXTENSION_PROPERTIES;
    }

    extensionResult = xrEnumerateInstanceExtensionProperties(nullptr, static_cast<uint32_t>(extensions.size()), &extensionCount, extensions.empty() ? nullptr : extensions.data());
    
    if (XR_FAILED(extensionResult)) {
        fprintf(stderr, "Failed to enumerate instance extension properties: %d\n", static_cast<int>(extensionResult));
        return EXIT_FAILURE;
    }

    bool hasVulkanEnable2 = false;
    for (uint32_t i = 0; i < extensionCount; ++i) 
    {
        const auto& extension = extensions[i];
        printf("Extension: %s (revision %u)\n", extension.extensionName, extension.extensionVersion);
        if (strcmp(extension.extensionName, "XR_KHR_vulkan_enable2") == 0) {
            hasVulkanEnable2 = true;
        }
    }

    printf("XR_KHR_vulkan_enable2 advertised: %s\n",
            hasVulkanEnable2 ? "yes" : "no");


    if (!hasVulkanEnable2)
    {
        std::fprintf(stderr,
                    "This Vulkan probe requires XR_KHR_vulkan_enable2.\n");
        return EXIT_FAILURE;
    }

    const char* enabledExtensions[] = {
        XR_KHR_VULKAN_ENABLE2_EXTENSION_NAME
    };

    XrInstanceCreateInfo createInfo{};     // by XrInstanceCreateInfo being initialized to 0, those values mean we aren’t requesting layers or extensions in this first probe.
    createInfo.type = XR_TYPE_INSTANCE_CREATE_INFO; // set the struct to be identified as as a create info instance.
    
    // Identify the application and requested API version
    strcpy(createInfo.applicationInfo.applicationName, "OpenBlack XR Probe");
    createInfo.applicationInfo.applicationVersion = 1; // set this application version.
    createInfo.applicationInfo.apiVersion = XR_MAKE_VERSION(1, 0, 0); // set the OpenXR API version to 1.0.0.

    createInfo.enabledExtensionCount = 1;
    createInfo.enabledExtensionNames = enabledExtensions; //requests permission to use an extension on the new instance

    XrInstance instance = XR_NULL_HANDLE;
    XrResult result = xrCreateInstance(&createInfo, &instance);

    if (XR_FAILED(result)) {
        fprintf(stderr, "Failed to create OpenXR instance: %d\n", static_cast<int>(result));
        return EXIT_FAILURE;
    }

    /*
    Define a reusable result checker lambda function. 
    auto check lets C++ infer the lambda's type. [instance] captures the instance handled by value
    If the conversion works, print both symbolic and numeric information.
    If it fails, still report the original error—and the conversion failure. We don’t hide one failure behind another.
    */
    auto check = [instance](XrResult value, const char * operation)
    {
        if (XR_SUCCEEDED(value)) {
            return true;
        }

        char text[XR_MAX_RESULT_STRING_SIZE]{};
        XrResult conversion = xrResultToString(instance, value, text); // have OpenXR describe the result

        if (XR_SUCCEEDED(conversion))
        {
            fprintf(stderr, "%s failed: %s (%d)\n", operation, text, static_cast<int>(value));
        } 
        else 
        {
            fprintf(stderr, "%s failed: %d; xrResultToString failed %d\n", operation, static_cast<int>(value), static_cast<int>(conversion));
        }
        return false;
    };

    bool success = true;

    //Query the runtime’s identity
    XrInstanceProperties properties{};
    properties.type = XR_TYPE_INSTANCE_PROPERTIES;
    success = check(xrGetInstanceProperties(instance, &properties), "xrGetInstanceProperties");

    if (success) 
    {
        printf("Runtime: %s\nVersion: %u.%u.%u\n",
            properties.runtimeName,
            XR_VERSION_MAJOR(properties.runtimeVersion),
            XR_VERSION_MINOR(properties.runtimeVersion),
            XR_VERSION_PATCH(properties.runtimeVersion));
    }

    //Build a request for a head-mounted-display system
    XrSystemGetInfo systemInfo{};
    systemInfo.type = XR_TYPE_SYSTEM_GET_INFO;
    systemInfo.formFactor = XR_FORM_FACTOR_HEAD_MOUNTED_DISPLAY;
    XrSystemId system = XR_NULL_SYSTEM_ID;

    success = success && check(xrGetSystem(instance, &systemInfo, &system), "xrGetSystem");;

    if (success)
    {
        puts("HMD system discovered.");
    }


    if (success)
    {
        PFN_xrGetVulkanGraphicsRequirements2KHR getVulkanRequirements = nullptr; //A function-pointer type with the exact signature required by this extension command.

        success = check(
            xrGetInstanceProcAddr( //	Obtains the function address associated with this instance. Extension commands are normally accessed this way.
                instance,
                "xrGetVulkanGraphicsRequirements2KHR",
                reinterpret_cast<PFN_xrVoidFunction*>(&getVulkanRequirements)),
            "xrGetInstanceProcAddr");

        if (success && getVulkanRequirements == nullptr)
        {
            std::fprintf(stderr,
                        "Vulkan requirements function pointer is null.\n");
            success = false;
        }

        if (success)
        {
            XrGraphicsRequirementsVulkan2KHR requirements{};
            requirements.type = XR_TYPE_GRAPHICS_REQUIREMENTS_VULKAN2_KHR;

            success = check(
                getVulkanRequirements(instance, system, &requirements),
                "xrGetVulkanGraphicsRequirements2KHR");

            if (success)
            {
                const auto printVersion = [](const char* label, XrVersion version)
                {
                    std::printf("%s: %u.%u.%u\n",
                                label,
                                XR_VERSION_MAJOR(version),
                                XR_VERSION_MINOR(version),
                                XR_VERSION_PATCH(version));
                };

                printVersion("Minimum Vulkan API",
                            requirements.minApiVersionSupported);
                printVersion("Maximum Vulkan API",
                            requirements.maxApiVersionSupported);
            }
        }
    }

    result = xrDestroyInstance(instance);
    if (XR_FAILED(result)) {
        fprintf(stderr, "Failed to destroy OpenXR instance: %d\n", static_cast<int>(result));
        success = false;
    }
    return success ? EXIT_SUCCESS : EXIT_FAILURE;
}