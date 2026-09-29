#version 410 core

// Ground texturing: every height sample carries one of the 32 original layer
// textures; a fragment blends the layers of the four surrounding samples
// bilinearly. Lighting and fog as in lit.frag, without specular.

in vec3 vWorldPos;
in vec3 vNormal;
in float vLogZ;

uniform usampler2D uInfo;         // R8UI, bits 0-4 ground layer
uniform sampler2DArray uLayers;   // sRGB layer textures
uniform sampler2D uDetail;        // sRGB detail texture, modulates close up
uniform vec3 uDetailMean;         // linear mean of uDetail, keeps the modulation neutral on average
uniform float uSpacing;
uniform float uTilePeriod;        // metres per layer texture repeat
uniform float uDetailPeriod;      // metres per detail texture repeat
uniform float uDetailFade;        // metres, detail strength falls off as exp(-dist / fade)
uniform float uDetailStrength;    // 0 = off, 1 = full modulation next to the camera

uniform vec3 uSunDir;
uniform vec3 uCameraPos;
uniform vec3 uSunColor;
uniform vec3 uSkyAmbient;
uniform vec3 uGroundAmbient;
uniform vec3 uFogColor;
uniform float uFogExtinction;
uniform float uFogScaleHeight;
uniform float uLogDepth;

out vec4 fragColor;

// See lit.frag.
float fogTransmittance(vec3 eye, vec3 p)
{
	float k = 1.0 / uFogScaleHeight;
	float dy = (p.y - eye.y) * k;
	float e0 = exp(-eye.y * k);
	float meanDensity = abs(dy) < 1e-4 ? e0 : (e0 - exp(-p.y * k)) / dy;
	return exp(-uFogExtinction * length(p - eye) * meanDensity);
}

uint layerAt(ivec2 s)
{
	return texelFetch(uInfo, clamp(s, ivec2(0), textureSize(uInfo, 0) - 1), 0).r & 31u;
}

void main()
{
	vec2 ground = vec2(vWorldPos.x, -vWorldPos.z);
	vec2 g = ground / uSpacing;
	ivec2 s = ivec2(floor(g));
	vec2 f = g - vec2(s);

	uint l00 = layerAt(s);
	uint l10 = layerAt(s + ivec2(1, 0));
	uint l01 = layerAt(s + ivec2(0, 1));
	uint l11 = layerAt(s + ivec2(1, 1));

	// Gradients taken outside the branch: implicit derivatives are undefined
	// in non-uniform control flow.
	vec2 uv = ground / uTilePeriod;
	vec2 dx = dFdx(uv), dy = dFdy(uv);
	vec3 albedo;
	if (l00 == l10 && l00 == l01 && l00 == l11) {
		albedo = textureGrad(uLayers, vec3(uv, float(l00)), dx, dy).rgb;
	} else {
		vec3 c00 = textureGrad(uLayers, vec3(uv, float(l00)), dx, dy).rgb;
		vec3 c10 = textureGrad(uLayers, vec3(uv, float(l10)), dx, dy).rgb;
		vec3 c01 = textureGrad(uLayers, vec3(uv, float(l01)), dx, dy).rgb;
		vec3 c11 = textureGrad(uLayers, vec3(uv, float(l11)), dx, dy).rgb;
		albedo = mix(mix(c00, c10, f.x), mix(c01, c11, f.x), f.y);
	}

	float dist = length(uCameraPos - vWorldPos);
	vec3 detail = texture(uDetail, ground / uDetailPeriod).rgb / uDetailMean;
	albedo *= mix(vec3(1.0), detail, uDetailStrength * exp(-dist / uDetailFade));

	vec3 n = normalize(vNormal);
	float ndl = max(dot(n, normalize(uSunDir)), 0.0);
	vec3 ambient = mix(uGroundAmbient, uSkyAmbient, n.y * 0.5 + 0.5);
	vec3 color = albedo * (ambient + uSunColor * ndl);

	fragColor = vec4(mix(uFogColor, color, fogTransmittance(uCameraPos, vWorldPos)), 1.0);
	gl_FragDepth = log2(vLogZ) * uLogDepth * 0.5;
}
