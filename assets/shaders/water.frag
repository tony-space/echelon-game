#version 410 core

// Opaque water: the original layer texture for colour, Schlick-Fresnel sky
// reflection and a sun glint. Linear RGB like everything else.

in vec3 vWorldPos;
in float vLogZ;

uniform sampler2D uWaterTex;  // sRGB
uniform float uTilePeriod;

uniform vec3 uSunDir;
uniform vec3 uCameraPos;
uniform vec3 uSunColor;
uniform vec3 uSkyAmbient;
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

void main()
{
	vec2 ground = vec2(vWorldPos.x, -vWorldPos.z);
	vec3 albedo = texture(uWaterTex, ground / uTilePeriod).rgb;

	vec3 n = vec3(0.0, 1.0, 0.0);
	vec3 l = normalize(uSunDir);
	vec3 toEye = uCameraPos - vWorldPos;
	float dist = length(toEye);
	vec3 v = toEye / max(dist, 1e-3);
	vec3 h = normalize(l + v);

	float ndl = max(dot(n, l), 0.0);
	vec3 body = albedo * (uSkyAmbient + uSunColor * ndl) * 0.6;
	float fresnel = 0.02 + 0.98 * pow(1.0 - max(dot(n, v), 0.0), 5.0);
	vec3 color = mix(body, uFogColor, fresnel) + uSunColor * pow(max(dot(n, h), 0.0), 400.0) * 2.0;

	fragColor = vec4(mix(uFogColor, color, fogTransmittance(uCameraPos, vWorldPos)), 1.0);
	gl_FragDepth = log2(vLogZ) * uLogDepth * 0.5;
}
