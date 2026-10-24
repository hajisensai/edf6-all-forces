#pragma once
#include <cstdint>
namespace crew::enemy_control {
// Only applied may acknowledge the complete native batch. pending retains its cursor; retries of the same
// actionSequence must never execute already committed native steps again. failed starts full control restoration.
enum class ActionResult : std::uint8_t { applied, pending, failed };
}
