#include <openxr/openxr.h>

#include <cstdint>
#include <iostream>
#include <vector>

int main()
{
	// The first enumeration call asks the loader for the number of global
	// extensions. It does not need an OpenXR instance or a headset session.
	uint32_t extensionCount = 0;
	const XrResult countResult =
	    xrEnumerateInstanceExtensionProperties(nullptr, 0, &extensionCount, nullptr);
	if (XR_FAILED(countResult))
	{
		std::cerr << "Could not count OpenXR instance extensions (XrResult "
		          << countResult << ")\n";
		return 1;
	}

	if (extensionCount == 0)
	{
		std::cout << "No OpenXR instance extensions found.\n";
		return 0;
	}
	// OpenXR writes into this caller-owned vector. Each output structure must identify its type before it is passed to the API.
	// Neither assignment retrieves extension details. They prepare the output structures for the second call.
	std::vector<XrExtensionProperties> extensions(extensionCount); 
	for (XrExtensionProperties& extension : extensions) // each element describes a different available extension
	{
		/* 
			C++ already knows the variable’s type, but OpenXR’s C API requires this explicit identifier in many structures. 
			Allocating a structure doesn’t automatically set its required OpenXR type identifier.
			XR_TYPE_EXTENSION_PROPERTIES is a named constant from the XrStructureType enumeration. 
			It identifies the structure—not the extension whose name will eventually be returned.
		 */
		extension.type = XR_TYPE_EXTENSION_PROPERTIES; // Tells OpenXR “This structure is an XrExtensionProperties.”
		extension.next = nullptr; // In OpenXR, next is used to attach additional structures to one API structure, where permitted. For XrExtensionProperties, it must be nullptr; we are not leaving it for OpenXR to fill later.
	}

	uint32_t returnedCount = 0;

	// TODO 1: Make the second xrEnumerateInstanceExtensionProperties call.
	// Pass nullptr for layerName, the vector's size for propertyCapacityInput,
	// &returnedCount for propertyCountOutput, and extensions.data() for
	// properties. Check its XrResult just as above.
	XrResult propInfoResults = xrEnumerateInstanceExtensionProperties(nullptr, extensionCount, &returnedCount, extensions.data());
	if (XR_FAILED(propInfoResults))
	{
		std::cerr << "Could not retrieve OpenXR instance extension properties "
		          << propInfoResults << ")\n";
		return 1;
	}


	// TODO 2: Print each returned extension's extensionName and
	// extensionVersion. Use returnedCount as the loop limit.
	extensions.resize(returnedCount);
	for (const XrExtensionProperties& extension : extensions)
	{
		std::cout << "Extension Name: " << extension.extensionName
		          << ", Extension Version: " << extension.extensionVersion
		          << "\n";
	}
}