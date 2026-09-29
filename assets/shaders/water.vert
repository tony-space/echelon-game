#version 410 core

// Procedural water surface of one terrain block: one quad per .vb cell corner
// grid (the original WaterLevel interpolates the per-cell levels bilinearly,
// i.e. they are values at the cell corners). Quads touching a "no water"
// corner collapse to a point and are dropped by the rasteriser.

uniform isampler2D uWater;  // R16I, raw water level per cell, <= -2500 means none
uniform ivec2 uCellOrigin;  // first cell of the block
uniform int uCells;         // cells per block edge
uniform float uCellSize;    // metres per cell
uniform float uHeightScale;

uniform mat4 uView;
uniform mat4 uProj;

out vec3 vWorldPos;
out float vLogZ;

const ivec2 kCorners[6] = ivec2[6](ivec2(0, 0), ivec2(1, 0), ivec2(1, 1), ivec2(0, 0), ivec2(1, 1), ivec2(0, 1));
const int kNoWater = -2500;

int levelAt(ivec2 c)
{
	return texelFetch(uWater, clamp(c, ivec2(0), textureSize(uWater, 0) - 1), 0).r;
}

void main()
{
	int quad = gl_VertexID / 6;
	int corner = gl_VertexID - quad * 6;
	ivec2 q = uCellOrigin + ivec2(quad % uCells, quad / uCells);
	int minLevel = min(min(levelAt(q), levelAt(q + ivec2(1, 0))), min(levelAt(q + ivec2(0, 1)), levelAt(q + ivec2(1, 1))));
	if (minLevel <= kNoWater) {
		vWorldPos = vec3(0.0);
		vLogZ = 1.0;
		gl_Position = vec4(2.0, 2.0, 2.0, 1.0);
		return;
	}

	ivec2 c = q + kCorners[corner];
	vec3 world = vec3(float(c.x) * uCellSize, float(levelAt(c)) * uHeightScale, -float(c.y) * uCellSize);
	vWorldPos = world;
	gl_Position = uProj * uView * vec4(world, 1.0);
	vLogZ = 1.0 + gl_Position.w;
}
