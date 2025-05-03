
#pragma once

#include <vk_types.h>

/// @brief Custom struct that holds the queue indices for various device queue families.
struct QueueFamilyIndices {
	/// @brief The index of the Graphics queue family (if any) of the GPU.
	std::optional<uint32_t> graphicsFamily;
	/// @brief The index of the Presentation queue family (if any) of the GPU.
	std::optional<uint32_t> presentationFamily;
	/// @brief The index of the Transfer queue family (if any) of the GPU.
	std::optional<uint32_t> transferFamily;

	bool isComplete() {
		return (
			graphicsFamily.has_value() && 
			presentationFamily.has_value() && 
			transferFamily.has_value()
		);
	}
};

/// @brief Custom struct that holds the data relevant to the Swapchain support of the Physical Device (GPU).
struct SwapChainSupportDetails {
	/// @brief The capabilities of the surface supported by our GPU (eg: min/max images in Swapchain).
	VkSurfaceCapabilitiesKHR surfaceCapabilities;
	/// @brief List of pixel formats and color spaces supported by our GPU (eg: SRGB color space).
	std::vector<VkSurfaceFormatKHR> surfaceFormats;
	/// @brief List of presentation modes supported for the Swapchain (eg: FIFO, Mailbox etc.).
	std::vector<VkPresentModeKHR> presentationModes;
};