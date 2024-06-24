#version 450

#include "ubo.glsl"

layout (binding = 1) uniform sampler2D tex;

layout (location = 0) in vec3 inPosition;
layout (location = 1) in vec3 inColor;
layout (location = 2) in vec2 inTexCoord;
layout (location = 3) in vec3 inNormal;

layout (location = 0) out vec4 outColor;

const float MIN_DIST = 0.001;
const float MAX_DIST = 100.0;

const float ambient = 0.2;
const float specular = 0.5;
const float shininess = 128.0;

const vec3 cameraPos = vec3(0.0, 0.0, 3.0);
const vec3 lightPos = vec3(1.0, -1.0, 1.0);
const vec3 spherePos = vec3(0, 0.0, 0.0);
const vec3 cubePos = vec3(0, 0.0, 0.0);
const vec3 torusPos = vec3(0, 0.0, 0.0);

const float sphereRadius = 1.3;
const vec3 cubeSize = vec3(0.7, 0.7, 0.7);
const vec2 torusSize = vec2(1.0, 0.3);

float sphereSDF(vec3 point, float radius) {
	return length(point) - radius;
}

float cubeSDF(vec3 point, vec3 bounds) {
	vec3 dist = abs(point) - bounds;
	return min(max(dist.x, max(dist.y, dist.z)), 0.0) + length(max(dist, 0.0));
}

float torusSDF(vec3 point, vec2 size) {
	vec2 q = vec2(length(point.xz) - size.x, point.y);
	return length(q) - size.y;
}

float smoothMin(float d1, float d2, float k) {
	float h = clamp(0.5 + 0.5 * (d2 - d1) / k, 0.0, 1.0);
	return mix(d2, d1, h) - k * h * (1.0 - h);
}

float combineFunc(float d1, float d2) {
	//	return min(d1, d2); // union
	//	return max(d1, d2); // intersection
	//	return max(d1, -d2); // difference
	return smoothMin(d1, d2, 0.8); // smooth union
}

float march(vec3 origin, vec3 dir, float time) {
	float dist = 0.0;
	for (int i = 0; i < 100; i++) {
		vec3 point = origin + dir * dist;

		float d1 = sphereSDF(point - spherePos, sphereRadius);
		float d2 = cubeSDF(point - (cubePos + vec3(3.0, 0.0, 0.0) * sin(time)), cubeSize);
		float d3 = torusSDF(point - (torusPos + vec3(0.0, -2.5, 0.0) * cos(time / 2)), torusSize);
		float df = combineFunc(d1, combineFunc(d2, d3));

		dist += df;
		if (df < MIN_DIST) {
			break;
		}
		if (dist > MAX_DIST) {
			dist = MAX_DIST;
			break;
		}
	}
	return dist;
}

vec3 calcNormal(vec3 point, float time) {
	const vec3 eps = vec3(0.0001, 0.0, 0.0);
	vec3 norm = vec3(
		march(point + eps.xyy, vec3(0.0), time) - march(point - eps.xyy, vec3(0.0), time),
		march(point + eps.yxy, vec3(0.0), time) - march(point - eps.yxy, vec3(0.0), time),
		march(point + eps.yyx, vec3(0.0), time) - march(point - eps.yyx, vec3(0.0), time)
	);
	return normalize(norm);
}

void main() {
	// normalized screen coordinates
	vec2 uv = inTexCoord * 2.0 - 1.0;
	uv.x *= 16.0 / 9.0;

	// do raymarching
	vec3 ray = normalize(vec3(uv, -1.0));
	float dist = march(cameraPos, ray, ubo.time);

	// check if hit
	if (dist > MAX_DIST) {
		discard;
	}

	// intersection point and normal
	vec3 point = cameraPos + ray * dist;
	vec3 norm = calcNormal(point, ubo.time);

	// light direction
	vec3 lightDir = normalize((lightPos * 100) - point);

	// diffuse light
	float diffuse = max(dot(norm, lightDir), 0.0);

	// specular light
	vec3 viewDir = normalize(cameraPos - point);
	vec3 reflectDir = reflect(-lightDir, norm);
	float spec = pow(max(dot(viewDir, reflectDir), 0.0), shininess);

	// final color
	vec3 baseColor = norm * 0.5 + 0.5;
	vec3 color = (baseColor * (ambient + diffuse)) + (vec3(1.0) * spec);
	outColor = vec4(color, 1.0);
}