#pragma once

#include <span>
#include <string_view>
#include <cstdlib>

namespace VkDraw {
	enum class Result {
		SUCCESS = EXIT_SUCCESS,
		FAILURE = EXIT_FAILURE
	};

	Result run(std::span<std::string_view> args);
}
