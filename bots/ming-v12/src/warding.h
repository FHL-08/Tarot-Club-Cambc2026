#pragma once

struct Obs;

namespace warding {

// On a safe, well-funded turn, leave one lookout over an altar approach.
// This never chooses a movement, combat, farming, banking, or draft action.
void maybe_place(const Obs& obs);

} // namespace warding
