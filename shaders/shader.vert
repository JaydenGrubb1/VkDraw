#version 450

#include "ubo.glsl"

layout (location = 0) in vec3 inPosition;
layout (location = 1) in vec3 inColor;
layout (location = 2) in vec2 inTexCoord;
layout (location = 3) in vec3 inNormal;

layout (location = 0) out vec3 outPosition;
layout (location = 1) out vec3 outColor;
layout (location = 2) out vec2 outTexCoord;
layout (location = 3) out vec3 outNormal;

void main() {
	//	gl_Position = ubo.proj * ubo.view * ubo.model * vec4(inPosition, 1.0);
	//	outPosition = gl_Position.xyz;
	//	outColor = inColor;
	//	outTexCoord = inTexCoord;
	//	mat3 normalMatrix = transpose(inverse(mat3(ubo.model))); // TODO: pre-compute this on the CPU
	//	outNormal = normalize(normalMatrix * inNormal);

	gl_Position = vec4(inPosition, 1.0);
	outTexCoord = inTexCoord;
}