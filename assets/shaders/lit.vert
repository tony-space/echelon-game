#version 410 core

layout(location = 0) in vec3 aPosition;
layout(location = 1) in vec3 aNormal;
layout(location = 2) in vec2 aUv;

uniform mat4 uModel;
uniform mat4 uView;
uniform mat4 uProj;

out vec3 vWorldPos;
out vec3 vNormal;
out vec2 vUv;

void main()
{
	vec4 world = uModel * vec4(aPosition, 1.0);
	vWorldPos = world.xyz;
	vNormal = mat3(uModel) * aNormal;
	vUv = aUv;
	gl_Position = uProj * uView * world;
}
