layout (binding = 0) uniform UBO {
	mat4 model;
	mat4 view;
	mat4 proj;
	float time;
} ubo;