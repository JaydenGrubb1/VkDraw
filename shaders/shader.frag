#version 450

layout (binding = 1) uniform sampler2D tex;

layout (location = 0) in vec3 inPosition;
layout (location = 1) in vec3 inColor;
layout (location = 2) in vec2 inTexCoord;
layout (location = 3) in vec3 inNormal;

layout (location = 0) out vec4 outColor;

vec3 lightPos = vec3(5.0, -2.0, 5.0);
vec3 lightColor = vec3(1.0, 1.0, 0.9);

float ambientStrength = 0.03;
vec3 ambientColor = vec3(1.0, 0.854, 1.0);

vec3 cameraPos = vec3(0.0, -1.0, 3.0);
vec3 specularColor = vec3(1.0, 1.0, 1.0);
float specularStrength = 1.0;
float shininess = 32.0;

void main() {
	// Get texture color and normal
	vec3 baseColor = texture(tex, inTexCoord).rgb;
	vec3 normalTex = texture(tex, inTexCoord + vec2(0.0, 0.5)).rgb;
	vec3 normal = normalize(inNormal + normalTex);
	vec3 lightDir = normalize((lightPos * 1000) - inPosition);
	// lightPos is scaled by a large number to make it a directional light
	// i.e. the light is infinitely far away
	// i.e. the entire face is illuminated by the same amount

	// Ambient light
	vec3 ambient = ambientStrength * ambientColor;

	// Diffuse light
	float diff = max(dot(normal, lightDir), 0.0);
	vec3 diffuse = diff * lightColor;

	// Specular light
	vec3 viewDir = normalize(cameraPos - inPosition);
	vec3 reflectDir = reflect(-lightDir, normal);
	float spec = pow(max(dot(viewDir, reflectDir), 0.0), shininess);
	vec3 specular = specularStrength * spec * specularColor;

	// Combine all the lights
	vec3 result = (ambient + diffuse + specular) * baseColor;
	outColor = vec4(result, 1.0);
}