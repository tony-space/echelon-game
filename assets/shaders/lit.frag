#version 410 core

// All lighting is in linear RGB. uAlbedo is an sRGB texture (decoded by the
// sampler), the colour uniforms below are linearised on the CPU, and the back
// buffer re-encodes to sRGB on write (GL_FRAMEBUFFER_SRGB).

in vec3 vWorldPos;
in vec3 vNormal;
in vec2 vUv;
in float vLogZ;

uniform float uLogDepth;
uniform sampler2D uAlbedo;
uniform vec3 uSunDir;    // normalized, towards the sun
uniform vec3 uCameraPos;
uniform vec3 uTint;
uniform float uAlpha;    // 1.0 for opaque geometry; glass uses the material's diffuse alpha

uniform vec3 uSunColor;       // linear radiance of direct sunlight
uniform vec3 uSkyAmbient;     // linear, hemisphere light from above
uniform vec3 uGroundAmbient;  // linear, bounce light from below
uniform vec3 uFogColor;       // linear in-scattered light (sky at the horizon)
uniform float uFogExtinction; // 1/m at sea level
uniform float uFogScaleHeight; // m, extinction falls off as exp(-y / H)

out vec4 fragColor;

// Beer-Lambert through an exponential atmosphere, integrated along the view
// ray. Same as ech::heightFogTransmittance (color.hpp); keep in sync with
// terrain.frag and water.frag.
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
	vec3 n = normalize(vNormal);
	vec3 l = normalize(uSunDir);
	vec3 v = normalize(uCameraPos - vWorldPos);
	vec3 h = normalize(l + v);

	vec3 albedo = texture(uAlbedo, vUv).rgb * uTint;

	float ndl = max(dot(n, l), 0.0);
	vec3 ambient = mix(uGroundAmbient, uSkyAmbient, n.y * 0.5 + 0.5);
	vec3 sun = uSunColor * ndl;
	float spec = pow(max(dot(n, h), 0.0), 48.0) * 0.25 * step(0.0, ndl);

	vec3 color = albedo * (ambient + sun) + uSunColor * spec;

	// Homogeneous fog: the surface radiance is attenuated by the transmittance
	// and the same fraction of sky light is scattered in along the ray.
	color = mix(uFogColor, color, fogTransmittance(uCameraPos, vWorldPos));

	// Highlights stay visible on glass even where it is mostly transparent.
	fragColor = vec4(color, clamp(uAlpha + spec, 0.0, 1.0));
	gl_FragDepth = log2(vLogZ) * uLogDepth * 0.5;
}
