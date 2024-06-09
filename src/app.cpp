#include <ranges>
#include <cstdio>
#include <vector>
#include <cstdint>

#include <SDL.h>
#include <SDL_vulkan.h>
#include <vulkan/vulkan.h>

#include "app.h"

static constexpr auto WIDTH = 800;
static constexpr auto HEIGHT = 600;
static constexpr auto VALIDATION_LAYER = "VK_LAYER_KHRONOS_validation";

namespace VkDraw {
	static SDL_Window* _window;
	static SDL_Renderer* _renderer;

	static VkApplicationInfo _app_info{};
	static VkInstance _instance{};
	static std::vector<VkExtensionProperties> _extensions;

#ifdef NDEBUG
	static bool _use_validation = false;
#else
	static bool _use_validation = true;
#endif

	Result run(std::span<std::string_view> args) {
		// print all arguements
		for (auto [idx, arg] : std::views::enumerate(args)) {
			std::printf("arg[%zu] = %s\n", idx, arg.data());
			// TODO: parse arguements
		}

		if (_window = SDL_CreateWindow("VkDraw", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, WIDTH, HEIGHT,
		                               SDL_WINDOW_VULKAN); _window == nullptr) {
			std::fprintf(stderr, "SDL: Window could not be created!\n");
			return Result::FAILURE;
		}

		if (_renderer = SDL_CreateRenderer(_window, -1, SDL_RENDERER_ACCELERATED); _renderer == nullptr) {
			std::fprintf(stderr, "SDL: Renderer could not be created!\n");
			return Result::FAILURE;
		}

		_app_info.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
		_app_info.pNext = nullptr;
		_app_info.pApplicationName = "VkDraw";
		_app_info.applicationVersion = VK_MAKE_VERSION(1, 0, 0);
		_app_info.pEngineName = "NA";
		_app_info.engineVersion = VK_MAKE_VERSION(1, 0, 0);
		_app_info.apiVersion = VK_API_VERSION_1_0;

		// check Vulkan extension support
		{
			uint32_t count;
			vkEnumerateInstanceExtensionProperties(nullptr, &count, nullptr);
			_extensions.resize(count);
			vkEnumerateInstanceExtensionProperties(nullptr, &count, _extensions.data());

			std::printf("Vulkan: %u extension/s supported {\n", count);
			for (auto ext : _extensions) {
				std::printf("\t%s\n", ext.extensionName);
			}
			std::printf("}\n");
		}

		// check Vulkan validation layer support
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
				return Result::FAILURE;
			}
		}

		// create Vulkan instance
		{
			uint32_t count;
			SDL_Vulkan_GetInstanceExtensions(_window, &count, nullptr);
			std::vector<const char*> names(count);
			SDL_Vulkan_GetInstanceExtensions(_window, &count, names.data());

			std::printf("Vulkan: SDL requires %u extension/s {\n", count);
			for (const auto name : names) {
				std::printf("\t%s\n", name);
			}
			std::printf("}\n");

			VkInstanceCreateInfo info{};
			info.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
			info.pApplicationInfo = &_app_info;
			info.enabledExtensionCount = count;
			info.ppEnabledExtensionNames = names.data();

			if (_use_validation) {
				info.enabledLayerCount = 1;
				info.ppEnabledLayerNames = &VALIDATION_LAYER;
			} else {
				info.enabledLayerCount = 0;
				info.ppEnabledLayerNames = nullptr;
			}

			if (vkCreateInstance(&info, nullptr, &_instance) != VK_SUCCESS) {
				std::fprintf(stderr, "Vulkan: Failed to create instance!");
				return Result::FAILURE;
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

			// TODO: draw fram here...

			SDL_RenderPresent(_renderer);
		}

		vkDestroyInstance(_instance, nullptr);

		SDL_DestroyRenderer(_renderer);
		SDL_DestroyWindow(_window);

		return Result::SUCCESS;
	}
}
