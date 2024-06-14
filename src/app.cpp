#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdio>
#include <fstream>
#include <limits>
#include <optional>
#include <ranges>
#include <set>
#include <vector>

#include <vulkan/vulkan.h>
#include <SDL.h>
#include <SDL_vulkan.h>

#include "app.h"

static constexpr auto WIDTH = 1280;
static constexpr auto HEIGHT = 720;
static constexpr auto MAX_FRAMES_IN_FLIGHT = 2;

static constexpr std::string_view VERT_SHADER_PATH = "shaders/shader.vert.spv";
static constexpr std::string_view FRAG_SHADER_PATH = "shaders/shader.frag.spv";

static constexpr std::array VALIDATION_LAYERS = {
	"VK_LAYER_KHRONOS_validation"
};
static constexpr std::array DEVICE_EXTENSIONS = {
	"VK_KHR_swapchain"
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

	static SDL_Window *_window;
	static VkApplicationInfo _app_info{};
	static VkInstance _instance{};
	static VkSurfaceKHR _surface{};
	static std::vector<VkExtensionProperties> _supported_extensions;
	static std::vector<const char *> _required_extensions;
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
	static VkPipelineLayout _pipeline_layout;
	static VkRenderPass _render_pass;
	static VkPipeline _pipeline;
	static std::vector<VkFramebuffer> _framebuffers;
	static VkCommandPool _command_pool;
	static std::vector<VkCommandBuffer> _command_buffer;
	static std::vector<VkSemaphore> _image_available;
	static std::vector<VkSemaphore> _render_finished;
	static std::vector<VkFence> _in_flight;
	static uint32_t _current_frame = 0;
	static bool _window_resized = false;

#ifdef NDEBUG
	static bool _use_validation = false;
#else
	static bool _use_validation = true;
#endif

	static VkShaderModule create_module(const std::string_view path) {
		std::ifstream file(path.data(), std::ios::ate | std::ios::binary);
		if (!file.is_open()) {
			throw std::runtime_error("Failed to open shader file!");
		}

		const auto size = file.tellg();
		std::vector<char> code(size);
		std::printf("loaded %zu bytes from \"%s\"\n", code.size(), path.data());

		file.seekg(0);
		file.read(code.data(), size);
		file.close();

		VkShaderModuleCreateInfo info{};
		info.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
		info.codeSize = code.size();
		info.pCode = reinterpret_cast<const uint32_t *>(code.data());

		VkShaderModule module;
		if (vkCreateShaderModule(_logical_device, &info, nullptr, &module) != VK_SUCCESS) {
			throw std::runtime_error("Failed to create shader module!");
		}

		return module;
	}

	static void record_command(VkCommandBuffer cmd_buffer, uint32_t image_idx) {
		VkCommandBufferBeginInfo buffer_info{};
		buffer_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;

		if (vkBeginCommandBuffer(cmd_buffer, &buffer_info) != VK_SUCCESS) {
			throw std::runtime_error("Failed to begin command buffer!");
		}

		VkRenderPassBeginInfo render_info{};
		render_info.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
		render_info.renderPass = _render_pass;
		render_info.framebuffer = _framebuffers[image_idx];
		render_info.renderArea.offset = {0, 0};
		render_info.renderArea.extent = _swapchain_extent;

		VkClearValue clear_color = {{{0.0f, 0.0f, 0.0f, 1.0f}}};
		render_info.clearValueCount = 1;
		render_info.pClearValues = &clear_color;

		vkCmdBeginRenderPass(cmd_buffer, &render_info, VK_SUBPASS_CONTENTS_INLINE);
		vkCmdBindPipeline(cmd_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, _pipeline);

		VkViewport viewport{};
		viewport.x = 0.0f;
		viewport.y = 0.0f;
		viewport.width = static_cast<float>(_swapchain_extent.width);
		viewport.height = static_cast<float>(_swapchain_extent.height);
		viewport.minDepth = 0.0f;
		viewport.maxDepth = 1.0f;
		vkCmdSetViewport(cmd_buffer, 0, 1, &viewport);

		VkRect2D scissor{};
		scissor.offset = {0, 0};
		scissor.extent = _swapchain_extent;
		vkCmdSetScissor(cmd_buffer, 0, 1, &scissor);

		vkCmdDraw(cmd_buffer, 3, 1, 0, 0);
		vkCmdEndRenderPass(cmd_buffer);

		if (vkEndCommandBuffer(cmd_buffer) != VK_SUCCESS) {
			throw std::runtime_error("Failed to record command buffer!");
		}
	}

	static void create_swapchain() {
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
				throw std::runtime_error("No suitable swapchain available!");
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

		std::printf("Vulkan: creating swapchain (%ux%u)\n", _swapchain_extent.width, _swapchain_extent.height);

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

		uint32_t queue_indices[] = {_queue_family.gfx_family.value(), _queue_family.present_family.value()};

		if (_queue_family.gfx_family == _queue_family.present_family) {
			info.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
		} else {
			info.imageSharingMode = VK_SHARING_MODE_CONCURRENT;
			info.queueFamilyIndexCount = 2;
			info.pQueueFamilyIndices = queue_indices;
		}

		if (vkCreateSwapchainKHR(_logical_device, &info, nullptr, &_swapchain) != VK_SUCCESS) {
			throw std::runtime_error("Failed to create swapchain!");
		}
	}

	static void create_image_views() {
		uint32_t count;
		vkGetSwapchainImagesKHR(_logical_device, _swapchain, &count, nullptr);
		_swapchain_images.resize(count);
		vkGetSwapchainImagesKHR(_logical_device, _swapchain, &count, _swapchain_images.data());

		_swapchain_image_views.resize(count);
		for (uint32_t i = 0; i < count; i++) {
			VkImageViewCreateInfo info{};
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
				throw std::runtime_error("Failed to create image view!");
			}
		}
	}

	static void create_framebuffers() {
		_framebuffers.resize(_swapchain_image_views.size());

		for (size_t i = 0; i < _swapchain_image_views.size(); ++i) {
			VkImageView attachment[] = {
				_swapchain_image_views[i]
			};

			VkFramebufferCreateInfo info{};
			info.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
			info.renderPass = _render_pass;
			info.attachmentCount = 1;
			info.pAttachments = attachment;
			info.width = _swapchain_extent.width;
			info.height = _swapchain_extent.height;
			info.layers = 1;

			if (vkCreateFramebuffer(_logical_device, &info, nullptr, &_framebuffers[i]) != VK_SUCCESS) {
				throw std::runtime_error("Failed to create framebuffer!");
			}
		}
	}

	static void cleanup_swapchain() {
		for (const auto buffer : _framebuffers) {
			vkDestroyFramebuffer(_logical_device, buffer, nullptr);
		}
		for (const auto view : _swapchain_image_views) {
			vkDestroyImageView(_logical_device, view, nullptr);
		}
		vkDestroySwapchainKHR(_logical_device, _swapchain, nullptr);
	}

	static void recreate_swapchain() {
		if (SDL_GetWindowFlags(_window) & SDL_WINDOW_MINIMIZED) {
			return;
		}
		vkDeviceWaitIdle(_logical_device);
		cleanup_swapchain();
		create_swapchain();
		create_image_views();
		create_framebuffers();
		_window_resized = false;
	}

	static void draw_frame() {
		vkWaitForFences(_logical_device, 1, &_in_flight[_current_frame], VK_TRUE, UINT64_MAX);

		uint32_t image_idx;
		auto res = vkAcquireNextImageKHR(_logical_device, _swapchain, UINT64_MAX, _image_available[_current_frame],
		                                 VK_NULL_HANDLE, &image_idx);
		if (res == VK_ERROR_OUT_OF_DATE_KHR) {
			recreate_swapchain();
			return;
		} else if (res != VK_SUCCESS && res != VK_SUBOPTIMAL_KHR) {
			throw std::runtime_error("Failed to acquire swapchain images!");
		}

		vkResetFences(_logical_device, 1, &_in_flight[_current_frame]);

		vkResetCommandBuffer(_command_buffer[_current_frame], 0);
		record_command(_command_buffer[_current_frame], image_idx);

		VkSemaphore wait[] = {_image_available[_current_frame]};
		VkSemaphore signal[] = {_render_finished[_current_frame]};
		VkPipelineStageFlags wait_stage[] = {VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT};

		VkSubmitInfo submit{};
		submit.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
		submit.waitSemaphoreCount = 1;
		submit.pWaitSemaphores = wait;
		submit.pWaitDstStageMask = wait_stage;
		submit.commandBufferCount = 1;
		submit.pCommandBuffers = &_command_buffer[_current_frame];
		submit.signalSemaphoreCount = 1;
		submit.pSignalSemaphores = signal;

		if (vkQueueSubmit(_gfx_queue, 1, &submit, _in_flight[_current_frame]) != VK_SUCCESS) {
			throw std::runtime_error("Failed to submit queue!");
		}

		VkSwapchainKHR swapchains[] = {_swapchain};

		VkPresentInfoKHR present{};
		present.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
		present.waitSemaphoreCount = 1;
		present.pWaitSemaphores = signal;
		present.swapchainCount = 1;
		present.pSwapchains = swapchains;
		present.pImageIndices = &image_idx;

		res = vkQueuePresentKHR(_present_queue, &present);
		if (res == VK_ERROR_OUT_OF_DATE_KHR || res == VK_SUBOPTIMAL_KHR || _window_resized) {
			recreate_swapchain();
		} else if (res != VK_SUCCESS) {
			throw std::runtime_error("Failed to present swap chain image!");
		}

		_current_frame = (_current_frame + 1) % MAX_FRAMES_IN_FLIGHT;
	}

	int run(std::span<std::string_view> args) {
		// print all arguments
		for (auto [idx, arg] : std::views::enumerate(args)) {
			std::printf("arg[%zu] = %s\n", idx, arg.data());
			// TODO: parse arguments
		}

		if (SDL_Init(SDL_INIT_VIDEO) != 0) {
			throw std::runtime_error("Failed to initialize SDL!");
		}

		if (_window = SDL_CreateWindow("VkDraw", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, WIDTH, HEIGHT,
		                               SDL_WINDOW_VULKAN | SDL_WINDOW_RESIZABLE); _window == nullptr) {
			throw std::runtime_error("Failed to create SDL Window!");
		}

		uint32_t ver;
		vkEnumerateInstanceVersion(&ver);
		std::printf("Vulkan: API version = %d.%d.%d-%d\n",
		            VK_API_VERSION_MAJOR(ver),
		            VK_API_VERSION_MINOR(ver),
		            VK_API_VERSION_PATCH(ver),
		            VK_API_VERSION_VARIANT(ver)
		);

		if (ver < VK_API_VERSION_1_3) {
			throw std::runtime_error("Unsupported API version, must be at least version 1.3.0");
		}

		_app_info.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
		_app_info.pNext = nullptr;
		_app_info.pApplicationName = "VkDraw";
		_app_info.applicationVersion = VK_MAKE_VERSION(1, 0, 0);
		_app_info.pEngineName = "NA";
		_app_info.engineVersion = VK_MAKE_VERSION(1, 0, 0);
		_app_info.apiVersion = VK_API_VERSION_1_3;

		// check supported Vulkan extension
		{
			uint32_t count;
			vkEnumerateInstanceExtensionProperties(nullptr, &count, nullptr);
			_supported_extensions.resize(count);
			vkEnumerateInstanceExtensionProperties(nullptr, &count, _supported_extensions.data());

			std::printf("Vulkan: %u extension/s supported {\n", count);
			for (const auto &ext : _supported_extensions) {
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

			for (const auto &required : VALIDATION_LAYERS) {
				bool found = false;
				for (const auto &layer : layers) {
					if (strcmp(layer.layerName, required) == 0) {
						found = true;
						break;
					}
				}
				if (!found) {
					throw std::runtime_error("Requested validation layer is not supported");
				}
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
				info.enabledLayerCount = VALIDATION_LAYERS.size();
				info.ppEnabledLayerNames = VALIDATION_LAYERS.data();
			} else {
				info.enabledLayerCount = 0;
				info.ppEnabledLayerNames = nullptr;
			}

			if (vkCreateInstance(&info, nullptr, &_instance) != VK_SUCCESS) {
				throw std::runtime_error("Failed to create Vulkan instance!");
			}
		}

		// create window surface
		{
			if (SDL_Vulkan_CreateSurface(_window, _instance, &_surface) != SDL_TRUE) {
				throw std::runtime_error("Failed to create window surface!");
			}
		}

		// select appropriate GPU
		{
			uint32_t count;
			vkEnumeratePhysicalDevices(_instance, &count, nullptr);
			std::vector<VkPhysicalDevice> devices(count);
			vkEnumeratePhysicalDevices(_instance, &count, devices.data());

			std::printf("Vulkan: %u device/s found {\n", count);
			for (const auto &device : devices) {
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

					for (const auto &required : DEVICE_EXTENSIONS) {
						bool found = false;
						for (const auto &ext : ext_properties) {
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
				// TODO: also check swapchain support
				// TODO: "rank" devices by non-essential features
				if (dedicated && supports_extensions) {
					_physical_device = device;
				}
			}
			std::printf("}\n");

			if (_physical_device == nullptr) {
				throw std::runtime_error("No suitable graphics device was found!");
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
				throw std::runtime_error("No suitable graphics queue family available!");
			}
			if (!_queue_family.present_family.has_value()) {
				throw std::runtime_error("No suitable presentation queue family available!");
			}
		}

		// create logical device
		{
			std::vector<VkDeviceQueueCreateInfo> families;
			std::set<uint32_t> unique_families = {
				_queue_family.gfx_family.value(), _queue_family.present_family.value()
			};
			float priority = 1.0f;

			for (auto family : unique_families) {
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
				info.enabledLayerCount = VALIDATION_LAYERS.size();
				info.ppEnabledLayerNames = VALIDATION_LAYERS.data();
			} else {
				info.enabledLayerCount = 0;
				info.ppEnabledLayerNames = nullptr;
			}

			if (vkCreateDevice(_physical_device, &info, nullptr, &_logical_device) != VK_SUCCESS) {
				throw std::runtime_error("Failed to create logical device!");
			}
		}

		// get device queues
		{
			vkGetDeviceQueue(_logical_device, _queue_family.gfx_family.value(), 0, &_gfx_queue);
			vkGetDeviceQueue(_logical_device, _queue_family.present_family.value(), 0, &_present_queue);
		}

		create_swapchain();
		create_image_views();

		// create graphics pipeline
		{
			VkGraphicsPipelineCreateInfo pipeline_info{};
			pipeline_info.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;

			// create shader modules
			auto vert_shader = create_module(VERT_SHADER_PATH);
			auto frag_shader = create_module(FRAG_SHADER_PATH);

			VkPipelineShaderStageCreateInfo vert_stage{};
			vert_stage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
			vert_stage.stage = VK_SHADER_STAGE_VERTEX_BIT;
			vert_stage.module = vert_shader;
			vert_stage.pName = "main";

			VkPipelineShaderStageCreateInfo frag_stage{};
			frag_stage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
			frag_stage.stage = VK_SHADER_STAGE_FRAGMENT_BIT;
			frag_stage.module = frag_shader;
			frag_stage.pName = "main";

			VkPipelineShaderStageCreateInfo stages[] = {vert_stage, frag_stage};

			pipeline_info.stageCount = 2;
			pipeline_info.pStages = stages;

			// vertex input stage
			VkPipelineVertexInputStateCreateInfo vertex_input_stage{};
			vertex_input_stage.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
			vertex_input_stage.vertexBindingDescriptionCount = 0;
			vertex_input_stage.pVertexBindingDescriptions = nullptr;
			vertex_input_stage.vertexAttributeDescriptionCount = 0;
			vertex_input_stage.pVertexAttributeDescriptions = nullptr;
			pipeline_info.pVertexInputState = &vertex_input_stage;

			// input assembly
			VkPipelineInputAssemblyStateCreateInfo input_assembly{};
			input_assembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
			input_assembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
			input_assembly.primitiveRestartEnable = VK_FALSE;
			pipeline_info.pInputAssemblyState = &input_assembly;

			// viewport state
			VkPipelineViewportStateCreateInfo viewport_state{};
			viewport_state.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
			viewport_state.viewportCount = 1;
			viewport_state.scissorCount = 1;
			pipeline_info.pViewportState = &viewport_state;

			// rasterization
			VkPipelineRasterizationStateCreateInfo rasterization_stage{};
			rasterization_stage.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
			rasterization_stage.depthClampEnable = VK_FALSE;
			rasterization_stage.rasterizerDiscardEnable = VK_FALSE;
			rasterization_stage.polygonMode = VK_POLYGON_MODE_FILL;
			rasterization_stage.lineWidth = 1.0f;
			rasterization_stage.cullMode = VK_CULL_MODE_BACK_BIT;
			rasterization_stage.frontFace = VK_FRONT_FACE_CLOCKWISE;
			rasterization_stage.depthBiasEnable = VK_FALSE;
			pipeline_info.pRasterizationState = &rasterization_stage;

			// multisampling
			VkPipelineMultisampleStateCreateInfo multisampling_state{};
			multisampling_state.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
			multisampling_state.sampleShadingEnable = VK_FALSE;
			multisampling_state.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
			pipeline_info.pMultisampleState = &multisampling_state;

			// depth and stencil
			pipeline_info.pDepthStencilState = nullptr;

			// color blending
			VkPipelineColorBlendAttachmentState blend_attachment{};
			blend_attachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
			                                  VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
			blend_attachment.blendEnable = VK_FALSE;

			VkPipelineColorBlendStateCreateInfo blending_state{};
			blending_state.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
			blending_state.logicOpEnable = VK_FALSE;
			blending_state.attachmentCount = 1;
			blending_state.pAttachments = &blend_attachment;
			pipeline_info.pColorBlendState = &blending_state;

			// dynamic states
			std::vector<VkDynamicState> dynamic_states = {
				VK_DYNAMIC_STATE_VIEWPORT,
				VK_DYNAMIC_STATE_SCISSOR
			};

			VkPipelineDynamicStateCreateInfo dynamic_state_info{};
			dynamic_state_info.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
			dynamic_state_info.dynamicStateCount = dynamic_states.size();
			dynamic_state_info.pDynamicStates = dynamic_states.data();

			pipeline_info.pDynamicState = &dynamic_state_info;

			// pipeline layout
			{
				VkPipelineLayoutCreateInfo info{};
				info.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
				info.setLayoutCount = 0;
				info.pSetLayouts = nullptr;
				info.pushConstantRangeCount = 0;
				info.pPushConstantRanges = nullptr;

				if (vkCreatePipelineLayout(_logical_device, &info, nullptr, &_pipeline_layout) != VK_SUCCESS) {
					throw std::runtime_error("Failed to create pipeline layout!");
				}

				pipeline_info.layout = _pipeline_layout;
			}

			// create render pass
			{
				VkAttachmentDescription attachment{};
				attachment.format = _swapchain_format.format;
				attachment.samples = VK_SAMPLE_COUNT_1_BIT;
				attachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
				attachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
				attachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
				attachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
				attachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
				attachment.finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;

				VkAttachmentReference reference{};
				reference.attachment = 0;
				reference.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

				VkSubpassDescription subpass{};
				subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
				subpass.colorAttachmentCount = 1;
				subpass.pColorAttachments = &reference;

				VkSubpassDependency dependency{};
				dependency.srcSubpass = VK_SUBPASS_EXTERNAL;
				dependency.dstSubpass = 0;
				dependency.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
				dependency.srcAccessMask = 0;
				dependency.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
				dependency.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;

				VkRenderPassCreateInfo info{};
				info.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
				info.attachmentCount = 1;
				info.pAttachments = &attachment;
				info.subpassCount = 1;
				info.pSubpasses = &subpass;
				info.dependencyCount = 1;
				info.pDependencies = &dependency;

				if (vkCreateRenderPass(_logical_device, &info, nullptr, &_render_pass) != VK_SUCCESS) {
					throw std::runtime_error("Failed to create render pass!");
				}

				pipeline_info.renderPass = _render_pass;
				pipeline_info.subpass = 0;
			}

			pipeline_info.basePipelineHandle = VK_NULL_HANDLE;
			pipeline_info.basePipelineIndex = -1;

			if (vkCreateGraphicsPipelines(_logical_device, VK_NULL_HANDLE, 1, &pipeline_info, nullptr, &_pipeline) !=
			    VK_SUCCESS) {
				throw std::runtime_error("Failed to create graphics pipeline!");
			}

			// cleanup shader modules
			vkDestroyShaderModule(_logical_device, vert_shader, nullptr);
			vkDestroyShaderModule(_logical_device, frag_shader, nullptr);
		}

		create_framebuffers();

		// create command pools
		{
			VkCommandPoolCreateInfo info{};
			info.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
			info.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
			info.queueFamilyIndex = _queue_family.gfx_family.value();

			if (vkCreateCommandPool(_logical_device, &info, nullptr, &_command_pool) != VK_SUCCESS) {
				throw std::runtime_error("Failed to create command pool!");
			}
		}

		// setup in-flight arrays
		{
			_command_buffer.resize(MAX_FRAMES_IN_FLIGHT);
			_image_available.resize(MAX_FRAMES_IN_FLIGHT);
			_render_finished.resize(MAX_FRAMES_IN_FLIGHT);
			_in_flight.resize(MAX_FRAMES_IN_FLIGHT);
		}

		// create command buffers
		{
			VkCommandBufferAllocateInfo info{};
			info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
			info.commandPool = _command_pool;
			info.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
			info.commandBufferCount = MAX_FRAMES_IN_FLIGHT;

			if (vkAllocateCommandBuffers(_logical_device, &info, _command_buffer.data()) != VK_SUCCESS) {
				throw std::runtime_error("Failed to allocate command buffer!");
			}
		}

		// create synchronization object
		{
			VkSemaphoreCreateInfo sem_info{};
			sem_info.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;

			VkFenceCreateInfo fence_info{};
			fence_info.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
			fence_info.flags = VK_FENCE_CREATE_SIGNALED_BIT; // ensure first frame is not blocked

			for (int i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
				if (vkCreateSemaphore(_logical_device, &sem_info, nullptr, &_image_available[i]) != VK_SUCCESS) {
					throw std::runtime_error("Failed to create image_available semaphore!");
				}
				if (vkCreateSemaphore(_logical_device, &sem_info, nullptr, &_render_finished[i]) != VK_SUCCESS) {
					throw std::runtime_error("Failed to create render_finished semaphore!");
				}
				if (vkCreateFence(_logical_device, &fence_info, nullptr, &_in_flight[i]) != VK_SUCCESS) {
					throw std::runtime_error("Failed to create in_flight fence!");
				}
			}
		}

		SDL_Event event;
		bool running = true;

		auto last = static_cast<float>(SDL_GetTicks());
		float accumulator = 0.0f;
		float frame_count = 0.0f;

		while (running) {
			auto now = static_cast<float>(SDL_GetTicks());
			float delta = now - last;
			last = now;
			accumulator += delta;
			frame_count++;

			if (accumulator >= 1000) {
				char title[64];
				float avg = accumulator / frame_count;
				accumulator = 0.0f;
				frame_count = 0.0f;

				std::snprintf(title, sizeof(title), "VkDraw | FPS: %.0f (%.2fms)", 1000.0f / avg, avg);
				SDL_SetWindowTitle(_window, title);
			}

			while (SDL_PollEvent(&event)) {
				switch (event.type) {
					case SDL_QUIT:
						running = false;
						break;
					case SDL_WINDOWEVENT:
						if (event.window.type == SDL_WINDOWEVENT_RESIZED) {
							_window_resized = true;
						}
					default:
						break;
				}
			}

			draw_frame();
		}

		vkDeviceWaitIdle(_logical_device);

		for (int i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
			vkDestroyFence(_logical_device, _in_flight[i], nullptr);
			vkDestroySemaphore(_logical_device, _render_finished[i], nullptr);
			vkDestroySemaphore(_logical_device, _image_available[i], nullptr);
		}

		vkDestroyCommandPool(_logical_device, _command_pool, nullptr);

		vkDestroyPipeline(_logical_device, _pipeline, nullptr);
		vkDestroyRenderPass(_logical_device, _render_pass, nullptr);
		vkDestroyPipelineLayout(_logical_device, _pipeline_layout, nullptr);

		cleanup_swapchain();

		vkDestroyDevice(_logical_device, nullptr);
		vkDestroySurfaceKHR(_instance, _surface, nullptr);
		vkDestroyInstance(_instance, nullptr);

		SDL_DestroyWindow(_window);
		SDL_Quit();

		return EXIT_SUCCESS;
	}
}
