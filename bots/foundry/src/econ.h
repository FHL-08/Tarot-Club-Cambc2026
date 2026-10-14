#pragma once
// The spending brain (docs/strategy.md §Architecture — econ.cpp).
// Per-champion build queues, consumable policy, and the tribute bag-cap.
// Passive layer only: the FSM calls maybe_buy on shopping turns and
// consults should_tribute before diverting a carrier to an altar.
// Role comes from plan.h — the caller resolves the champion's slot role.
#include "obs.h"
#include "plan.h"

namespace econ {

// Run the champion's build queue plus consumable top-up. Legal whenever the
// engine allows shopping — spawn tiles or dead turns both count, so callers
// invoke this even on turns where the champion cannot act otherwise.
// Consumable policy is route-aware: a slot whose compiled route passes an
// enemy-side altar stocks its lock ahead of shard gold (issue #24).
// Pre-builds econ's static caches; safe anytime, idempotent.
void warm();

void maybe_buy(Role role, const Obs& obs);

// True when the carried bag should be banked now: at/above the role-scaled
// bag cap, or when remaining rounds no longer fit channel + walk time.
bool should_tribute(const Obs& obs);

} // namespace econ
