#pragma once

#include "render/mesh.hpp"

namespace ech {

// Blocky stand-in for a BF-class fighter, roughly 12 m long, nose towards -Z.
// Replaced by a real model once we have one.
MeshData makePlaceholderCraft();

// Flat ground plane centred at the origin, `size` metres across, UVs tiled
// every `tileMeters`.
MeshData makeGroundPlane(float size, float tileMeters);

} // namespace ech
