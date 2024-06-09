#include <ranges>
#include <cstdio>
#include <vector>
#include <cstdint>
#include <optional>
#include <set>

#include <SDL.h>
#include <SDL_vulkan.h>
#include <vulkan/vulkan.h>

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

		vkDestroyDevice(_logical_device, nullptr);
		vkDestroySurfaceKHR(_instance, _surface, nullptr);
		vkDestroyInstance(_instance, nullptr);

		SDL_DestroyRenderer(_renderer);
		SDL_DestroyWindow(_window);

		return EXIT_SUCCESS;
	}
}
