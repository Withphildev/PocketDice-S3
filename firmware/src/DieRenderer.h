#pragma once

#include <cstdint>

namespace pocketdice {

// Lightweight polyhedral renderer for the 240x135 StickS3 display. It uses
// the same camera, fixed-light, back-face-culling, and result-facing rules as
// the WebUI without requiring a framebuffer or depth buffer.
void drawDie(std::uint16_t sides, std::uint16_t value, bool settled, int frame = 0);

}  // namespace pocketdice
