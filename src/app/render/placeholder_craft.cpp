#include "render/placeholder_craft.hpp"

namespace ech {

MeshData makePlaceholderCraft()
{
	MeshData m;
	// Fuselage: long box, nose at -Z.
	m.addBox(glm::vec3(0.0f, 0.0f, 0.0f), glm::vec3(0.8f, 0.7f, 6.0f));
	// Nose cone stand-in.
	m.addBox(glm::vec3(0.0f, -0.1f, -6.8f), glm::vec3(0.45f, 0.4f, 0.9f));
	// Cockpit bump.
	m.addBox(glm::vec3(0.0f, 0.9f, -2.5f), glm::vec3(0.5f, 0.35f, 1.4f));
	// Wings, swept back: main slab plus tips.
	m.addBox(glm::vec3(0.0f, -0.1f, 1.0f), glm::vec3(5.5f, 0.12f, 1.6f));
	m.addBox(glm::vec3(-5.2f, -0.1f, 1.6f), glm::vec3(0.9f, 0.25f, 1.3f));
	m.addBox(glm::vec3(5.2f, -0.1f, 1.6f), glm::vec3(0.9f, 0.25f, 1.3f));
	// Twin engines under the wing roots (LE / RE in the original hull tree).
	m.addBox(glm::vec3(-1.7f, -0.35f, 2.5f), glm::vec3(0.5f, 0.5f, 2.2f));
	m.addBox(glm::vec3(1.7f, -0.35f, 2.5f), glm::vec3(0.5f, 0.5f, 2.2f));
	// Vertical tails.
	m.addBox(glm::vec3(-1.2f, 1.1f, 5.0f), glm::vec3(0.08f, 1.0f, 1.0f));
	m.addBox(glm::vec3(1.2f, 1.1f, 5.0f), glm::vec3(0.08f, 1.0f, 1.0f));
	return m;
}

MeshData makeGroundPlane(float size, float tileMeters)
{
	MeshData m;
	const float h = size * 0.5f;
	const float tiles = size / tileMeters;
	m.addQuad(glm::vec3(-h, 0.0f, h), glm::vec3(h, 0.0f, h), glm::vec3(h, 0.0f, -h), glm::vec3(-h, 0.0f, -h),
		glm::vec2(tiles));
	return m;
}

} // namespace ech
