// ground.cpp: the Depth Crawler (502), which has no stock AI. Included by crew.h.
#pragma once
namespace crew {
bool IsGroundRobo(const void* vehicle) noexcept;
void GroundFrame(unsigned char* vehicle) noexcept;   // after its stock pre-update, NPC-driven crawlers only
bool CheckGroundProfile() noexcept;
}  // namespace crew
