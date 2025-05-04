
#include "vk_engine.h"

#include <SDL.h>
#include <SDL_vulkan.h>
#include <vk_initializers.h>
#include <vk_types.h>
#include <chrono>
#include <thread>
#include <map>
#include <fmt/color.h>
#include <fmt/chrono.h>
#include <set>
#include <VkBootstrap.h>


VulkanEngine* loadedEngine = nullptr;

VulkanEngine& VulkanEngine::get_instance() { return *loadedEngine; }


// Engine Initialization Functions ----------------------------------------------------------------

void VulkanEngine::init() {
    // Only one engine initialization is allowed with the application.
    // TODO: Use thread safe std::call_once to create a singleton instance
    assert(loadedEngine == nullptr);
    loadedEngine = this;

    // Initialize SDL and create a window:
    SDL_Init(SDL_INIT_VIDEO);
    SDL_WindowFlags window_flags = (SDL_WindowFlags)(SDL_WINDOW_VULKAN);
    m_window = SDL_CreateWindow(
        "Vulkan Game Engine",
        SDL_WINDOWPOS_UNDEFINED,
        SDL_WINDOWPOS_UNDEFINED,
        m_windowExtent.width,
        m_windowExtent.height,
        window_flags
    );
    if (m_window == nullptr) {
        log_error("RUNTIME ERROR: Failed to create SDL window!");
        throw std::runtime_error("RUNTIME ERROR: Failed to create SDL window!");
    }
    log_success("SDL window created.");

    // Get the SDL Vulkan Extensions
    get_sdl_vulkan_extensions(m_window);

    // Vulkan initialization:
    init_vulkan();
    init_swapchain();
    init_commands();
    init_sync_structures();

    // All initialization steps of the engine went fine
    m_isInitialized = true;
}

void VulkanEngine::cleanup() {
    if (m_isInitialized) {
        // Wait for the GPU to finish executing its ongoing tasks
        vkDeviceWaitIdle(m_vulkanLogicalDevice);

        cleanup_swapchain();
        cleanup_command_pools();
        vkDestroyDevice(m_vulkanLogicalDevice, nullptr);
        vkDestroySurfaceKHR(m_vulkanInstance, m_vulkanSurface, nullptr);
        if (m_useValidationLayers) {
            destroy_debug_utils_messenger_ext(m_vulkanInstance, m_vulkanDebugMessenger, nullptr);
        }
        vkDestroyInstance(m_vulkanInstance, nullptr);
        SDL_DestroyWindow(m_window);
    }
    // Clear engine pointer
    loadedEngine = nullptr;
}

void VulkanEngine::draw() {
    // Nothing yet...
}

void VulkanEngine::run() {
    SDL_Event e;
    bool bQuit = false;

    // Main loop
    while (!bQuit) {
        // Handle events on queue
        while (SDL_PollEvent(&e) != 0) {
            // Close the window when user alt-f4s or clicks the X button
            if (e.type == SDL_QUIT)
                bQuit = true;

            if (e.type == SDL_WINDOWEVENT) {
                if (e.window.event == SDL_WINDOWEVENT_MINIMIZED) {
                    m_stopRendering = true;
                }
                if (e.window.event == SDL_WINDOWEVENT_RESTORED) {
                    m_stopRendering = false;
                }
            }
        }

        // Do not draw if we are minimized
        if (m_stopRendering) {
            // Throttle the speed to avoid the endless spinning
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            continue;
        }

        draw();
    }
}


// Vulkan Initialization Functions ----------------------------------------------------------------

void VulkanEngine::init_vulkan() {
    create_vulkan_instance();
    setup_vulkan_debug_messenger();
    create_sdl_vulkan_surface();
    select_vulkan_physical_device();
    create_vulkan_logical_device();
    create_vulkan_swapchain();
}

void VulkanEngine::init_swapchain() {}

void VulkanEngine::init_commands() {
    // Create a command pool for commands submitted to the Graphics Queue.
    // We also want the pool to allow for resetting of individual command buffers.
    VkCommandPoolCreateInfo graphicsCommandPoolCreateInfo = vkinit::command_pool_create_info(
        m_queueFamilyIndices.graphicsFamily.value()
    );

    for (size_t i{ 0 }; i < m_frameData.size(); i++) {
        // Create graphics command pool
        VkResult result = vkCreateCommandPool(m_vulkanLogicalDevice, &graphicsCommandPoolCreateInfo, nullptr, &m_frameData.at(i).commandPool);
        if (result != VK_SUCCESS) {
            log_error("RUNTIME ERROR: Failed to create command pool!");
            throw std::runtime_error("RUNTIME ERROR: Failed to create command pool!");
        }

        // Allocate command buffer from the pool
        VkCommandBufferAllocateInfo commandBufferAllocInfo = vkinit::command_buffer_allocate_info(m_frameData.at(i).commandPool);

        result = vkAllocateCommandBuffers(m_vulkanLogicalDevice, &commandBufferAllocInfo, &m_frameData.at(i).mainCommandBuffer);
        if (result != VK_SUCCESS) {
            log_error("RUNTIME ERROR: Failed to allocate command buffer!");
            throw std::runtime_error("RUNTIME ERROR: Failed to allocate command buffer!");
        }
    }
    log_success("Initialized graphics command pool.");
    log_success("Allocated command buffers from graphics command pool.");
}

void VulkanEngine::init_sync_structures() {}


// Vulkan Initialization Helper Functions ---------------------------------------------------------

void VulkanEngine::create_vulkan_instance() {
    // If requested validation layers, check for their support
    if (m_useValidationLayers == true && !check_vulkan_validation_layers_support(m_vulkanValidationLayers)) {
        log_error("RUNTIME ERROR: Requested validation layers aren't supported!");
        throw std::runtime_error("RUNTIME ERROR: Requested validation layers aren't supported!");
    }

    // Application Info
    VkApplicationInfo appInfo{};
    appInfo.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    appInfo.pApplicationName = "Vulkan Game Engine";
    appInfo.apiVersion = VK_API_VERSION_1_3;

    // Print the supported Vulkan Instance Extensions (debug only)
    list_vulkan_instance_extensions();

    // Get all required Vulkan Extension names (including SDL Extensions)
    std::vector<const char*> requiredVulkanExtensions = m_sdlVulkanExtensionNames;
    for (auto vulkanExtension : m_vulkanExtensionNames) {
        requiredVulkanExtensions.push_back(vulkanExtension);
    }

    // Instance Create Info
    VkDebugUtilsMessengerCreateInfoEXT debugCreateInfo{};
    VkInstanceCreateInfo instanceCreateInfo{};
    instanceCreateInfo.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    instanceCreateInfo.pApplicationInfo = &appInfo;
    instanceCreateInfo.enabledExtensionCount = static_cast<uint32_t>(requiredVulkanExtensions.size());
    instanceCreateInfo.ppEnabledExtensionNames = requiredVulkanExtensions.data();
    if (m_useValidationLayers) {
        populate_debug_messenger_create_info(debugCreateInfo);
        instanceCreateInfo.enabledLayerCount = static_cast<uint32_t>(m_vulkanValidationLayers.size());
        instanceCreateInfo.ppEnabledLayerNames = m_vulkanValidationLayers.data();
        instanceCreateInfo.pNext = (VkDebugUtilsMessengerCreateInfoEXT*)&debugCreateInfo;
    }
    else {
        instanceCreateInfo.enabledLayerCount = 0;
        instanceCreateInfo.ppEnabledLayerNames = nullptr;
        instanceCreateInfo.pNext = nullptr;
    }

    // Create the Vulkan Instance
    VkResult result = vkCreateInstance(&instanceCreateInfo, nullptr, &m_vulkanInstance);
    if (result != VK_SUCCESS) {
        log_error("RUNTIME ERROR: Failed to create Vulkan Instance!");
        throw std::runtime_error("RUNTIME ERROR: Failed to create Vulkan Instance!");
    }
    log_success("Vulkan Instance created.");
}

void VulkanEngine::setup_vulkan_debug_messenger() {
    if (!m_useValidationLayers) {
        return;
    }
    // Create the debug messenger
    VkDebugUtilsMessengerCreateInfoEXT createInfo{};
    populate_debug_messenger_create_info(createInfo);

    if (create_debug_utils_messenger_ext(m_vulkanInstance, &createInfo, nullptr, &m_vulkanDebugMessenger) != VK_SUCCESS) {
        log_error("RUNTIME ERROR: Failed to set up Vulkan debug messenger!");
        throw std::runtime_error("RUNTIME ERROR: Failed to set up Vulkan debug messenger!");
    }
}

void VulkanEngine::create_sdl_vulkan_surface() {
    if (!SDL_Vulkan_CreateSurface(m_window, m_vulkanInstance, &m_vulkanSurface)) {
        log_error("RUNTIME ERROR: Failed to create SDL Vulkan surface!");
        throw std::runtime_error("RUNTIME ERROR: Failed to create SDL Vulkan surface!");
    }
    log_success("SDL Vulkan surface created.");
}

void VulkanEngine::select_vulkan_physical_device() {
    // Vulkan 1.2 physical device features
    VkPhysicalDeviceVulkan12Features vulkan12Features{};
    vulkan12Features.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES;
    
    // Vulkan 1.3 physical device features
    VkPhysicalDeviceVulkan13Features vulkan13Features{};
    vulkan13Features.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES;
    vulkan13Features.pNext = &vulkan12Features;

    // Vulkan Physical Device Features 2 - Chaining to 1.3 and 1.2 features
    VkPhysicalDeviceFeatures2 physicalDeviceFeatures2{};
    physicalDeviceFeatures2.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2;
    physicalDeviceFeatures2.pNext = &vulkan13Features;

    // Get the available physical devices on the system
    uint32_t physicalDevicesCount{};
    std::vector<VkPhysicalDevice> availablePhysicalDevices{};
    vkEnumeratePhysicalDevices(m_vulkanInstance, &physicalDevicesCount, nullptr);
    availablePhysicalDevices.resize(physicalDevicesCount);
    vkEnumeratePhysicalDevices(m_vulkanInstance, &physicalDevicesCount, availablePhysicalDevices.data());

    // Iterate through the available physical devices, and pick the one most suitable
    std::multimap<int, VkPhysicalDevice, std::greater<>> physicalDeviceRankings{};
    QueueFamilyIndices queueFamilyIndices{};
    SwapChainSupportDetails swapchainSupportDetails{};
    for (const auto& physicalDevice : availablePhysicalDevices) {
        // [Strict] Check if it has minimum Vulkan 1.3 support
        VkPhysicalDeviceProperties physicalDeviceProperties{};
        vkGetPhysicalDeviceProperties(physicalDevice, &physicalDeviceProperties);
        if (VK_VERSION_MAJOR(physicalDeviceProperties.apiVersion) < 1 || VK_VERSION_MINOR(physicalDeviceProperties.apiVersion) < 3) {
            continue;
        }
        // [Strict] Check for required Vulkan 1.3 and 1.2 features
        vkGetPhysicalDeviceFeatures2(physicalDevice, &physicalDeviceFeatures2);
        if (!vulkan13Features.dynamicRendering || !vulkan13Features.synchronization2) {
            continue;
        }
        if (!vulkan12Features.bufferDeviceAddress || !vulkan12Features.descriptorIndexing) {
            continue;
        }
        // [Strict] Check if the physical device has the required queue families
        queueFamilyIndices = find_required_queue_families(physicalDevice, m_useDedicatedTransferQueueFamily);
        if (!queueFamilyIndices.isComplete()) {
            continue;
        }
        // [Strict] Check if all the required device extensions are supported
        bool device_extensions_supported = check_physical_device_supports_required_extensions(physicalDevice, m_requiredPhysicalDeviceExtensions);
        if (!device_extensions_supported) {
            continue;
        }
        // [Strict] Check for adequate swapchain support
        swapchainSupportDetails = query_swapchain_support(physicalDevice, m_vulkanSurface);
        if (swapchainSupportDetails.surfaceFormats.empty() || swapchainSupportDetails.presentationModes.empty()) {
            continue;
        }

        // Ranking of Physical Devices based on various criteria:
        int physicalDeviceRank{ 0 };
        if (physicalDeviceProperties.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU) {
            physicalDeviceRank += 100;
        }        
        // ADD MORE CRITERIA HERE AS NEEDED, IN THE FUTURE...

        // Insert the physical device along with its ranking to the map (automatically sorts in descending order using std::greater<>)
        physicalDeviceRankings.insert(std::make_pair(physicalDeviceRank, physicalDevice));
    }

    if (physicalDeviceRankings.empty()) {
        // Could not find any physical device matching the required criteria
        log_error("RUNTIME ERROR: Failed to find a suitable Physical Device!");
        throw std::runtime_error("RUNTIME ERROR: Failed to find a suitable Physical Device!");
    }
    
#ifndef NDEBUG
    // Print the scores for each physical device that matched the criteria
    log_debug("Physical Device selection rankings:");
    for (auto i = physicalDeviceRankings.begin(); i != physicalDeviceRankings.end(); i++) {
        int rank{ i->first };
        VkPhysicalDevice physicalDevice{ i->second };
        VkPhysicalDeviceProperties physicalDeviceProperties{};
        vkGetPhysicalDeviceProperties(physicalDevice, &physicalDeviceProperties);
        std::string msg{ "- GPU: " + std::string(physicalDeviceProperties.deviceName) + " | Score: " + std::to_string(rank) };
        log_debug(msg);
    }
#endif

    // Pick the first entry in the rankings (GPU with highest score)
    m_vulkanPhysicalDevice = physicalDeviceRankings.begin()->second;
#ifndef NDEBUG
    // Print the name of the GPU selected as the Physical Device
    VkPhysicalDeviceProperties selectedPhysicalDeviceProperties{};
    vkGetPhysicalDeviceProperties(m_vulkanPhysicalDevice, &selectedPhysicalDeviceProperties);
    std::string selectedPhysicalDeviceName{ selectedPhysicalDeviceProperties.deviceName };
    log_success("Selected Vulkan Physical Device: " + selectedPhysicalDeviceName);
#else
    log_success("Selected Vulkan Physical Device.");
#endif
    
    // Set the queue families and swapchain support details
    m_queueFamilyIndices = queueFamilyIndices;
    m_swapChainSupportDetails = swapchainSupportDetails;

#ifndef NDEBUG
    // Print the queue families selected
    log_debug("Selected Queue Family Indices:");
    try {
        log_debug("- Graphics Family: " + std::to_string(m_queueFamilyIndices.graphicsFamily.value()));
        log_debug("- Transfer Family: " + std::to_string(m_queueFamilyIndices.transferFamily.value()));
        log_debug("- Presentation Family: " + std::to_string(m_queueFamilyIndices.presentationFamily.value()));
    } catch (const std::exception& e) {
        log_error("Queue family indices may not have been set properly!");
    }
#endif

}

void VulkanEngine::create_vulkan_logical_device() {
    // Safety check
    if (!m_queueFamilyIndices.isComplete()) {
        log_error("RUNTIME ERROR: Queue family indices not set correctly!");
        throw std::runtime_error("RUNTIME ERROR: Queue family indices not set correctly!");
    }
    
    // Queue create infos for each queue family:
    std::set<uint32_t> queueFamilyIndices = {
        m_queueFamilyIndices.graphicsFamily.value(),
        m_queueFamilyIndices.presentationFamily.value(),
        m_queueFamilyIndices.transferFamily.value()
    };
    std::vector<VkDeviceQueueCreateInfo> deviceQueueCreateInfos{};
    float queuePriority{ 1.0f };
    for (uint32_t queueFamilyIndex : queueFamilyIndices) {
        // Queue Create Info
        VkDeviceQueueCreateInfo deviceQueueCreateInfo{};
        deviceQueueCreateInfo.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
        deviceQueueCreateInfo.queueFamilyIndex = queueFamilyIndex;
        deviceQueueCreateInfo.queueCount = 1;
        deviceQueueCreateInfo.pQueuePriorities = &queuePriority;

        // Push this struct to the vector
        deviceQueueCreateInfos.push_back(deviceQueueCreateInfo);
    }

    // Specify the Physical Device Features we'll be using to the logical device
    VkPhysicalDeviceVulkan12Features vulkan12Features{};  // Vulkan 1.2 features
    vulkan12Features.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES;
    vulkan12Features.bufferDeviceAddress = VK_TRUE;
    vulkan12Features.descriptorIndexing = VK_TRUE;

    VkPhysicalDeviceVulkan13Features vulkan13Features{};  // Vulkan 1.3 features
    vulkan13Features.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES;
    vulkan13Features.pNext = &vulkan12Features;
    vulkan13Features.dynamicRendering = VK_TRUE;
    vulkan13Features.synchronization2 = VK_TRUE;

    VkPhysicalDeviceFeatures2 physicalDeviceFeatures2{};  // Chaining to 1.3 and 1.2 features
    physicalDeviceFeatures2.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2;
    physicalDeviceFeatures2.pNext = &vulkan13Features;


    // Logical Device create info:
    VkDeviceCreateInfo deviceCreateInfo{};
    deviceCreateInfo.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
    deviceCreateInfo.pNext = &physicalDeviceFeatures2;  // Chain the Vulkan 1.3 and 1.2 Physical Device features
    deviceCreateInfo.enabledExtensionCount = static_cast<uint32_t>(m_requiredPhysicalDeviceExtensions.size());
    deviceCreateInfo.ppEnabledExtensionNames = m_requiredPhysicalDeviceExtensions.data();
    deviceCreateInfo.queueCreateInfoCount = static_cast<uint32_t>(deviceQueueCreateInfos.size());
    deviceCreateInfo.pQueueCreateInfos = deviceQueueCreateInfos.data();

    // Create the Logical Device:
    VkResult result = vkCreateDevice(m_vulkanPhysicalDevice, &deviceCreateInfo, nullptr, &m_vulkanLogicalDevice);
    if (result != VK_SUCCESS) {
        log_error("RUNTIME ERROR: Failed to create Vulkan Logical Device!");
        throw std::runtime_error("RUNTIME ERROR: Failed to create Vulkan Logical Device!");
    }
    log_success("Created Vulkan Logical Device.");

    // Get the handles to the queues:
    vkGetDeviceQueue(m_vulkanLogicalDevice, m_queueFamilyIndices.graphicsFamily.value(), 0, &m_graphicsQueue);
    vkGetDeviceQueue(m_vulkanLogicalDevice, m_queueFamilyIndices.presentationFamily.value(), 0, &m_presentationQueue);
    vkGetDeviceQueue(m_vulkanLogicalDevice, m_queueFamilyIndices.transferFamily.value(), 0, &m_transferQueue);
    log_success("Retrieved Queue Handles.");

}

void VulkanEngine::create_vulkan_swapchain() {
    // Choose and set the desired properties of our Swapchain
    VkSurfaceFormatKHR surfaceFormat = choose_swapchain_surface_format(m_swapChainSupportDetails.surfaceFormats);
    VkPresentModeKHR presentModeKHR = choose_swapchain_present_mode(m_swapChainSupportDetails.presentationModes, VK_PRESENT_MODE_FIFO_KHR);
    VkExtent2D swapchainExtent2D = choose_swapchain_extent_2D(m_swapChainSupportDetails.surfaceCapabilities);

    // We would like one image more than the min supported images in the swapchain by the device (ensured that its clamped)
    uint32_t swapChainImagesCount = std::clamp(
        m_swapChainSupportDetails.surfaceCapabilities.minImageCount + 1, 
        m_swapChainSupportDetails.surfaceCapabilities.minImageCount, 
        m_swapChainSupportDetails.surfaceCapabilities.maxImageCount
    );

    // Swapchain Create Info
    VkSwapchainCreateInfoKHR swapchainCreateInfo{};
    swapchainCreateInfo.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
    swapchainCreateInfo.surface = m_vulkanSurface;
    swapchainCreateInfo.minImageCount = swapChainImagesCount;
    swapchainCreateInfo.imageFormat = surfaceFormat.format;
    swapchainCreateInfo.imageColorSpace = surfaceFormat.colorSpace;
    swapchainCreateInfo.imageExtent = swapchainExtent2D;
    swapchainCreateInfo.imageArrayLayers = 1;  // 2D Application
    swapchainCreateInfo.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    std::vector<uint32_t> queueFamilyIndices = { m_queueFamilyIndices.graphicsFamily.value(), m_queueFamilyIndices.presentationFamily.value() };
    if (m_queueFamilyIndices.graphicsFamily.value() != m_queueFamilyIndices.presentationFamily.value()) {
        swapchainCreateInfo.imageSharingMode = VK_SHARING_MODE_CONCURRENT;
        swapchainCreateInfo.pQueueFamilyIndices = queueFamilyIndices.data();
        swapchainCreateInfo.queueFamilyIndexCount = static_cast<uint32_t>(queueFamilyIndices.size());
    }
    else {
        swapchainCreateInfo.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
        swapchainCreateInfo.pQueueFamilyIndices = nullptr;
        swapchainCreateInfo.queueFamilyIndexCount = 0;
    }
    swapchainCreateInfo.preTransform = m_swapChainSupportDetails.surfaceCapabilities.currentTransform;  // no pre-transform
    swapchainCreateInfo.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
    swapchainCreateInfo.presentMode = presentModeKHR;
    swapchainCreateInfo.clipped = VK_TRUE;
    swapchainCreateInfo.oldSwapchain = VK_NULL_HANDLE;

    // Create the Swapchain
    VkResult result = vkCreateSwapchainKHR(m_vulkanLogicalDevice, &swapchainCreateInfo, nullptr, &m_vulkanSwapchainKHR);
    if (result != VK_SUCCESS) {
        log_error("RUNTIME ERROR: Failed to create Vulkan Swapchain!");
        throw std::runtime_error("RUNTIME ERROR: Failed to create Vulkan Swapchain!");
    }
    log_success("Created Vulkan Swapchain.");

    // Populate the member variables:
    m_swapchainExtent2D = swapchainExtent2D;
    m_swapchainSurfaceFormat = surfaceFormat.format;
    m_swapchainSurfaceColorspace = surfaceFormat.colorSpace;

    // Retrieve the handles to the Swapchain Images:
    vkGetSwapchainImagesKHR(m_vulkanLogicalDevice, m_vulkanSwapchainKHR, &swapChainImagesCount, nullptr);
    m_swapchainImages.resize(swapChainImagesCount);
    vkGetSwapchainImagesKHR(m_vulkanLogicalDevice, m_vulkanSwapchainKHR, &swapChainImagesCount, m_swapchainImages.data());

    // Create Swapchain Image Views:
    m_swapchainImageViews.resize(m_swapchainImages.size());
    for (size_t i{ 0 }; i < m_swapchainImageViews.size(); i++) {
        // Create an image view corresponding to each swapchain image
        m_swapchainImageViews.at(i) = create_image_view(
            m_swapchainImages.at(i),
            m_swapchainSurfaceFormat,
            VK_IMAGE_ASPECT_COLOR_BIT
        );
    }

}


// Vulkan Extensions Helper Functions -------------------------------------------------------------

void VulkanEngine::list_vulkan_instance_extensions() {
#ifndef NDEBUG
    uint32_t instanceExtensionsCount{};
    std::vector<VkExtensionProperties> vulkanInstanceExtensions{};

    vkEnumerateInstanceExtensionProperties(nullptr, &instanceExtensionsCount, nullptr);
    vulkanInstanceExtensions.resize(instanceExtensionsCount);
    vkEnumerateInstanceExtensionProperties(nullptr, &instanceExtensionsCount, vulkanInstanceExtensions.data());

    log_debug("Supported Vulkan Instance Extensions: (" + std::to_string(instanceExtensionsCount) + ")");
    for (const VkExtensionProperties& extension : vulkanInstanceExtensions) {
        log_debug(extension.extensionName);
    }
#endif
}


// Vulkan Device related Helper Functions ---------------------------------------------------------

/// @brief Finds the queue family indices: [graphics, presentation and transfer] in the Physical Device.
/// @brief NOTE: If 'findDedicatedTransferFamily' is true, it will TRY to find a dedicated transfer family. If not it will fallback to the graphics family index.
QueueFamilyIndices VulkanEngine::find_required_queue_families(VkPhysicalDevice physicalDevice, bool findDedicatedTransferFamily) {
    QueueFamilyIndices indices{};

    // Get the list of queue family properties
    uint32_t queueFamiliesCount{ 0 };
    vkGetPhysicalDeviceQueueFamilyProperties(physicalDevice, &queueFamiliesCount, nullptr);
    std::vector<VkQueueFamilyProperties> queueFamilyProperties(queueFamiliesCount);
    vkGetPhysicalDeviceQueueFamilyProperties(physicalDevice, &queueFamiliesCount, queueFamilyProperties.data());

    // Find the queue family indices:
    for (size_t i{ 0 }; i < queueFamilyProperties.size(); i++) {
        // Check for presentation support by queue family
        VkBool32 presentationSupport{ false };
        vkGetPhysicalDeviceSurfaceSupportKHR(physicalDevice, i, m_vulkanSurface, &presentationSupport);

        // Graphics Family
        if (!indices.graphicsFamily && (queueFamilyProperties.at(i).queueFlags & VK_QUEUE_GRAPHICS_BIT)) {
            indices.graphicsFamily = i;
        }

        // Presentation Family
        if (!indices.presentationFamily && presentationSupport == true) {
            indices.presentationFamily = i;
        }

        // Transfer Family
        if (!findDedicatedTransferFamily) {
            // No need for a dedicated Transfer Family
            if (!indices.transferFamily && (queueFamilyProperties.at(i).queueFlags & VK_QUEUE_TRANSFER_BIT)) {
                indices.transferFamily = i;
            }
        }
        else {
            // Need to try and find a dedicated Transfer Family (separate from Graphics queue)
            if ((queueFamilyProperties.at(i).queueFlags & VK_QUEUE_TRANSFER_BIT) && !(queueFamilyProperties.at(i).queueFlags & VK_QUEUE_GRAPHICS_BIT)) {
                indices.transferFamily = i;
            }
        }

        // Check if all queue family indices have been populated.
        if (indices.isComplete()) {
            break;  // early return from loop
        }
    }

    // If we couldn't find a dedicated Transfer Family
    if (findDedicatedTransferFamily && !indices.transferFamily) {
        // Fallback to the Graphics Queue (guaranteed to have transfer family)
        indices.transferFamily = indices.graphicsFamily;
    }

    return indices;
}

bool VulkanEngine::check_physical_device_supports_required_extensions(VkPhysicalDevice physicalDevice, std::vector<const char*> requiredDeviceExtensions) {
    uint32_t availableExtensionsCount{};
    vkEnumerateDeviceExtensionProperties(physicalDevice, nullptr, &availableExtensionsCount, nullptr);
    std::vector<VkExtensionProperties> availableExtensions(availableExtensionsCount);
    vkEnumerateDeviceExtensionProperties(physicalDevice, nullptr, &availableExtensionsCount, availableExtensions.data());

    // Create a set of required extensions (for easily checking if it is supported)
    std::set<std::string> requiredExtensions(requiredDeviceExtensions.begin(), requiredDeviceExtensions.end());

    // Tick of all the required extensions that are available
    for (const auto& extension : availableExtensions) {
        requiredExtensions.erase(extension.extensionName);
    }

    return requiredExtensions.empty();
}


// Vulkan Swapchain related Helper Functions ------------------------------------------------------

SwapChainSupportDetails VulkanEngine::query_swapchain_support(VkPhysicalDevice physicalDevice, VkSurfaceKHR surface) {
    SwapChainSupportDetails swapchainSupportDetails{};

    // Query surface capabilities of the physical device
    vkGetPhysicalDeviceSurfaceCapabilitiesKHR(physicalDevice, surface, &swapchainSupportDetails.surfaceCapabilities);
    
    // Get the supported surface formats
    uint32_t surfaceFormatsCount{};
    vkGetPhysicalDeviceSurfaceFormatsKHR(physicalDevice, surface, &surfaceFormatsCount, nullptr);
    swapchainSupportDetails.surfaceFormats.resize(surfaceFormatsCount);
    vkGetPhysicalDeviceSurfaceFormatsKHR(physicalDevice, surface, &surfaceFormatsCount, swapchainSupportDetails.surfaceFormats.data());

    // Get the supported presentation modes
    uint32_t presentModesCount{};
    vkGetPhysicalDeviceSurfacePresentModesKHR(physicalDevice, surface, &presentModesCount, nullptr);
    swapchainSupportDetails.presentationModes.resize(presentModesCount);
    vkGetPhysicalDeviceSurfacePresentModesKHR(physicalDevice, surface, &presentModesCount, swapchainSupportDetails.presentationModes.data());

    return swapchainSupportDetails;
}

VkSurfaceFormatKHR VulkanEngine::choose_swapchain_surface_format(const std::vector<VkSurfaceFormatKHR>& surfaceFormats) {
    for (const VkSurfaceFormatKHR& surfaceFormat : surfaceFormats) {
        if (surfaceFormat.format == VK_FORMAT_B8G8R8A8_UNORM && surfaceFormat.colorSpace == VK_COLORSPACE_SRGB_NONLINEAR_KHR) {
            return surfaceFormat;
        }
    }
    return surfaceFormats.at(0);
}

VkPresentModeKHR VulkanEngine::choose_swapchain_present_mode(const std::vector<VkPresentModeKHR>& presentModes, VkPresentModeKHR desiredPresentMode) {
    for (const VkPresentModeKHR& presentMode : presentModes) {
        if (presentMode == desiredPresentMode) {
            return presentMode;
        }
    }
    return VK_PRESENT_MODE_FIFO_KHR;
}

VkExtent2D VulkanEngine::choose_swapchain_extent_2D(const VkSurfaceCapabilitiesKHR& surfaceCapabilities) {
    if (surfaceCapabilities.currentExtent.width != std::numeric_limits<uint32_t>::max()) {
        // If its not max, that means the GPU requires a fixed width & height for the swapchain (eg: mobile GPUs)
        return surfaceCapabilities.currentExtent;
    }
    VkExtent2D actualExtent = {
        m_windowExtent.width,
        m_windowExtent.height
    };
    actualExtent.width = std::clamp(actualExtent.width, surfaceCapabilities.minImageExtent.width, surfaceCapabilities.maxImageExtent.width);
    actualExtent.height = std::clamp(actualExtent.height, surfaceCapabilities.minImageExtent.height, surfaceCapabilities.maxImageExtent.height);
    return actualExtent;
}


// General Vulkan Helper Functions ----------------------------------------------------------------

VkImageView VulkanEngine::create_image_view(VkImage image, VkFormat format, VkImageAspectFlags aspectFlags) {
    VkImageViewCreateInfo imageViewCreateInfo{};
    imageViewCreateInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    imageViewCreateInfo.image = image;
    imageViewCreateInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
    imageViewCreateInfo.format = format;
    imageViewCreateInfo.subresourceRange.aspectMask = aspectFlags;
    imageViewCreateInfo.subresourceRange.baseMipLevel = 0;
    imageViewCreateInfo.subresourceRange.levelCount = 1;
    imageViewCreateInfo.subresourceRange.baseArrayLayer = 0;
    imageViewCreateInfo.subresourceRange.layerCount = 1;

    VkImageView imageView;
    if (vkCreateImageView(m_vulkanLogicalDevice, &imageViewCreateInfo, nullptr, &imageView) != VK_SUCCESS) {
        throw std::runtime_error("failed to create texture image view!");
    }

    return imageView;
}


// Vulkan Validation Layers Helper Functions ------------------------------------------------------

/// @brief Checks if the requested Vulkan Validation layers are supported or not.
bool VulkanEngine::check_vulkan_validation_layers_support(std::vector<const char*> validationLayers) {
    uint32_t layersCount{};
    vkEnumerateInstanceLayerProperties(&layersCount, nullptr);
    std::vector<VkLayerProperties> availableLayers(layersCount);
    vkEnumerateInstanceLayerProperties(&layersCount, availableLayers.data());

    for (const char* requestedLayer : validationLayers) {
        bool layerFound{ false };
        for (const VkLayerProperties& layerProperty : availableLayers) {
            if (strcmp(requestedLayer, layerProperty.layerName) == 0) {
                layerFound = true;
                break;
            }
        }
        if (!layerFound) {
            return false;
        }
    }
    return true;
}

/// @brief The callback used by Vulkan validation layers to log their outputs.
VKAPI_ATTR VkBool32 VKAPI_CALL VulkanEngine::debug_callback(VkDebugUtilsMessageSeverityFlagBitsEXT messageSeverity, VkDebugUtilsMessageTypeFlagsEXT messageType, const VkDebugUtilsMessengerCallbackDataEXT* pCallbackData, void* pUserData) {
    
    if (messageSeverity >= VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT) {
        // Message is important enough to show:        
        if (messageSeverity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT) {
            // Warning
            fmt::print(fg(fmt::color::yellow), "[{:%T}] [VALIDATION LAYER - WARNING] {}\n", std::chrono::system_clock::now(), pCallbackData->pMessage);
        }
        else if (messageSeverity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT) {
            // Error
            fmt::print(fg(fmt::color::red) | fmt::emphasis::bold, "[{:%T}] [VALIDATION LAYER - ERROR] {}\n", std::chrono::system_clock::now(), pCallbackData->pMessage);
        }
    }
    return VK_FALSE;
}

void VulkanEngine::populate_debug_messenger_create_info(VkDebugUtilsMessengerCreateInfoEXT& createInfo) {
    createInfo = {};
    createInfo.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT;
    createInfo.messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_VERBOSE_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
    createInfo.messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;
    createInfo.pfnUserCallback = debug_callback;
}


// SDL-Vulkan Extension Helper Functions ----------------------------------------------------------

void VulkanEngine::get_sdl_vulkan_extensions(SDL_Window* window) {
    // Get count of SDL Vulkan extensions
    if (!SDL_Vulkan_GetInstanceExtensions(window, &m_sdlVulkanExtensionsCount, nullptr)) {
        log_error("RUNTIME ERROR: Failed to get count of SDL Vulkan extensions!");
        throw std::runtime_error("RUNTIME ERROR: Failed to get count of SDL Vulkan extensions!");
    }
    // Resize the vector to hold the no. of extensions required
    m_sdlVulkanExtensionNames.resize(m_sdlVulkanExtensionsCount);
    // Get the actual names of the SDL Vulkan extensions
    if (!SDL_Vulkan_GetInstanceExtensions(window, &m_sdlVulkanExtensionsCount, m_sdlVulkanExtensionNames.data())) {
        log_error("RUNTIME ERROR: Failed to get SDL Vulkan extensions!");
        throw std::runtime_error("RUNTIME ERROR: Failed to get SDL Vulkan extensions!");
    }
    list_sdl_vulkan_extensions();
    log_success("Fetched SDL Vulkan extensions.");
}

void VulkanEngine::list_sdl_vulkan_extensions() {
    if (m_sdlVulkanExtensionNames.size() == 0)
        return;
    log_debug("SDL Vulkan Extensions: (" + std::to_string(m_sdlVulkanExtensionsCount) + ")");
    for (const char* extension : m_sdlVulkanExtensionNames) {
        log_debug(extension);
    }
}


// Cleanup Helper Functions -----------------------------------------------------------------------

void VulkanEngine::cleanup_swapchain() {
    // Destroy the Swapchain Image Views:
    for (VkImageView imageView : m_swapchainImageViews) {
        vkDestroyImageView(m_vulkanLogicalDevice, imageView, nullptr);
    }
    vkDestroySwapchainKHR(m_vulkanLogicalDevice, m_vulkanSwapchainKHR, nullptr);
}

void VulkanEngine::cleanup_command_pools() {
    for (size_t i{ 0 }; i < m_frameData.size(); i++) {
        vkDestroyCommandPool(m_vulkanLogicalDevice, m_frameData.at(i).commandPool, nullptr);
        // The command buffers associated with the pools are automatically cleaned up...
    }
}


// Logging Functions using fmt --------------------------------------------------------------------

void VulkanEngine::log_error(const std::string& msg) {
    fmt::print(fg(fmt::color::red) | fmt::emphasis::bold, "[{:%T}] [ERROR] {}\n", std::chrono::system_clock::now(), msg);
}

void VulkanEngine::log_warning(const std::string& msg) {
    fmt::print(fg(fmt::color::yellow), "[{:%T}] [WARNING] {}\n", std::chrono::system_clock::now(), msg);
}

void VulkanEngine::log_info(const std::string& msg) {
    fmt::print(fg(fmt::color::cyan), "[{:%T}] [INFO] {}\n", std::chrono::system_clock::now(), msg);
}

void VulkanEngine::log_debug(const std::string& msg) {
#ifndef NDEBUG
    fmt::print(fg(fmt::color::gray), "[{:%T}] [DEBUG] {}\n", std::chrono::system_clock::now(), msg);
#endif
}

void VulkanEngine::log_success(const std::string& msg) {
    fmt::print(fg(fmt::color::green), "[{:%T}] [SUCCESS] {}\n", std::chrono::system_clock::now(), msg);
}

