#pragma once

#include <vk_types.h>
#include <vk_utils.h>

constexpr unsigned int FRAME_OVERLAP = 2;

class VulkanEngine {
public:

	static VulkanEngine& get_instance();  // Get the singleton instance of the engine.
	
	void init();  // Initializes everything in the engine.	
	void cleanup();  // Shuts down the engine.	
	void draw();  // Draw loop.	
	void run();  // Run main loop.	


	// PUBLIC METHOD DECLARATIONS:

	// Vulkan Commands related Helper Functions:
	inline FrameData& get_current_frame() { return m_frameData[m_frameNumber % FRAME_OVERLAP]; }

private:
	// PRIVATE MEMBER DECLARATIONS:

	// SDL Window Parameters:
	bool m_isInitialized{ false };
	bool m_stopRendering{ false };
	VkExtent2D m_windowExtent{ 800 , 600 };  // in pixels
	struct SDL_Window* m_window{ nullptr };  // forward declared
	
	// Vulkan Extension parameters:
	std::vector<const char*> m_vulkanExtensionNames = {
		"VK_EXT_debug_utils"
	};

	// SDL-Vulkan Extension parameters:
	std::vector<const char*> m_sdlVulkanExtensionNames;
	uint32_t m_sdlVulkanExtensionsCount{};
	std::vector<const char*> m_requiredPhysicalDeviceExtensions = {
		VK_KHR_SWAPCHAIN_EXTENSION_NAME
	};

	// Vulkan Components:
	VkInstance m_vulkanInstance = VK_NULL_HANDLE;  // Vulkan Instance handle
	VkSurfaceKHR m_vulkanSurface = VK_NULL_HANDLE;  // Vulkan window surface
	VkPhysicalDevice m_vulkanPhysicalDevice = VK_NULL_HANDLE;  // Vulkan Physical Device (GPU)
	VkDevice m_vulkanLogicalDevice = VK_NULL_HANDLE;  // Vulkan Logical Device (GPU Driver)
	
	// Device related data:
	const bool m_useDedicatedTransferQueueFamily{ false };
	QueueFamilyIndices m_queueFamilyIndices;
	SwapChainSupportDetails m_swapChainSupportDetails;
	VkQueue m_graphicsQueue = VK_NULL_HANDLE;
	VkQueue m_presentationQueue = VK_NULL_HANDLE;
	VkQueue m_transferQueue = VK_NULL_HANDLE;

	// Swapchain related data:
	VkSwapchainKHR m_vulkanSwapchainKHR = VK_NULL_HANDLE;
	VkFormat m_swapchainSurfaceFormat;
	VkColorSpaceKHR m_swapchainSurfaceColorspace;
	VkExtent2D m_swapchainExtent2D;
	std::vector<VkImage> m_swapchainImages;
	std::vector<VkImageView> m_swapchainImageViews;

	// Vulkan Commands related parameters:
	int m_frameNumber{ 0 };
	std::array<FrameData, FRAME_OVERLAP> m_frameData;

	// Vulkan Validation Layers:
	VkDebugUtilsMessengerEXT m_vulkanDebugMessenger = VK_NULL_HANDLE;  // Vulkan Debug Messenger
#ifndef NDEBUG
	const bool m_useValidationLayers{ true };
#else 
	const bool m_useValidationLayers{ false };
#endif
	const std::vector<const char*> m_vulkanValidationLayers = {
		"VK_LAYER_KHRONOS_validation"
	};

	// PRIVATE METHOD DECLARATIONS:

	// Vulkan Initialization Functions:
	void init_vulkan();
	void init_swapchain();
	void init_commands();
	void init_sync_structures();

	// Vulkan Initialization Helper Functions:
	void create_vulkan_instance();
	void setup_vulkan_debug_messenger();
	void create_sdl_vulkan_surface();
	void select_vulkan_physical_device();
	void create_vulkan_logical_device();
	void create_vulkan_swapchain();
	
	// Vulkan Extensions Helper Functions:
	void list_vulkan_instance_extensions();

	// Vulkan Device related Helper Functions:
	QueueFamilyIndices find_required_queue_families(VkPhysicalDevice physicalDevice, bool findDedicatedTransferFamily = false);
	bool check_physical_device_supports_required_extensions(VkPhysicalDevice physicalDevice, std::vector<const char*> requiredDeviceExtensions);

	// Vulkan Swapchain related Helper Functions:
	SwapChainSupportDetails query_swapchain_support(VkPhysicalDevice physicalDevice, VkSurfaceKHR surface);
	VkSurfaceFormatKHR choose_swapchain_surface_format(const std::vector<VkSurfaceFormatKHR>& surfaceFormats);
	VkPresentModeKHR choose_swapchain_present_mode(const std::vector<VkPresentModeKHR>& presentModes, VkPresentModeKHR desiredPresentMode);
	VkExtent2D choose_swapchain_extent_2D(const VkSurfaceCapabilitiesKHR& surfaceCapabilities);

	// General Vulkan Helper Functions:
	VkImageView create_image_view(VkImage image, VkFormat format, VkImageAspectFlags aspectFlags);

	// Vulkan Validation Layers Helper Functions and Proxies:
	bool check_vulkan_validation_layers_support(std::vector<const char*> validationLayers);
	static VKAPI_ATTR VkBool32 VKAPI_CALL debug_callback(VkDebugUtilsMessageSeverityFlagBitsEXT messageSeverity,
		VkDebugUtilsMessageTypeFlagsEXT messageType,
		const VkDebugUtilsMessengerCallbackDataEXT* pCallbackData,
		void* pUserData);
	void populate_debug_messenger_create_info(VkDebugUtilsMessengerCreateInfoEXT& createInfo);
	inline VkResult create_debug_utils_messenger_ext(
		VkInstance instance,
		const VkDebugUtilsMessengerCreateInfoEXT* pCreateInfo,
		const VkAllocationCallbacks* pAllocator,
		VkDebugUtilsMessengerEXT* pDebugMessenger) {
		auto func = (PFN_vkCreateDebugUtilsMessengerEXT)
			vkGetInstanceProcAddr(instance, "vkCreateDebugUtilsMessengerEXT");

		if (func != nullptr) {
			return func(instance, pCreateInfo, pAllocator, pDebugMessenger);
		}
		else {
			return VK_ERROR_EXTENSION_NOT_PRESENT;
		}
	}
	inline void destroy_debug_utils_messenger_ext(VkInstance instance, VkDebugUtilsMessengerEXT debugMessenger, const VkAllocationCallbacks* pAllocator) {
		auto func = (PFN_vkDestroyDebugUtilsMessengerEXT)vkGetInstanceProcAddr(instance, "vkDestroyDebugUtilsMessengerEXT");
		if (func != nullptr) {
			func(instance, debugMessenger, pAllocator);
		}
	}

	// SDL-Vulkan Extension Helper Functions:
	void get_sdl_vulkan_extensions(SDL_Window* window);
	void list_sdl_vulkan_extensions();

	// Cleanup Helper Functions:
	void cleanup_swapchain();
	void cleanup_command_pools();

	// Logging Functions using fmt:
	void log_error(const std::string& msg);
	void log_warning(const std::string& msg);
	void log_info(const std::string& msg);
	void log_debug(const std::string& msg);
	void log_success(const std::string& msg);

};
