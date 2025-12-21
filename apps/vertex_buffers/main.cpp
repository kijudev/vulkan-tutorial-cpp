#include <algorithm>
#include <array>
#include <cstddef>
#include <fstream>
#include <ios>
#include <limits>
#include <set>
#include <string>
#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>
#include <vulkan/vk_platform.h>
#include <vulkan/vulkan.h>
#include <vulkan/vulkan_core.h>

#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <exception>
#include <iostream>
#include <map>
#include <optional>
#include <stdexcept>
#include <utility>
#include <vector>

class QueueFamilyIndices {
   public:
    std::optional<uint32_t> graphics_family;
    std::optional<uint32_t> present_family;

    bool is_complete() { return graphics_family.has_value() && present_family.has_value(); }
};

class SwapChainSupportDetails {
   public:
    VkSurfaceCapabilitiesKHR        capabilities;
    std::vector<VkSurfaceFormatKHR> formats;
    std::vector<VkPresentModeKHR>   present_modes;
};

class TriangleApplication {
   private:
    static constexpr uint32_t WINDOW_WIDTH         = 800;
    static constexpr uint32_t WINDOW_HEIGHT        = 600;
    static constexpr uint32_t MAX_FRAMES_IN_FLIGHT = 2;

#ifdef NDEBUG
    static constexpr bool ENABLE_VALIDATION_LAYERS = false;
#else
    static constexpr bool ENABLE_VALIDATION_LAYERS = true;
#endif

    GLFWwindow*                m_window                 = nullptr;
    VkInstance                 m_instance               = VK_NULL_HANDLE;
    VkDebugUtilsMessengerEXT   m_debug_messenger        = VK_NULL_HANDLE;
    VkSurfaceKHR               m_surface                = VK_NULL_HANDLE;
    VkPhysicalDevice           m_physical_device        = VK_NULL_HANDLE;
    VkDevice                   m_logical_device         = VK_NULL_HANDLE;
    VkQueue                    m_graphics_queue         = VK_NULL_HANDLE;
    VkQueue                    m_present_queue          = VK_NULL_HANDLE;
    VkSwapchainKHR             m_swapchain              = VK_NULL_HANDLE;
    std::vector<VkImage>       m_swapchain_images       = {};
    VkFormat                   m_swapchain_format       = VK_FORMAT_UNDEFINED;
    VkExtent2D                 m_swapchain_extent       = {};
    std::vector<VkImageView>   m_swapchain_image_views  = {};
    VkRenderPass               m_render_pass            = VK_NULL_HANDLE;
    VkPipelineLayout           m_pipeline_layout        = VK_NULL_HANDLE;
    VkPipeline                 m_graphics_pipeline      = VK_NULL_HANDLE;
    std::vector<VkFramebuffer> m_swapchain_framebuffers = {};
    VkCommandPool              m_command_pool           = VK_NULL_HANDLE;

    std::array<VkCommandBuffer, MAX_FRAMES_IN_FLIGHT> m_command_buffers            = {VK_NULL_HANDLE};
    std::array<VkSemaphore, MAX_FRAMES_IN_FLIGHT>     m_semaphores_image_available = {VK_NULL_HANDLE};
    std::vector<VkSemaphore>                          m_semaphores_render_finished = {};
    std::array<VkFence, MAX_FRAMES_IN_FLIGHT>         m_fences_in_flight           = {VK_NULL_HANDLE};

    std::vector<VkFence> m_images_in_flight;

    uint32_t m_current_frame       = 0;
    bool     m_framebuffer_resized = true;

    const std::vector<const char*> m_validation_layers = {"VK_LAYER_KHRONOS_validation"};
    const std::vector<const char*> m_device_extensions = {VK_KHR_SWAPCHAIN_EXTENSION_NAME};

   public:
    void run() {
        init();
        main_loop();
        cleanup();
    }

   private:
    void init() {
        init_glfw();
        init_window();
        init_vulkan();
    }

    void init_glfw() {
        if (!glfwInit()) {
            throw std::runtime_error("TriangleApplication::init_glfw => Failed to initialize GLFW.");
        }
    }

    void init_window() {
        glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
        glfwWindowHint(GLFW_RESIZABLE, GLFW_TRUE);

        m_window = glfwCreateWindow(WINDOW_WIDTH, WINDOW_HEIGHT, "triangle", nullptr, nullptr);
        if (!m_window) {
            glfwTerminate();
            throw std::runtime_error("TriangleApplication::init_window => Failed to create GLFW window");
        }

        glfwSetWindowUserPointer(m_window, this);
        glfwSetFramebufferSizeCallback(m_window, framebuffer_resize_callback);
    }

    static void framebuffer_resize_callback(GLFWwindow* window, int width, int height) {
        TriangleApplication* app   = static_cast<TriangleApplication*>(glfwGetWindowUserPointer(window));
        app->m_framebuffer_resized = true;

        (void)width;
        (void)height;
    }

    void init_vulkan() {
        create_instance();
        check_extension_support();
        setup_debug_messenger();
        create_surface();
        pick_physical_device();
        create_logical_device();

        create_swapchain();
        create_image_views();

        create_render_pass();
        create_graphics_pipleline();
        create_framebuffers();
        create_command_pool();
        create_command_buffers();

        create_synchonization_objects();
    }

    void main_loop() {
        while (!glfwWindowShouldClose(m_window)) {
            glfwPollEvents();
            draw_frame();
        }
    }

    void cleanup() {
        // Wait until the device is idle before destroying resources to ensure no commands are
        // still referencing swapchain images, semaphores, fences, framebuffers, etc.
        if (m_logical_device != VK_NULL_HANDLE) {
            vkDeviceWaitIdle(m_logical_device);
        }

        cleanup_swapchain();

        vkDestroyPipeline(m_logical_device, m_graphics_pipeline, nullptr);
        vkDestroyPipelineLayout(m_logical_device, m_pipeline_layout, nullptr);
        vkDestroyRenderPass(m_logical_device, m_render_pass, nullptr);

        for (uint32_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
            vkDestroySemaphore(m_logical_device, m_semaphores_image_available[i], nullptr);
            vkDestroyFence(m_logical_device, m_fences_in_flight[i], nullptr);
        }

        vkDestroyCommandPool(m_logical_device, m_command_pool, nullptr);
        vkDestroyDevice(m_logical_device, nullptr);

        if (ENABLE_VALIDATION_LAYERS) {
            proxy_destroy_debug_utils_messenger_ext(m_instance, m_debug_messenger, nullptr);
        }

        vkDestroySurfaceKHR(m_instance, m_surface, nullptr);
        vkDestroyInstance(m_instance, nullptr);

        glfwDestroyWindow(m_window);
        glfwTerminate();
    }

    /* ---- Instance creation and validation layer helpers ---- */

    // Creates a vulkan instance
    // Creates vulcan create info
    // Sets up validation layers
    void create_instance() {
        if (ENABLE_VALIDATION_LAYERS && !(check_validation_layer_support())) {
            throw std::runtime_error(
                "TriangleApplication::create_instance => validation layers "
                "requested, but not available.");
        }

        VkApplicationInfo application_info{};
        application_info.sType              = VK_STRUCTURE_TYPE_APPLICATION_INFO;
        application_info.pApplicationName   = "triangle";
        application_info.applicationVersion = VK_MAKE_VERSION(1, 0, 0);
        application_info.pEngineName        = "no_engine";
        application_info.engineVersion      = VK_MAKE_VERSION(1, 0, 0);
        application_info.apiVersion         = VK_API_VERSION_1_0;

        VkInstanceCreateInfo application_create_info{};
        application_create_info.sType            = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
        application_create_info.pApplicationInfo = &application_info;

        std::vector<const char*> required_extensions = get_required_extensions();

        application_create_info.enabledExtensionCount   = static_cast<uint32_t>(required_extensions.size());
        application_create_info.ppEnabledExtensionNames = required_extensions.data();

        VkDebugUtilsMessengerCreateInfoEXT debug_messenger_create_info{};
        if (ENABLE_VALIDATION_LAYERS) {
            application_create_info.enabledLayerCount   = static_cast<uint32_t>(m_validation_layers.size());
            application_create_info.ppEnabledLayerNames = m_validation_layers.data();

            debug_messenger_create_info.sType           = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT;
            debug_messenger_create_info.messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_VERBOSE_BIT_EXT |
                                                          VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT |
                                                          VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
            debug_messenger_create_info.messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT |
                                                      VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT |
                                                      VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;
            debug_messenger_create_info.pfnUserCallback = debug_callback;
            debug_messenger_create_info.pUserData       = nullptr;
            application_create_info.pNext               = &debug_messenger_create_info;
        } else {
            application_create_info.enabledLayerCount = 0;
            application_create_info.pNext             = nullptr;
        }

        if (vkCreateInstance(&application_create_info, nullptr, &m_instance) != VK_SUCCESS) {
            throw std::runtime_error(
                "TriangleApplication::create_instance => Failed to create a "
                "Vulkan instance.");
        }
    }

    void create_swapchain() {
        SwapChainSupportDetails support_details = query_swapchain_support_details(m_physical_device);

        VkSurfaceFormatKHR surface_format = choose_swapchain_surface_format(support_details.formats);
        VkPresentModeKHR   present_mode   = choose_swapchain_present_mode(support_details.present_modes);
        VkExtent2D         extent         = choose_swapchain_extent(support_details.capabilities);

        uint32_t image_count = support_details.capabilities.minImageCount + 1;

        if (support_details.capabilities.maxImageCount > 0 &&
            image_count > support_details.capabilities.maxImageCount) {
            image_count = support_details.capabilities.maxImageCount;
        }

        VkSwapchainCreateInfoKHR create_info = {};
        create_info.sType                    = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
        create_info.surface                  = m_surface;
        create_info.minImageCount            = image_count;
        create_info.imageFormat              = surface_format.format;
        create_info.imageColorSpace          = surface_format.colorSpace;
        create_info.imageExtent              = extent;
        create_info.imageArrayLayers         = 1;
        create_info.imageUsage               = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;

        QueueFamilyIndices queue_family_indices         = find_queue_familiy_indices(m_physical_device);
        uint32_t           queue_family_indices_array[] = {queue_family_indices.graphics_family.value(),
                                                           queue_family_indices.present_family.value()};

        if (queue_family_indices.graphics_family.value() == queue_family_indices.present_family.value()) {
            create_info.imageSharingMode      = VK_SHARING_MODE_EXCLUSIVE;
            create_info.queueFamilyIndexCount = 1;
            create_info.pQueueFamilyIndices   = &queue_family_indices.graphics_family.value();
        } else {
            create_info.imageSharingMode      = VK_SHARING_MODE_CONCURRENT;
            create_info.queueFamilyIndexCount = 2;
            create_info.pQueueFamilyIndices   = queue_family_indices_array;
        }

        create_info.preTransform   = support_details.capabilities.currentTransform;
        create_info.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
        create_info.presentMode    = present_mode;
        create_info.clipped        = VK_TRUE;
        create_info.oldSwapchain   = VK_NULL_HANDLE;

        if (vkCreateSwapchainKHR(m_logical_device, &create_info, nullptr, &m_swapchain) != VK_SUCCESS) {
            throw std::runtime_error("TriangleApplication::create_swap_chain => failed to create swap chain!");
        }

        vkGetSwapchainImagesKHR(m_logical_device, m_swapchain, &image_count, nullptr);
        m_swapchain_images.resize(image_count);
        vkGetSwapchainImagesKHR(m_logical_device, m_swapchain, &image_count, m_swapchain_images.data());

        m_swapchain_format = surface_format.format;
        m_swapchain_extent = extent;

        // Ensure we have one "in-flight" fence slot per swapchain image to avoid semaphore reuse
        // races. Initialize to VK_NULL_HANDLE meaning that image is not currently in-flight.
        m_images_in_flight.resize(image_count, VK_NULL_HANDLE);

        // Create one render-finished semaphore per swapchain image. These semaphores are used
        // to signal that rendering to a particular swapchain image has finished before presenting.
        m_semaphores_render_finished.resize(image_count, VK_NULL_HANDLE);
        VkSemaphoreCreateInfo semaphore_create_info{};
        semaphore_create_info.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
        for (uint32_t i = 0; i < image_count; ++i) {
            if (vkCreateSemaphore(m_logical_device, &semaphore_create_info, nullptr,
                                  &m_semaphores_render_finished[i]) != VK_SUCCESS) {
                throw std::runtime_error(
                    "TriangleApplication::create_swapchain => failed to create render-finished semaphore!");
            }
        }
    }

    void check_extension_support() {
        uint32_t count = 0;
        vkEnumerateInstanceExtensionProperties(nullptr, &count, nullptr);

        std::vector<VkExtensionProperties> extensions(count);
        vkEnumerateInstanceExtensionProperties(nullptr, &count, extensions.data());

        std::cout << "TriangleApplication::check_extension_support => Available extensions:\n";
        for (const auto& extension : extensions) {
            std::cout << '\t' << extension.extensionName << '\n';
        }
    }

    bool check_validation_layer_support() {
        uint32_t count;
        vkEnumerateInstanceLayerProperties(&count, nullptr);

        std::vector<VkLayerProperties> available_layers(count);
        vkEnumerateInstanceLayerProperties(&count, available_layers.data());

        for (const char* layer_name : m_validation_layers) {
            bool layer_found = false;

            for (const auto& layer_properties : available_layers) {
                if (std::strcmp(layer_name, layer_properties.layerName) == 0) {
                    layer_found = true;
                    break;
                }
            }

            if (!layer_found) {
                return false;
            }
        }

        return true;
    }

    std::vector<const char*> get_required_extensions() {
        uint32_t     glfw_extion_count = 0;
        const char** glfw_extensions;

        glfw_extensions = glfwGetRequiredInstanceExtensions(&glfw_extion_count);

        std::vector<const char*> vulkan_extensions(glfw_extensions, glfw_extensions + glfw_extion_count);

        if (ENABLE_VALIDATION_LAYERS) {
            vulkan_extensions.push_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
        };

        return vulkan_extensions;
    }

    /* ---- Debug messenger helpers and proxies ---- */

    // Returns true if the debug message is to aborted
    static VKAPI_ATTR VkBool32 VKAPI_CALL debug_callback(VkDebugUtilsMessageSeverityFlagBitsEXT      message_severity,
                                                         VkDebugUtilsMessageTypeFlagsEXT             message_type,
                                                         const VkDebugUtilsMessengerCallbackDataEXT* ptr_callback_data,
                                                         void*                                       ptr_user_data) {
        std::cerr << "Validation layer: " << ptr_callback_data->pMessage << " Severity: " << message_severity
                  << " Type: " << message_type << ptr_user_data << std::endl;

        return VK_FALSE;
    }

    void setup_debug_messenger() {
        if (!ENABLE_VALIDATION_LAYERS) {
            return;
        }

        VkDebugUtilsMessengerCreateInfoEXT create_info{};
        create_info.sType           = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT;
        create_info.messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_VERBOSE_BIT_EXT |
                                      VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT |
                                      VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
        create_info.messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT |
                                  VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT |
                                  VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;
        create_info.pfnUserCallback = debug_callback;
        create_info.pUserData       = nullptr;

        if (proxy_create_debug_utils_messenger_ext(m_instance, &create_info, nullptr, &m_debug_messenger) !=
            VK_SUCCESS) {
            throw std::runtime_error(
                "TriangleApplication::setup_debug_messenger => failed to set "
                "up debug messenger.");
        }
    }

    static VkResult proxy_create_debug_utils_messenger_ext(VkInstance                                instance,
                                                           const VkDebugUtilsMessengerCreateInfoEXT* ptr_create_info,
                                                           const VkAllocationCallbacks*              ptr_allocator,
                                                           VkDebugUtilsMessengerEXT* ptr_debug_messenger) {
        auto func =
            (PFN_vkCreateDebugUtilsMessengerEXT)vkGetInstanceProcAddr(instance, "vkCreateDebugUtilsMessengerEXT");
        if (func != nullptr) {
            return func(instance, ptr_create_info, ptr_allocator, ptr_debug_messenger);
        } else {
            return VK_ERROR_EXTENSION_NOT_PRESENT;
        }
    }

    static void proxy_destroy_debug_utils_messenger_ext(VkInstance instance, VkDebugUtilsMessengerEXT messenger,
                                                        const VkAllocationCallbacks* ptr_allocator) {
        auto func =
            (PFN_vkDestroyDebugUtilsMessengerEXT)vkGetInstanceProcAddr(instance, "vkDestroyDebugUtilsMessengerEXT");
        if (func != nullptr) {
            func(instance, messenger, ptr_allocator);
        }
    }

    void create_surface() {
        if (glfwCreateWindowSurface(m_instance, m_window, nullptr, &m_surface) != VK_SUCCESS) {
            throw std::runtime_error("TriangleApplication::create_surface => failed to create window surface!");
        }
    }

    void pick_physical_device() {
        uint32_t count = 0;
        vkEnumeratePhysicalDevices(m_instance, &count, nullptr);

        if (count == 0) {
            throw std::runtime_error(
                "TriangleApplication::pick_physical_device => Failed to find GPUs with Vulkan "
                "support.");
        }

        std::vector<VkPhysicalDevice> devices(count);
        vkEnumeratePhysicalDevices(m_instance, &count, devices.data());

        std::multimap<uint32_t, VkPhysicalDevice> candidates;

        for (const auto& device : devices) {
            uint32_t score = rate_physical_device(device);
            candidates.insert(std::make_pair(score, device));
        }

        if (candidates.empty()) {
            throw std::runtime_error("TriangleApplication::pick_physical_device => Failed to find a suitable GPU.");
        }

        if (candidates.rbegin()->first > 0) {
            m_physical_device = candidates.rbegin()->second;
        } else {
            throw std::runtime_error("TriangleApplication::pick_physical_device => Failed to find a suitable GPU.");
        }

        if (m_physical_device == VK_NULL_HANDLE) {
            throw std::runtime_error("TriangleApplication::pick_physical_device => Failed to find a suitable GPU.");
        }
    }

    uint32_t rate_physical_device(VkPhysicalDevice device) {
        VkPhysicalDeviceProperties properties;
        VkPhysicalDeviceFeatures   features;

        vkGetPhysicalDeviceFeatures(device, &features);
        vkGetPhysicalDeviceProperties(device, &properties);

        int score = 0;

        if (properties.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU) {
            score += 1000;
        }

        score += properties.limits.maxImageDimension2D;

        if (!features.geometryShader) {
            return 0;
        }

        if (!is_physical_device_suitable(device)) {
            return 0;
        }

        return score;
    }

    bool is_physical_device_suitable(VkPhysicalDevice device) {
        QueueFamilyIndices queue_family_indices = find_queue_familiy_indices(device);

        bool are_extensions_supported = check_physical_device_extension_support(device);
        bool is_swap_chain_adequate   = are_extensions_supported ? check_swapchain_support(device) : false;

        return queue_family_indices.is_complete() && are_extensions_supported && is_swap_chain_adequate;
    }

    bool check_physical_device_extension_support(VkPhysicalDevice device) {
        uint32_t extension_count;
        vkEnumerateDeviceExtensionProperties(device, nullptr, &extension_count, nullptr);

        std::vector<VkExtensionProperties> available_extensions(extension_count);
        vkEnumerateDeviceExtensionProperties(device, nullptr, &extension_count, available_extensions.data());

        std::set<std::string> required_extensions(m_device_extensions.begin(), m_device_extensions.end());

        for (const auto& extension : available_extensions) {
            required_extensions.erase(extension.extensionName);
        }

        return required_extensions.empty();
    }

    bool check_swapchain_support(VkPhysicalDevice physical_device) {
        SwapChainSupportDetails details = query_swapchain_support_details(physical_device);
        return !details.formats.empty() && !details.present_modes.empty();
    }

    QueueFamilyIndices find_queue_familiy_indices(VkPhysicalDevice physical_device) {
        QueueFamilyIndices queue_family_indices{};

        uint32_t queue_family_count = 0;
        vkGetPhysicalDeviceQueueFamilyProperties(physical_device, &queue_family_count, nullptr);

        std::vector<VkQueueFamilyProperties> queue_families(queue_family_count);
        vkGetPhysicalDeviceQueueFamilyProperties(physical_device, &queue_family_count, queue_families.data());

        uint32_t i = 0;
        for (const auto& queue_family : queue_families) {
            if (queue_family.queueFlags & VK_QUEUE_GRAPHICS_BIT) {
                queue_family_indices.graphics_family = i;
            }

            VkBool32 has_presentation_support = false;
            vkGetPhysicalDeviceSurfaceSupportKHR(physical_device, i, m_surface, &has_presentation_support);
            if (has_presentation_support) {
                queue_family_indices.present_family = i;
            }

            if (queue_family_indices.is_complete()) {
                break;
            }

            ++i;
        }

        return queue_family_indices;
    }

    void create_logical_device() {
        QueueFamilyIndices queue_family_indices = find_queue_familiy_indices(m_physical_device);

        std::vector<VkDeviceQueueCreateInfo> queue_create_infos{};
        std::set<uint32_t>                   unique_queue_families = {queue_family_indices.graphics_family.value(),
                                                                      queue_family_indices.present_family.value()};

        float queue_priority = 1.0f;

        for (const uint32_t queue_family : unique_queue_families) {
            VkDeviceQueueCreateInfo queue_create_info = {};
            queue_create_info.sType                   = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
            queue_create_info.queueFamilyIndex        = queue_family;
            queue_create_info.queueCount              = 1;
            queue_create_info.pQueuePriorities        = &queue_priority;
            queue_create_infos.push_back(queue_create_info);
        }

        VkPhysicalDeviceFeatures physical_device_features = {};

        VkDeviceCreateInfo logical_device_create_info      = {};
        logical_device_create_info.sType                   = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
        logical_device_create_info.queueCreateInfoCount    = static_cast<uint32_t>(queue_create_infos.size());
        logical_device_create_info.pQueueCreateInfos       = queue_create_infos.data();
        logical_device_create_info.pEnabledFeatures        = &physical_device_features;
        logical_device_create_info.enabledExtensionCount   = static_cast<uint32_t>(m_device_extensions.size());
        logical_device_create_info.ppEnabledExtensionNames = m_device_extensions.data();

        // Previous implementations of Vulkan made a distinction between instance and device
        // specific validation layers. Backwards compatibility with older implementations of Vulkan.
        if (ENABLE_VALIDATION_LAYERS) {
            logical_device_create_info.enabledLayerCount   = static_cast<uint32_t>(m_validation_layers.size());
            logical_device_create_info.ppEnabledLayerNames = m_validation_layers.data();
        } else {
            logical_device_create_info.enabledLayerCount   = 0;
            logical_device_create_info.ppEnabledLayerNames = nullptr;
        }

        if (vkCreateDevice(m_physical_device, &logical_device_create_info, nullptr, &m_logical_device) != VK_SUCCESS) {
            throw std::runtime_error("TriangleApplication::create_logical_device => failed to create logical device!");
        }

        vkGetDeviceQueue(m_logical_device, queue_family_indices.graphics_family.value(), 0, &m_graphics_queue);
        vkGetDeviceQueue(m_logical_device, queue_family_indices.present_family.value(), 0, &m_present_queue);
    }

    SwapChainSupportDetails query_swapchain_support_details(VkPhysicalDevice physical_device) {
        SwapChainSupportDetails details;

        vkGetPhysicalDeviceSurfaceCapabilitiesKHR(physical_device, m_surface, &details.capabilities);

        uint32_t format_count;
        vkGetPhysicalDeviceSurfaceFormatsKHR(physical_device, m_surface, &format_count, nullptr);

        if (format_count != 0) {
            details.formats.resize(format_count);
            vkGetPhysicalDeviceSurfaceFormatsKHR(physical_device, m_surface, &format_count, details.formats.data());
        }

        uint32_t present_mode_count;
        vkGetPhysicalDeviceSurfacePresentModesKHR(physical_device, m_surface, &present_mode_count, nullptr);

        if (present_mode_count != 0) {
            details.present_modes.resize(present_mode_count);
            vkGetPhysicalDeviceSurfacePresentModesKHR(physical_device, m_surface, &present_mode_count,
                                                      details.present_modes.data());
        }

        return details;
    }

    VkSurfaceFormatKHR choose_swapchain_surface_format(const std::vector<VkSurfaceFormatKHR>& formats) {
        for (const auto& format : formats) {
            if (format.format == VK_FORMAT_B8G8R8A8_SRGB && format.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR) {
                return format;
            }
        }

        if (!formats.empty()) {
            return formats[0];
        }

        throw std::runtime_error(
            "TriangleApplication::choose_swap_chain_surface_format => Failed to find a suitable "
            "swap chain surface format.");
    }

    VkPresentModeKHR choose_swapchain_present_mode(const std::vector<VkPresentModeKHR>& present_modes) {
        for (const auto& present_mode : present_modes) {
            if (present_mode == VK_PRESENT_MODE_MAILBOX_KHR) {
                return present_mode;
            }
        }

        return VK_PRESENT_MODE_FIFO_KHR;
    }

    VkExtent2D choose_swapchain_extent(const VkSurfaceCapabilitiesKHR& capabilities) {
        if (capabilities.currentExtent.width != std::numeric_limits<uint32_t>::max()) {
            return capabilities.currentExtent;
        } else {
            int width{};
            int height{};

            glfwGetFramebufferSize(m_window, &width, &height);
            VkExtent2D extent = {static_cast<uint32_t>(width), static_cast<uint32_t>(height)};

            extent.width =
                std::clamp(extent.width, capabilities.minImageExtent.width, capabilities.maxImageExtent.width);
            extent.height =
                std::clamp(extent.height, capabilities.minImageExtent.height, capabilities.maxImageExtent.height);

            return extent;
        }
    }

    void create_render_pass() {
        VkAttachmentDescription color_attachment_description = {};
        color_attachment_description.format                  = m_swapchain_format;
        color_attachment_description.samples                 = VK_SAMPLE_COUNT_1_BIT;
        color_attachment_description.loadOp                  = VK_ATTACHMENT_LOAD_OP_CLEAR;
        color_attachment_description.storeOp                 = VK_ATTACHMENT_STORE_OP_STORE;
        color_attachment_description.stencilLoadOp           = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        color_attachment_description.stencilStoreOp          = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        color_attachment_description.initialLayout           = VK_IMAGE_LAYOUT_UNDEFINED;
        color_attachment_description.finalLayout             = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;

        VkAttachmentReference color_attachment_reference = {};
        color_attachment_reference.attachment            = 0;
        color_attachment_reference.layout                = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

        VkSubpassDescription subpass = {};
        subpass.pipelineBindPoint    = VK_PIPELINE_BIND_POINT_GRAPHICS;
        subpass.colorAttachmentCount = 1;
        subpass.pColorAttachments    = &color_attachment_reference;

        VkRenderPassCreateInfo render_pass_create_info = {};
        render_pass_create_info.sType                  = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
        render_pass_create_info.attachmentCount        = 1;
        render_pass_create_info.pAttachments           = &color_attachment_description;
        render_pass_create_info.subpassCount           = 1;
        render_pass_create_info.pSubpasses             = &subpass;

        if (vkCreateRenderPass(m_logical_device, &render_pass_create_info, nullptr, &m_render_pass) != VK_SUCCESS) {
            throw std::runtime_error("TriangleApplication::create_render_pass => Failed to create render pass!");
        }
    }

    void create_image_views() {
        m_swapchain_image_views.resize(m_swapchain_images.size());

        for (size_t i = 0; i < m_swapchain_image_views.size(); ++i) {
            VkImageViewCreateInfo create_info           = {};
            create_info.sType                           = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
            create_info.image                           = m_swapchain_images[i];
            create_info.viewType                        = VK_IMAGE_VIEW_TYPE_2D;
            create_info.format                          = m_swapchain_format;
            create_info.components.r                    = VK_COMPONENT_SWIZZLE_IDENTITY;
            create_info.components.g                    = VK_COMPONENT_SWIZZLE_IDENTITY;
            create_info.components.b                    = VK_COMPONENT_SWIZZLE_IDENTITY;
            create_info.components.a                    = VK_COMPONENT_SWIZZLE_IDENTITY;
            create_info.subresourceRange.aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT;
            create_info.subresourceRange.baseMipLevel   = 0;
            create_info.subresourceRange.levelCount     = 1;
            create_info.subresourceRange.baseArrayLayer = 0;
            create_info.subresourceRange.layerCount     = 1;

            if (vkCreateImageView(m_logical_device, &create_info, nullptr, &m_swapchain_image_views[i]) != VK_SUCCESS) {
                throw std::runtime_error("failed to create image views!");
            }
        }
    }

    void create_graphics_pipleline() {
        std::vector<char> vert_shader_code = read_file("bin/shaders/shader.vert.spv");
        std::vector<char> frag_shader_code = read_file("bin/shaders/shader.frag.spv");

        VkShaderModule vert_shader_module = create_shader_module(vert_shader_code);
        VkShaderModule frag_shader_module = create_shader_module(frag_shader_code);

        VkPipelineShaderStageCreateInfo vert_shader_stage_info{};
        vert_shader_stage_info.sType  = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        vert_shader_stage_info.stage  = VK_SHADER_STAGE_VERTEX_BIT;
        vert_shader_stage_info.module = vert_shader_module;
        vert_shader_stage_info.pName  = "main";

        VkPipelineShaderStageCreateInfo frag_shader_stage_info{};
        frag_shader_stage_info.sType  = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        frag_shader_stage_info.stage  = VK_SHADER_STAGE_FRAGMENT_BIT;
        frag_shader_stage_info.module = frag_shader_module;
        frag_shader_stage_info.pName  = "main";

        std::array<VkPipelineShaderStageCreateInfo, 2> shader_stages = {vert_shader_stage_info, frag_shader_stage_info};

        VkPipelineVertexInputStateCreateInfo vertex_input_info{};
        vertex_input_info.sType                           = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
        vertex_input_info.vertexBindingDescriptionCount   = 0;
        vertex_input_info.pVertexBindingDescriptions      = nullptr;
        vertex_input_info.vertexAttributeDescriptionCount = 0;
        vertex_input_info.pVertexAttributeDescriptions    = nullptr;

        VkPipelineInputAssemblyStateCreateInfo input_assembly_info{};
        input_assembly_info.sType                  = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
        input_assembly_info.topology               = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
        input_assembly_info.primitiveRestartEnable = VK_FALSE;

        VkViewport viewport{};
        viewport.x        = 0.0f;
        viewport.y        = 0.0f;
        viewport.width    = static_cast<float>(m_swapchain_extent.width);
        viewport.height   = static_cast<float>(m_swapchain_extent.height);
        viewport.minDepth = 0.0f;
        viewport.maxDepth = 1.0f;

        VkRect2D scissor{};
        scissor.offset = {0, 0};
        scissor.extent = m_swapchain_extent;

        std::vector<VkDynamicState> dynamic_states = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};

        VkPipelineDynamicStateCreateInfo dynamic_state_info{};
        dynamic_state_info.sType             = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
        dynamic_state_info.dynamicStateCount = static_cast<uint32_t>(dynamic_states.size());
        dynamic_state_info.pDynamicStates    = dynamic_states.data();

        VkPipelineViewportStateCreateInfo viewport_state_info{};
        viewport_state_info.sType         = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
        viewport_state_info.viewportCount = 1;
        viewport_state_info.scissorCount  = 1;

        // Idk about this one ???
        // https://vulkan-tutorial.com/en/Drawing_a_triangle/Graphics_pipeline_basics/Fixed_functions
        viewport_state_info.pViewports = &viewport;
        viewport_state_info.pScissors  = &scissor;

        VkPipelineRasterizationStateCreateInfo rasterization_state_info{};
        rasterization_state_info.sType                   = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
        rasterization_state_info.depthBiasClamp          = VK_FALSE;
        rasterization_state_info.rasterizerDiscardEnable = VK_FALSE;
        rasterization_state_info.polygonMode             = VK_POLYGON_MODE_FILL;
        rasterization_state_info.lineWidth               = 1.0f;
        rasterization_state_info.cullMode                = VK_CULL_MODE_BACK_BIT;
        rasterization_state_info.frontFace               = VK_FRONT_FACE_CLOCKWISE;
        rasterization_state_info.depthBiasEnable         = VK_FALSE;
        rasterization_state_info.depthBiasEnable         = VK_FALSE;

        // Optional
        rasterization_state_info.depthBiasConstantFactor = 0.0f;
        rasterization_state_info.depthBiasClamp          = 0.0f;
        rasterization_state_info.depthBiasSlopeFactor    = 0.0f;

        VkPipelineMultisampleStateCreateInfo multisample_state_info{};
        multisample_state_info.sType                 = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
        multisample_state_info.rasterizationSamples  = VK_SAMPLE_COUNT_1_BIT;
        multisample_state_info.sampleShadingEnable   = VK_FALSE;
        multisample_state_info.minSampleShading      = 1.0f;
        multisample_state_info.pSampleMask           = nullptr;
        multisample_state_info.alphaToCoverageEnable = VK_FALSE;
        multisample_state_info.alphaToOneEnable      = VK_FALSE;

        VkPipelineColorBlendAttachmentState color_blend_attachment_state{};
        color_blend_attachment_state.colorWriteMask =
            VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;

        // Color blending disabled for this example
        color_blend_attachment_state.blendEnable = VK_FALSE;

        // Optional blend factors and operations
        color_blend_attachment_state.srcColorBlendFactor = VK_BLEND_FACTOR_ONE;
        color_blend_attachment_state.dstColorBlendFactor = VK_BLEND_FACTOR_ZERO;
        color_blend_attachment_state.colorBlendOp        = VK_BLEND_OP_ADD;
        color_blend_attachment_state.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
        color_blend_attachment_state.dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO;
        color_blend_attachment_state.alphaBlendOp        = VK_BLEND_OP_ADD;

        VkPipelineColorBlendStateCreateInfo color_blend_state{};
        color_blend_state.sType           = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
        color_blend_state.logicOpEnable   = VK_FALSE;
        color_blend_state.logicOp         = VK_LOGIC_OP_COPY;  // Optional logic operation
        color_blend_state.attachmentCount = 1;
        color_blend_state.pAttachments    = &color_blend_attachment_state;

        // Optional blend constants
        color_blend_state.blendConstants[0] = 0.0f;
        color_blend_state.blendConstants[1] = 0.0f;
        color_blend_state.blendConstants[2] = 0.0f;
        color_blend_state.blendConstants[3] = 0.0f;

        VkPipelineLayoutCreateInfo pipeline_layout_info{};
        pipeline_layout_info.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;

        // Optional push constants
        pipeline_layout_info.setLayoutCount         = 0;
        pipeline_layout_info.pSetLayouts            = nullptr;
        pipeline_layout_info.pushConstantRangeCount = 0;
        pipeline_layout_info.pPushConstantRanges    = nullptr;

        if (vkCreatePipelineLayout(m_logical_device, &pipeline_layout_info, nullptr, &m_pipeline_layout) !=
            VK_SUCCESS) {
            throw std::runtime_error(
                "TriangleApplication::create_graphics_pipeline => failed to create pipeline "
                "layout!");
        }

        VkGraphicsPipelineCreateInfo pipeline_create_info{};
        pipeline_create_info.sType               = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
        pipeline_create_info.stageCount          = static_cast<uint32_t>(shader_stages.size());
        pipeline_create_info.pStages             = shader_stages.data();
        pipeline_create_info.pVertexInputState   = &vertex_input_info;
        pipeline_create_info.pInputAssemblyState = &input_assembly_info;
        pipeline_create_info.pViewportState      = &viewport_state_info;
        pipeline_create_info.pRasterizationState = &rasterization_state_info;
        pipeline_create_info.pMultisampleState   = &multisample_state_info;
        pipeline_create_info.pDepthStencilState  = nullptr;  // Optional;
        pipeline_create_info.pColorBlendState    = &color_blend_state;
        pipeline_create_info.pDynamicState       = &dynamic_state_info;

        pipeline_create_info.layout     = m_pipeline_layout;
        pipeline_create_info.renderPass = m_render_pass;
        pipeline_create_info.subpass    = 0;

        // Optional
        pipeline_create_info.basePipelineHandle = VK_NULL_HANDLE;
        pipeline_create_info.basePipelineIndex  = -1;

        if (vkCreateGraphicsPipelines(m_logical_device, VK_NULL_HANDLE, 1, &pipeline_create_info, nullptr,
                                      &m_graphics_pipeline) != VK_SUCCESS) {
            throw std::runtime_error(
                "TriangleApplication::create_graphics_pipeline => failed to create graphics "
                "pipeline!");
        }

        vkDestroyShaderModule(m_logical_device, vert_shader_module, nullptr);
        vkDestroyShaderModule(m_logical_device, frag_shader_module, nullptr);
    }

    static std::vector<char> read_file(const std::string& filename) {
        std::ifstream file(filename, std::ios::ate | std::ios::binary);

        if (!file.is_open()) {
            throw std::runtime_error("TriangleApplication::read_file => failed to open file!");
        }

        size_t            file_size = static_cast<size_t>(file.tellg());
        std::vector<char> buffer(file_size);

        file.seekg(0);
        file.read(buffer.data(), file_size);
        file.close();

        return buffer;
    }

    VkShaderModule create_shader_module(const std::vector<char>& code) {
        VkShaderModuleCreateInfo create_info{};
        create_info.sType    = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
        create_info.codeSize = code.size();
        create_info.pCode    = reinterpret_cast<const uint32_t*>(code.data());

        VkShaderModule shader_module;
        if (vkCreateShaderModule(m_logical_device, &create_info, nullptr, &shader_module) != VK_SUCCESS) {
            throw std::runtime_error("TriangleApplication::create_shader_module => failed to create shader module!");
        }

        return shader_module;
    }

    void create_framebuffers() {
        m_swapchain_framebuffers.resize(m_swapchain_image_views.size());

        for (size_t i = 0; i < m_swapchain_image_views.size(); ++i) {
            VkImageView attachments[] = {m_swapchain_image_views[i]};

            VkFramebufferCreateInfo framebuffer_create_info{};
            framebuffer_create_info.sType           = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
            framebuffer_create_info.renderPass      = m_render_pass;
            framebuffer_create_info.attachmentCount = 1;
            framebuffer_create_info.pAttachments    = attachments;
            framebuffer_create_info.width           = m_swapchain_extent.width;
            framebuffer_create_info.height          = m_swapchain_extent.height;
            framebuffer_create_info.layers          = 1;

            if (vkCreateFramebuffer(m_logical_device, &framebuffer_create_info, nullptr,
                                    &m_swapchain_framebuffers[i]) != VK_SUCCESS) {
                throw std::runtime_error("TriangleApplication::create_framebuffers => failed to create framebuffer!");
            }
        }
    }

    void create_command_pool() {
        QueueFamilyIndices queue_family_indices = find_queue_familiy_indices(m_physical_device);

        VkCommandPoolCreateInfo command_pool_create_info{};
        command_pool_create_info.sType            = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
        command_pool_create_info.queueFamilyIndex = queue_family_indices.graphics_family.value();
        command_pool_create_info.flags            = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;

        if (vkCreateCommandPool(m_logical_device, &command_pool_create_info, nullptr, &m_command_pool) != VK_SUCCESS) {
            throw std::runtime_error("TriangleApplication::create_command_pool => failed to create command pool!");
        }
    }

    void create_command_buffers() {
        VkCommandBufferAllocateInfo command_buffer_allocate_info{};
        command_buffer_allocate_info.sType              = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
        command_buffer_allocate_info.commandPool        = m_command_pool;
        command_buffer_allocate_info.level              = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        command_buffer_allocate_info.commandBufferCount = MAX_FRAMES_IN_FLIGHT;

        if (vkAllocateCommandBuffers(m_logical_device, &command_buffer_allocate_info, m_command_buffers.data()) !=
            VK_SUCCESS) {
            throw std::runtime_error(
                "TriangleApplication::create_command_buffer => failed to allocate command buffer!");
        }
    }

    void record_command_buffer(VkCommandBuffer command_buffer, uint32_t image_index) {
        VkCommandBufferBeginInfo command_buffer_begin_info{};
        command_buffer_begin_info.sType            = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
        command_buffer_begin_info.flags            = 0;        // Optional
        command_buffer_begin_info.pInheritanceInfo = nullptr;  // Optional

        if (vkBeginCommandBuffer(command_buffer, &command_buffer_begin_info) != VK_SUCCESS) {
            throw std::runtime_error(
                "TriangleApplication::record_command_buffer => failed to begin recording command "
                "buffer!");
        }

        VkRenderPassBeginInfo render_pass_begin_info{};
        render_pass_begin_info.sType             = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
        render_pass_begin_info.renderPass        = m_render_pass;
        render_pass_begin_info.framebuffer       = m_swapchain_framebuffers[image_index];
        render_pass_begin_info.renderArea.offset = {0, 0};
        render_pass_begin_info.renderArea.extent = m_swapchain_extent;

        VkClearValue clear_color               = {{{0.0f, 0.0f, 0.0f, 1.0f}}};
        render_pass_begin_info.clearValueCount = 1;
        render_pass_begin_info.pClearValues    = &clear_color;

        vkCmdBeginRenderPass(command_buffer, &render_pass_begin_info, VK_SUBPASS_CONTENTS_INLINE);
        vkCmdBindPipeline(m_command_buffers[m_current_frame], VK_PIPELINE_BIND_POINT_GRAPHICS, m_graphics_pipeline);

        VkViewport viewport{};
        viewport.x        = 0.0f;
        viewport.y        = 0.0f;
        viewport.width    = static_cast<float>(m_swapchain_extent.width);
        viewport.height   = static_cast<float>(m_swapchain_extent.height);
        viewport.minDepth = 0.0f;
        viewport.maxDepth = 1.0f;
        vkCmdSetViewport(command_buffer, 0, 1, &viewport);

        VkRect2D scissor{};
        scissor.offset = {0, 0};
        scissor.extent = m_swapchain_extent;
        vkCmdSetScissor(command_buffer, 0, 1, &scissor);

        vkCmdDraw(command_buffer, 3, 1, 0, 0);

        vkCmdEndRenderPass(command_buffer);

        if (vkEndCommandBuffer(command_buffer) != VK_SUCCESS) {
            throw std::runtime_error("TriangleApplication::record_command_buffer => failed to record command buffer!");
        }
    }

    void draw_frame() {
        vkWaitForFences(m_logical_device, 1, &m_fences_in_flight[m_current_frame], VK_TRUE, UINT64_MAX);

        uint32_t image_index{};
        VkResult acquire_image_result =
            vkAcquireNextImageKHR(m_logical_device, m_swapchain, UINT64_MAX,
                                  m_semaphores_image_available[m_current_frame], VK_NULL_HANDLE, &image_index);

        if (acquire_image_result == VK_ERROR_OUT_OF_DATE_KHR) {
            recreate_swapchain();
            return;
        } else if (acquire_image_result != VK_SUCCESS && acquire_image_result != VK_SUBOPTIMAL_KHR) {
            throw std::runtime_error("TriangleApplication::draw_frame => failed to acquire next image!");
        }

        // If this image is already in flight (used by a previous frame), wait for that fence
        // to ensure the image is available and the semaphores/fences are not still in use.
        if (!m_images_in_flight.empty() && m_images_in_flight[image_index] != VK_NULL_HANDLE) {
            vkWaitForFences(m_logical_device, 1, &m_images_in_flight[image_index], VK_TRUE, UINT64_MAX);
        }

        // Mark this image as now being in use by the current frame's fence.
        m_images_in_flight[image_index] = m_fences_in_flight[m_current_frame];

        vkResetFences(m_logical_device, 1, &m_fences_in_flight[m_current_frame]);

        vkResetCommandBuffer(m_command_buffers[m_current_frame], 0);
        record_command_buffer(m_command_buffers[m_current_frame], image_index);

        VkSubmitInfo submit_info{};
        submit_info.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;

        std::array<VkSemaphore, 1>          wait_semaphores = {m_semaphores_image_available[m_current_frame]};
        std::array<VkPipelineStageFlags, 1> wait_stages     = {VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT};

        // Use the render-finished semaphore dedicated to the acquired swapchain image.
        VkSemaphore render_finished_semaphore = VK_NULL_HANDLE;
        if (!m_semaphores_render_finished.empty()) {
            render_finished_semaphore = m_semaphores_render_finished[image_index];
        }

        std::array<VkSemaphore, 1> signal_semaphores = {render_finished_semaphore};

        submit_info.waitSemaphoreCount   = static_cast<uint32_t>(wait_semaphores.size());
        submit_info.pWaitSemaphores      = wait_semaphores.data();
        submit_info.pWaitDstStageMask    = wait_stages.data();
        submit_info.commandBufferCount   = 1;
        submit_info.pCommandBuffers      = &m_command_buffers[m_current_frame];
        submit_info.signalSemaphoreCount = static_cast<uint32_t>(signal_semaphores.size());
        submit_info.pSignalSemaphores    = signal_semaphores.data();

        if (vkQueueSubmit(m_graphics_queue, 1, &submit_info, m_fences_in_flight[m_current_frame]) != VK_SUCCESS) {
            throw std::runtime_error("TriangleApplication::draw_frame => failed to submit draw command buffer!");
        }

        VkSubpassDependency subpass_dependency{};
        subpass_dependency.srcSubpass    = VK_SUBPASS_EXTERNAL;
        subpass_dependency.dstSubpass    = 0;
        subpass_dependency.srcStageMask  = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        subpass_dependency.srcAccessMask = 0;
        subpass_dependency.dstStageMask  = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        subpass_dependency.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;

        VkRenderPassCreateInfo render_pass_create_info{};
        render_pass_create_info.sType           = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
        render_pass_create_info.dependencyCount = 1;
        render_pass_create_info.pDependencies   = &subpass_dependency;

        std::array<VkSwapchainKHR, 1> swapchains = {m_swapchain};

        VkPresentInfoKHR present_info{};
        present_info.sType              = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
        present_info.waitSemaphoreCount = (render_finished_semaphore == VK_NULL_HANDLE) ? 0u : 1u;
        present_info.pWaitSemaphores =
            (render_finished_semaphore == VK_NULL_HANDLE) ? nullptr : &render_finished_semaphore;
        present_info.swapchainCount = static_cast<uint32_t>(swapchains.size());
        present_info.pSwapchains    = swapchains.data();
        present_info.pImageIndices  = &image_index;
        present_info.pResults       = nullptr;  // Optional

        VkResult queue_present_result = vkQueuePresentKHR(m_present_queue, &present_info);
        if (queue_present_result == VK_ERROR_OUT_OF_DATE_KHR || queue_present_result == VK_SUBOPTIMAL_KHR ||
            m_framebuffer_resized) {
            m_framebuffer_resized = false;
            recreate_swapchain();
        } else if (queue_present_result != VK_SUCCESS) {
            throw std::runtime_error("failed to present swap chain image!");
        }

        m_current_frame = (m_current_frame + 1) % MAX_FRAMES_IN_FLIGHT;
    }

    void create_synchonization_objects() {
        VkSemaphoreCreateInfo semaphore_create_info{};
        semaphore_create_info.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;

        VkFenceCreateInfo fence_create_info{};
        fence_create_info.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
        fence_create_info.flags = VK_FENCE_CREATE_SIGNALED_BIT;

        for (uint32_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
            if (vkCreateSemaphore(m_logical_device, &semaphore_create_info, nullptr,
                                  &m_semaphores_image_available[i]) != VK_SUCCESS ||
                vkCreateFence(m_logical_device, &fence_create_info, nullptr, &m_fences_in_flight[i]) != VK_SUCCESS) {
                throw std::runtime_error(
                    "TriangleApplication::create_synchonization_objects => failed to create semaphores or fences!");
            }
        }
    }

    void recreate_swapchain() {
        int width = 0, height = 0;
        glfwGetFramebufferSize(m_window, &width, &height);
        while (width == 0 || height == 0) {
            glfwGetFramebufferSize(m_window, &width, &height);
            glfwWaitEvents();
        }

        vkDeviceWaitIdle(m_logical_device);

        cleanup_swapchain();

        create_swapchain();
        create_image_views();
        create_framebuffers();
    }

    void cleanup_swapchain() {
        for (VkFramebuffer framebuffer : m_swapchain_framebuffers) {
            vkDestroyFramebuffer(m_logical_device, framebuffer, nullptr);
        }

        for (VkImageView image_view : m_swapchain_image_views) {
            vkDestroyImageView(m_logical_device, image_view, nullptr);
        }

        // Destroy per-swapchain-image render-finished semaphores.
        for (VkSemaphore sem : m_semaphores_render_finished) {
            if (sem != VK_NULL_HANDLE) {
                vkDestroySemaphore(m_logical_device, sem, nullptr);
            }
        }
        m_semaphores_render_finished.clear();

        vkDestroySwapchainKHR(m_logical_device, m_swapchain, nullptr);
    }
};

int main() {
    TriangleApplication application;

    try {
        application.run();
    } catch (const std::exception& e) {
        std::cerr << e.what() << "\n";
        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}
