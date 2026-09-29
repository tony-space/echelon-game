#version 410 core

layout(location = 0) in vec3 aPosition;
layout(location = 1) in vec3 aNormal;
layout(location = 2) in vec2 aUv;

uniform mat4 uModel;
uniform mat4 uView;
uniform mat4 uProj;
// Logarithmic depth is written per fragment (gl_FragDepth, see lit.frag) from
// vLogZ; clipping keeps the regular projection so triangles crossing the near
// plane are cut correctly. terrain.* and water.* use the same mapping.
out vec3 vWorldPos;
out vec3 vNormal;
out vec2 vUv;
out float vLogZ;

void main()
{
	vec4 world = uModel * vec4(aPosition, 1.0);
	vWorldPos = world.xyz;
	vNormal = mat3(uModel) * aNormal;
	vUv = aUv;
	gl_Position = uProj * uView * world;
	vLogZ = 1.0 + gl_Position.w;
}
