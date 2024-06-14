#include <cstdio>
#include <vector>

#include "app.h"

int main(const int argc, char **argv) {
	std::vector<std::string_view> args(argv, argv + argc);
	int res = EXIT_SUCCESS;

	try {
		res = VkDraw::run(args);
	} catch (std::exception &e) {
		std::fflush(stdout);
		std::fprintf(stderr, "Unhandled exception: %s\n", e.what());
		res = EXIT_FAILURE;
	}

	return res;
}
