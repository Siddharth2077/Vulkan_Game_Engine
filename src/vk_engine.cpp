
#include "vk_engine.h"

#include <SDL.h>
#include <SDL_vulkan.h>
#include <vk_initializers.h>
#include <vk_types.h>
#include <chrono>
#include <thread>
#include <iostream>
#include <fmt/color.h>
#include <fmt/chrono.h>


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
}

void VulkanEngine::init_swapchain() {}

void VulkanEngine::init_commands() {}

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

