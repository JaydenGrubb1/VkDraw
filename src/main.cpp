#include <vector>

#include "app.h"

int main(const int argc, char** argv) {
	std::vector<std::string_view> args(argv, argv + argc);
	return static_cast<int>(VkDraw::run(args));
}
