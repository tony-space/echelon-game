#version 410 core

// Procedural terrain node: no vertex buffers. Each quad of the node is six
// vertices of gl_VertexID; the position comes from the height texture and the
// quad is split along the diagonal stored in the original .sq flags.
//
// LOD morphing (CDLOD): step S is used for ground between uLodDistance * S / 2
// and uLodDistance * S from the camera. Over the outer part of that
// range every vertex the step-2S grid does not have slides from its own height
// onto the step-2S surface (the midpoint of the coarse edge or diagonal it
// lies on), so at the switch the two grids coincide and nothing pops. The
// morph factor depends only on the vertex position and the step, so nodes of
// different steps meet exactly and need no stitching.

uniform isampler2D uHeights; // R16I, raw heights, texel (i, j) = sample (i, j)
uniform usampler2D uInfo;    // R8UI, bits 0-4 ground layer, bit 7 diagonal (i,j)-(i+1,j+1)
uniform ivec2 uOrigin;       // first sample of the node
uniform int uStep;           // samples per quad edge (LOD)
uniform ivec2 uQuads;        // quads per axis
uniform int uMorph;          // 0 for the coarsest level (nothing to morph into)
uniform float uLodDistance;  // range of step 1, metres
uniform float uMorphStart;   // fraction of the range where morphing begins
uniform float uSpacing;      // metres between samples
uniform float uHeightScale;  // raw -> metres
uniform vec3 uCameraPos;

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

bool diagonalAt(ivec2 s)
{
	s = clamp(s, ivec2(0), textureSize(uInfo, 0) - 1);
	return (texelFetch(uInfo, s, 0).r & 128u) != 0u;
}

// Height of the step-2S surface at a step-S vertex.
float coarseHeight(ivec2 s, int step)
{
	ivec2 odd = (s / step) & 1;
	if (odd.x == 1 && odd.y == 1) {
		ivec2 s0 = s - ivec2(step);
		int two = 2 * step;
		return diagonalAt(s0) ? 0.5 * (heightAt(s0) + heightAt(s0 + ivec2(two)))
							  : 0.5 * (heightAt(s0 + ivec2(two, 0)) + heightAt(s0 + ivec2(0, two)));
	}
	if (odd.x == 1)
		return 0.5 * (heightAt(s - ivec2(step, 0)) + heightAt(s + ivec2(step, 0)));
	if (odd.y == 1)
		return 0.5 * (heightAt(s - ivec2(0, step)) + heightAt(s + ivec2(0, step)));
	return heightAt(s);
}

vec3 normalAt(ivec2 s, int step)
{
	float d = float(step) * uSpacing;
	float dhdx = (heightAt(s + ivec2(step, 0)) - heightAt(s - ivec2(step, 0))) / (2.0 * d);
	float dhdz = -(heightAt(s + ivec2(0, step)) - heightAt(s - ivec2(0, step))) / (2.0 * d); // j grows towards -Z
	return normalize(vec3(-dhdx, 1.0, -dhdz));
}

void main()
{
	int quad = gl_VertexID / 6;
	int corner = gl_VertexID - quad * 6;
	ivec2 q = ivec2(quad % uQuads.x, quad / uQuads.x) * uStep;
	bool diagonal = diagonalAt(uOrigin + q);
	ivec2 s = uOrigin + q + (diagonal ? kSplit00to11[corner] : kSplit10to01[corner]) * uStep;
	// Nodes clipped by the map edge end on the last sample.
	s = min(s, textureSize(uHeights, 0) - 1);

	float h = heightAt(s);
	vec3 world = vec3(float(s.x) * uSpacing, h, -float(s.y) * uSpacing);
	vec3 n = normalAt(s, uStep);

	if (uMorph != 0) {
		float rangeEnd = uLodDistance * float(uStep);
		float morphBegin = uMorphStart * rangeEnd;
		float t = clamp((distance(uCameraPos, world) - morphBegin) / (rangeEnd - morphBegin), 0.0, 1.0);
		if (t > 0.0) {
			world.y = mix(h, coarseHeight(s, uStep), t);
			n = normalize(mix(n, normalAt(s, 2 * uStep), t));
		}
	}

	vNormal = n;
	vWorldPos = world;
	gl_Position = uProj * uView * vec4(world, 1.0);
	vLogZ = 1.0 + gl_Position.w;
}
