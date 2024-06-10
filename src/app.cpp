#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <limits>
#include <optional>
#include <ranges>
#include <set>
#include <vector>

#include <vulkan/vulkan.h>
#include <SDL.h>
#include <SDL_vulkan.h>

#include "app.h"

static constexpr auto WIDTH = 800;
static constexpr auto HEIGHT = 600;
static constexpr auto VALIDATION_LAYER = "VK_LAYER_KHRONOS_validation";
static const std::vector<const char*> DEVICE_EXTENSIONS = {
	VK_KHR_SWAPCHAIN_EXTENSION_NAME
};

namespace VkDraw {
	struct QueueFamilyIndex {
		std::optional<uint32_t> gfx_family;
		std::optional<uint32_t> present_family;
	};

	struct SwapchainSupport {
		VkSurfaceCapabilitiesKHR capabilities{};
		std::vector<VkSurfaceFormatKHR> formats;
		std::vector<VkPresentModeKHR> present_modes;
	};

	static SDL_Window* _window;
	static SDL_Renderer* _renderer;

	static VkApplicationInfo _app_info{};
	static VkInstance _instance{};
	static VkSurfaceKHR _surface{};
	static std::vector<VkExtensionProperties> _supported_extensions;
	static std::vector<const char*> _required_extensions;
	static VkPhysicalDevice _physical_device = nullptr;
	static VkDevice _logical_device = nullptr;
	static QueueFamilyIndex _queue_family;
	static VkQueue _gfx_queue;
	static VkQueue _present_queue;
	static SwapchainSupport _swapchain_support;
	static VkSurfaceFormatKHR _swapchain_format;
	static VkPresentModeKHR _swapchain_mode = VK_PRESENT_MODE_FIFO_KHR;
	static VkExtent2D _swapchain_extent;
	static VkSwapchainKHR _swapchain;
	static std::vector<VkImage> _swapchain_images;
	static std::vector<VkImageView> _swapchain_image_views;

#ifdef NDEBUG
	static bool _use_validation = false;
#else
	static bool _use_validation = true;
#endif

	int run(std::span<std::string_view> args) {
		// print all arguements
		for (auto [idx, arg] : std::views::enumerate(args)) {
			std::printf("arg[%zu] = %s\n", idx, arg.data());
			// TODO: parse arguements
		}

		if (SDL_Init(SDL_INIT_VIDEO) != 0) {
			std::fprintf(stderr, "SDL: Failed to initialize!\n");
			return EXIT_FAILURE;
		}

		if (_window = SDL_CreateWindow("VkDraw", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, WIDTH, HEIGHT,
		                               SDL_WINDOW_VULKAN); _window == nullptr) {
			std::fprintf(stderr, "SDL: Window could not be created!\n");
			return EXIT_FAILURE;
		}

		if (_renderer = SDL_CreateRenderer(_window, -1, SDL_RENDERER_ACCELERATED); _renderer == nullptr) {
			std::fprintf(stderr, "SDL: Renderer could not be created!\n");
			return EXIT_FAILURE;
		}

		_app_info.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
		_app_info.pNext = nullptr;
		_app_info.pApplicationName = "VkDraw";
		_app_info.applicationVersion = VK_MAKE_VERSION(1, 0, 0);
		_app_info.pEngineName = "NA";
		_app_info.engineVersion = VK_MAKE_VERSION(1, 0, 0);
		_app_info.apiVersion = VK_API_VERSION_1_0;

		// check supported Vulkan extension
		{
			uint32_t count;
			vkEnumerateInstanceExtensionProperties(nullptr, &count, nullptr);
			_supported_extensions.resize(count);
			vkEnumerateInstanceExtensionProperties(nullptr, &count, _supported_extensions.data());

			std::printf("Vulkan: %u extension/s supported {\n", count);
			for (const auto& ext : _supported_extensions) {
				std::printf("\t%s\n", ext.extensionName);
			}
			std::printf("}\n");
		}

		// check required Vulkan extensions
		{
			uint32_t count;
			SDL_Vulkan_GetInstanceExtensions(_window, &count, nullptr);
			_required_extensions.resize(count);
			SDL_Vulkan_GetInstanceExtensions(_window, &count, _required_extensions.data());

			// TODO: push additional required extensions

			std::printf("Vulkan: %u extension/s required {\n", count);
			for (const auto ext : _required_extensions) {
				std::printf("\t%s\n", ext);
			}
			std::printf("}\n");
		}

		// check supported Vulkan layers
		if (_use_validation) {
			uint32_t count;
			vkEnumerateInstanceLayerProperties(&count, nullptr);
			std::vector<VkLayerProperties> layers(count);
			vkEnumerateInstanceLayerProperties(&count, layers.data());

			bool found = false;
			std::printf("Vulkan: %u layers/s supported {\n", count);
			for (auto layer : layers) {
				std::printf("\t%s\n", layer.layerName);
				if (strcmp(layer.layerName, VALIDATION_LAYER) == 0) {
					found = true;
				}
			}
			std::printf("}\n");

			if (!found) {
				std::fprintf(stderr, "Vulkan: Validation layers requested but not supported!");
				return EXIT_FAILURE;
			}
		}

		// create Vulkan instance
		{
			VkInstanceCreateInfo info{};
			info.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
			info.pApplicationInfo = &_app_info;
			info.enabledExtensionCount = _required_extensions.size();
			info.ppEnabledExtensionNames = _required_extensions.data();

			if (_use_validation) {
				info.enabledLayerCount = 1;
				info.ppEnabledLayerNames = &VALIDATION_LAYER;
			} else {
				info.enabledLayerCount = 0;
				info.ppEnabledLayerNames = nullptr;
			}

			if (vkCreateInstance(&info, nullptr, &_instance) != VK_SUCCESS) {
				std::fprintf(stderr, "Vulkan: Failed to create instance!");
				return EXIT_FAILURE;
			}
		}

		// create window surface
		{
			if (SDL_Vulkan_CreateSurface(_window, _instance, &_surface) != SDL_TRUE) {
				std::fprintf(stderr, "Vulkan: Failed to create window surface!");
				return EXIT_FAILURE;
			}
		}

		// select appropriate GPU
		{
			uint32_t count;
			vkEnumeratePhysicalDevices(_instance, &count, nullptr);
			std::vector<VkPhysicalDevice> devices(count);
			vkEnumeratePhysicalDevices(_instance, &count, devices.data());

			std::printf("Vulkan: %u device/s found {\n", count);
			for (const auto& device : devices) {
				VkPhysicalDeviceProperties properties;
				vkGetPhysicalDeviceProperties(device, &properties);

				std::printf("\t%s\n", properties.deviceName);

				bool dedicated = properties.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU;
				bool supports_extensions = true;

				// check if device supports required extensions
				{
					uint32_t ext_count;
					vkEnumerateDeviceExtensionProperties(device, nullptr, &ext_count, nullptr);
					std::vector<VkExtensionProperties> ext_properties(ext_count);
					vkEnumerateDeviceExtensionProperties(device, nullptr, &ext_count, ext_properties.data());

					for (const auto& required : DEVICE_EXTENSIONS) {
						bool found = false;
						for (const auto& ext : ext_properties) {
							if (strcmp(ext.extensionName, required) == 0) {
								found = true;
								break;
							}
						}
						if (!found) {
							supports_extensions = false;
							break;
						}
					}
				}

				// TODO: also check queue family support
				// TODO: also check swapchain supprt
				// TODO: "rank" devices by non-essential features
				if (dedicated && supports_extensions) {
					_physical_device = device;
				}
			}
			std::printf("}\n");

			if (_physical_device == nullptr) {
				std::fprintf(stderr, "Vulkan: No suitable graphics device was found!");
				return EXIT_FAILURE;
			}
		}

		// find queue families
		{
			uint32_t count;
			vkGetPhysicalDeviceQueueFamilyProperties(_physical_device, &count, nullptr);
			std::vector<VkQueueFamilyProperties> families(count);
			vkGetPhysicalDeviceQueueFamilyProperties(_physical_device, &count, families.data());

			for (auto [idx, family] : std::views::enumerate(families)) {
				bool support_gfx = family.queueFlags & VK_QUEUE_GRAPHICS_BIT;
				if (support_gfx) {
					_queue_family.gfx_family = idx;
				}

				VkBool32 support_presentation = false;
				vkGetPhysicalDeviceSurfaceSupportKHR(_physical_device, idx, _surface, &support_presentation);
				if (support_presentation) {
					_queue_family.present_family = idx;
				}

				if (support_gfx && support_presentation) {
					break;
				}
			}

			if (!_queue_family.gfx_family.has_value()) {
				std::fprintf(stderr, "Vulkan: No suitable graphics queue family available!");
				return EXIT_FAILURE;
			}
			if (!_queue_family.present_family.has_value()) {
				std::fprintf(stderr, "Vulkan: No sutiable presentation queue family available!");
				return EXIT_FAILURE;
			}
		}

		// create logical device
		{
			std::vector<VkDeviceQueueCreateInfo> families;
			std::set<uint32_t> unique_familes = {
				_queue_family.gfx_family.value(), _queue_family.present_family.value()
			};
			float priority = 1.0f;

			for (auto family : unique_familes) {
				VkDeviceQueueCreateInfo info{};
				info.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
				info.queueFamilyIndex = family;
				info.queueCount = 1;
				info.pQueuePriorities = &priority;
				families.push_back(info);
			}

			VkPhysicalDeviceFeatures features{};
			// TODO: add features

			VkDeviceCreateInfo info{};
			info.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
			info.pQueueCreateInfos = families.data();
			info.queueCreateInfoCount = families.size();
			info.pEnabledFeatures = &features;
			info.ppEnabledExtensionNames = DEVICE_EXTENSIONS.data();
			info.enabledExtensionCount = DEVICE_EXTENSIONS.size();

			if (_use_validation) {
				info.enabledLayerCount = 1;
				info.ppEnabledLayerNames = &VALIDATION_LAYER;
			} else {
				info.enabledLayerCount = 0;
				info.ppEnabledLayerNames = nullptr;
			}

			if (vkCreateDevice(_physical_device, &info, nullptr, &_logical_device) != VK_SUCCESS) {
				std::fprintf(stderr, "Vulkan: Failed to create logical device!");
				return EXIT_FAILURE;
			}
		}

		// get device queues
		{
			vkGetDeviceQueue(_logical_device, _queue_family.gfx_family.value(), 0, &_gfx_queue);
			vkGetDeviceQueue(_logical_device, _queue_family.present_family.value(), 0, &_present_queue);
		}

		// get swapchain support information
		{
			// get surface capabilities
			vkGetPhysicalDeviceSurfaceCapabilitiesKHR(_physical_device, _surface, &_swapchain_support.capabilities);

			// get surface formats
			{
				uint32_t count;
				vkGetPhysicalDeviceSurfaceFormatsKHR(_physical_device, _surface, &count, nullptr);
				_swapchain_support.formats.resize(count);
				vkGetPhysicalDeviceSurfaceFormatsKHR(_physical_device, _surface, &count,
				                                     _swapchain_support.formats.data());
			}

			// get surface presentation modes
			{
				uint32_t count;
				vkGetPhysicalDeviceSurfacePresentModesKHR(_physical_device, _surface, &count, nullptr);
				_swapchain_support.present_modes.resize(count);
				vkGetPhysicalDeviceSurfacePresentModesKHR(_physical_device, _surface, &count,
				                                          _swapchain_support.present_modes.data());
			}

			if (_swapchain_support.formats.empty() || _swapchain_support.present_modes.empty()) {
				std::fprintf(stderr, "Vulkan: No sutiable swapchain available!");
				return EXIT_FAILURE;
			}
		}

		// select swapchain format
		{
			for (auto format : _swapchain_support.formats) {
				if (format.format == VK_FORMAT_B8G8R8A8_SRGB && format.colorSpace ==
					VK_COLOR_SPACE_SRGB_NONLINEAR_KHR) {
					_swapchain_format = format;
					break;
				}
			}
			// TODO: "rank" format preferences
		}

		// select swapchain presentation mode
		{
			for (auto mode : _swapchain_support.present_modes) {
				if (mode == VK_PRESENT_MODE_MAILBOX_KHR) {
					_swapchain_mode = mode;
				}
			}
			// TODO: consider FIFO for low power device
		}

		// select swapchain extent
		{
			if (_swapchain_support.capabilities.currentExtent.width == std::numeric_limits<uint32_t>::max()) {
				int width;
				int height;
				SDL_Vulkan_GetDrawableSize(_window, &width, &height);

				_swapchain_extent.width = std::clamp(static_cast<uint32_t>(width),
				                                     _swapchain_support.capabilities.minImageExtent.width,
				                                     _swapchain_support.capabilities.maxImageExtent.width);
				_swapchain_extent.height = std::clamp(static_cast<uint32_t>(height),
				                                      _swapchain_support.capabilities.minImageExtent.height,
				                                      _swapchain_support.capabilities.maxImageExtent.height);
			} else {
				_swapchain_extent = _swapchain_support.capabilities.currentExtent;
			}
		}

		// create swapchain (probs needs to be function)
		{
			uint32_t image_count = std::min(_swapchain_support.capabilities.minImageCount + 1,
			                                _swapchain_support.capabilities.maxImageCount);

			VkSwapchainCreateInfoKHR info{};
			info.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
			info.surface = _surface;
			info.minImageCount = image_count;
			info.imageFormat = _swapchain_format.format;
			info.imageColorSpace = _swapchain_format.colorSpace;
			info.imageExtent = _swapchain_extent;
			info.imageArrayLayers = 1; // unless using VR
			info.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT; // render direct to image for now
			info.preTransform = _swapchain_support.capabilities.currentTransform;
			info.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
			info.presentMode = _swapchain_mode;
			info.clipped = VK_TRUE;
			info.oldSwapchain = nullptr;

			uint32_t queue_indexs[] = {_queue_family.gfx_family.value(), _queue_family.present_family.value()};

			if (_queue_family.gfx_family == _queue_family.present_family) {
				info.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
			} else {
				info.imageSharingMode = VK_SHARING_MODE_CONCURRENT;
				info.queueFamilyIndexCount = 2;
				info.pQueueFamilyIndices = queue_indexs;
			}

			if (vkCreateSwapchainKHR(_logical_device, &info, nullptr, &_swapchain) != VK_SUCCESS) {
				std::fprintf(stderr, "Vulkan: Failed to create swapchain!");
				return EXIT_FAILURE;
			}
		}

		// get swapchain images and image views
		{
			uint32_t count;
			vkGetSwapchainImagesKHR(_logical_device, _swapchain, &count, nullptr);
			_swapchain_images.resize(count);
			vkGetSwapchainImagesKHR(_logical_device, _swapchain, &count, _swapchain_images.data());

			_swapchain_image_views.resize(count);
			for (uint32_t i = 0; i < count; i++) {
				VkImageViewCreateInfo info;
				info.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
				info.image = _swapchain_images[i];
				info.viewType = VK_IMAGE_VIEW_TYPE_2D;
				info.format = _swapchain_format.format;
				info.components.r = VK_COMPONENT_SWIZZLE_IDENTITY;
				info.components.g = VK_COMPONENT_SWIZZLE_IDENTITY;
				info.components.b = VK_COMPONENT_SWIZZLE_IDENTITY;
				info.components.a = VK_COMPONENT_SWIZZLE_IDENTITY;
				info.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
				info.subresourceRange.baseMipLevel = 0;
				info.subresourceRange.levelCount = 1;
				info.subresourceRange.baseArrayLayer = 0;
				info.subresourceRange.layerCount = 1;

				if (vkCreateImageView(_logical_device, &info, nullptr, &_swapchain_image_views[i]) != VK_SUCCESS) {
					std::fprintf(stderr, "Vulkan: Failed to create image view");
					return EXIT_FAILURE;
				}
			}
		}

		SDL_Event event;
		bool running = true;

		while (running) {
			while (SDL_PollEvent(&event)) {
				if (event.type == SDL_QUIT) {
					running = false;
					break;
				}
			}

			SDL_SetRenderDrawColor(_renderer, 0, 0, 0, SDL_ALPHA_OPAQUE);
			SDL_RenderClear(_renderer);

			// TODO: draw frame here...

			SDL_RenderPresent(_renderer);
		}

		for (auto view : _swapchain_image_views) {
			vkDestroyImageView(_logical_device, view, nullptr);
		}

		vkDestroySwapchainKHR(_logical_device, _swapchain, nullptr);
		vkDestroyDevice(_logical_device, nullptr);
		vkDestroySurfaceKHR(_instance, _surface, nullptr);
		vkDestroyInstance(_instance, nullptr);

		SDL_DestroyRenderer(_renderer);
		SDL_DestroyWindow(_window);
		SDL_Quit();

		return EXIT_SUCCESS;
	}
}
