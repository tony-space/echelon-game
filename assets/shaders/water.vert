#version 410 core

// Procedural water surface of one terrain block: one quad per .vb cell corner
// grid (the original WaterLevel interpolates the per-cell levels bilinearly,
// i.e. they are values at the cell corners). Quads touching a "no water"
// corner collapse to a point and are dropped by the rasteriser.

uniform isampler2D uWater;  // R16I, raw water level per cell, <= -2500 means none
uniform ivec2 uCellOrigin;  // first cell of the node
uniform ivec2 uCells;       // quads per axis
uniform int uCellStep;      // cells per quad edge (coarser far away)
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
	ivec2 q = uCellOrigin + ivec2(quad % uCells.x, quad / uCells.x) * uCellStep;
	ivec2 limit = textureSize(uWater, 0) - 1;
	ivec2 q1 = min(q + ivec2(uCellStep), limit);
	int minLevel = min(min(levelAt(q), levelAt(ivec2(q1.x, q.y))), min(levelAt(ivec2(q.x, q1.y)), levelAt(q1)));
	if (minLevel <= kNoWater) {
		vWorldPos = vec3(0.0);
		vLogZ = 1.0;
		gl_Position = vec4(2.0, 2.0, 2.0, 1.0);
		return;
	}

	ivec2 c = min(q + kCorners[corner] * uCellStep, limit);
	vec3 world = vec3(float(c.x) * uCellSize, float(levelAt(c)) * uHeightScale, -float(c.y) * uCellSize);
	vWorldPos = world;
	gl_Position = uProj * uView * vec4(world, 1.0);
	vLogZ = 1.0 + gl_Position.w;
}
