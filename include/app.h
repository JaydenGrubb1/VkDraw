#pragma once

#include <cstdlib>
#include <span>
#include <string_view>

namespace VkDraw {
	int run(std::span<std::string_view> args);
}
