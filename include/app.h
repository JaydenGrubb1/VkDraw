#pragma once

#include <span>
#include <string_view>
#include <cstdlib>

namespace VkDraw {
	int run(std::span<std::string_view> args);
}
