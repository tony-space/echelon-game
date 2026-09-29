#pragma once

#include <glm/vec3.hpp>

namespace ech {

// The original game stores two coordinate frames. Both are converted once, at
// export time (tools/legacy_export.py mirrors these two functions); the engine
// never flips axes at runtime. Spec is here so loaders and tests cannot drift.

// mesh.dat vertices: right-handed, +X right, -Y up, nose at +Z. That is the
// engine frame (right-handed, +Y up, nose at -Z) rotated 180 degrees about X,
// so this is a rotation, not a mirror: triangle winding is unchanged.
inline glm::vec3 meshToEngine(const glm::vec3& v)
{
	return glm::vec3(v.x, -v.y, -v.z);
}

// Everything else (objects.dat node positions, gdata.dat ViewDelta / hardpoints):
// classic left-handed Direct3D, +X right, +Y up, nose at +Z. Only Z flips.
inline glm::vec3 d3dToEngine(const glm::vec3& v)
{
	return glm::vec3(v.x, v.y, -v.z);
}

} // namespace ech
