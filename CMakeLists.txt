cmake_minimum_required(VERSION 3.20)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON CACHE INTERNAL "")

project(VkDraw)
enable_language(CXX)
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_compile_options(
	-Wall
	-Wextra
	-Wpedantic
	#	-Wold-style-cast
)

add_executable(
	${PROJECT_NAME}
	src/main.cpp
	src/app.cpp
)

find_package(SDL2 REQUIRED)
find_package(Vulkan REQUIRED)

include_directories(
	${CMAKE_CURRENT_SOURCE_DIR}/include
	${SDL2_INCLUDE_DIRS}
	${Vulkan_INCLUDE_DIRS}
)

target_link_libraries(
	${PROJECT_NAME}
	${SDL2_LIBRARIES}
	${Vulkan_LIBRARIES}
)