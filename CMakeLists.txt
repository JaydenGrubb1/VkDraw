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
	#-Wold-style-cast
)

add_executable(
	${PROJECT_NAME}
	src/main.cpp
	src/app.cpp
)

set(
	SHADER_SRC
	shaders/shader.frag
	shaders/shader.vert
)

find_package(SDL2 REQUIRED)
find_package(SDL2_image REQUIRED)
find_package(Vulkan REQUIRED)

include_directories(
	${CMAKE_CURRENT_SOURCE_DIR}/include
	${SDL2_INCLUDE_DIRS}
	${SDL2_IMAGE_INCLUDE_DIRS}
	${Vulkan_INCLUDE_DIRS}
)

target_link_libraries(
	${PROJECT_NAME}
	SDL2::SDL2
	SDL2_image::SDL2_image
	Vulkan::Vulkan
)

find_program(GLSLC glslc REQUIRED HINTS Vulkan::glslc)

foreach (shader ${SHADER_SRC})
	get_filename_component(shader_name ${shader} NAME)
	set(shader_spirv ${CMAKE_CURRENT_BINARY_DIR}/shaders/${shader_name}.spv)
	add_custom_command(
		OUTPUT ${shader_spirv}
		COMMAND ${GLSLC} ${CMAKE_CURRENT_SOURCE_DIR}/${shader} -o ${shader_spirv}
		DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/${shader}
	)
	list(APPEND SHADER_SPIRV ${shader_spirv})
endforeach ()

add_custom_target(shaders ALL DEPENDS ${SHADER_SPIRV})