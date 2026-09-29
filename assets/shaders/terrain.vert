#version 410 core

// Procedural terrain block: no vertex buffers. Each quad of the block is six
// vertices of gl_VertexID; the position comes from the height texture and the
// quad is split along the diagonal stored in the original .sq flags.

uniform isampler2D uHeights; // R16I, raw heights, texel (i, j) = sample (i, j)
uniform usampler2D uInfo;    // R8UI, bits 0-4 ground layer, bit 7 diagonal (i,j)-(i+1,j+1)
uniform ivec2 uOrigin;       // first sample of the block
uniform int uStep;           // samples per quad edge (LOD)
uniform int uQuads;          // quads per block edge
uniform ivec4 uEdgeStep;     // quad step of the neighbour at -i, +i, -j, +j (>= uStep)
uniform float uSpacing;      // metres between samples
uniform float uHeightScale;  // raw -> metres

uniform mat4 uView;
uniform mat4 uProj;

out vec3 vWorldPos;
out vec3 vNormal;
out float vLogZ;

const ivec2 kSplit00to11[6] = ivec2[6](ivec2(0, 0), ivec2(1, 0), ivec2(1, 1), ivec2(0, 0), ivec2(1, 1), ivec2(0, 1));
const ivec2 kSplit10to01[6] = ivec2[6](ivec2(0, 0), ivec2(1, 0), ivec2(0, 1), ivec2(1, 0), ivec2(1, 1), ivec2(0, 1));

float heightAt(ivec2 s)
{
	s = clamp(s, ivec2(0), textureSize(uHeights, 0) - 1);
	return float(texelFetch(uHeights, s, 0).r) * uHeightScale;
}

// Vertices on an edge shared with a coarser block follow the neighbour's
// straight edge between its own vertices, so the two meshes meet without cracks.
float stitchedHeight(ivec2 local)
{
	int extent = uQuads * uStep;
	int coarse = 0;
	int along = 0;
	ivec2 dir = ivec2(0);
	if (local.x == 0 && uEdgeStep.x > uStep) {
		coarse = uEdgeStep.x; along = local.y; dir = ivec2(0, 1);
	} else if (local.x == extent && uEdgeStep.y > uStep) {
		coarse = uEdgeStep.y; along = local.y; dir = ivec2(0, 1);
	} else if (local.y == 0 && uEdgeStep.z > uStep) {
		coarse = uEdgeStep.z; along = local.x; dir = ivec2(1, 0);
	} else if (local.y == extent && uEdgeStep.w > uStep) {
		coarse = uEdgeStep.w; along = local.x; dir = ivec2(1, 0);
	}
	ivec2 s = uOrigin + local;
	if (coarse == 0)
		return heightAt(s);
	int rest = along % coarse;
	ivec2 base = s - dir * rest;
	return mix(heightAt(base), heightAt(base + dir * coarse), float(rest) / float(coarse));
}

void main()
{
	int quad = gl_VertexID / 6;
	int corner = gl_VertexID - quad * 6;
	ivec2 q = ivec2(quad % uQuads, quad / uQuads) * uStep;
	bool diagonal = (texelFetch(uInfo, clamp(uOrigin + q, ivec2(0), textureSize(uInfo, 0) - 1), 0).r & 128u) != 0u;
	ivec2 local = q + (diagonal ? kSplit00to11[corner] : kSplit10to01[corner]) * uStep;
	ivec2 s = uOrigin + local;

	float h = stitchedHeight(local);
	vec3 world = vec3(float(s.x) * uSpacing, h, -float(s.y) * uSpacing);

	// Central differences at the LOD spacing; j grows towards -Z.
	float d = float(uStep) * uSpacing;
	float dhdx = (heightAt(s + ivec2(uStep, 0)) - heightAt(s - ivec2(uStep, 0))) / (2.0 * d);
	float dhdz = -(heightAt(s + ivec2(0, uStep)) - heightAt(s - ivec2(0, uStep))) / (2.0 * d);
	vNormal = normalize(vec3(-dhdx, 1.0, -dhdz));

	vWorldPos = world;
	gl_Position = uProj * uView * vec4(world, 1.0);
	vLogZ = 1.0 + gl_Position.w;
}
