#pragma once

// Learned advisory gates (issue #121): two 117->32->1 relu/sigmoid MLPs
// trained on replay features. atk5 estimates P(this champion is attacked
// within the next 5 rounds); chan10 estimates P(a channel survives the next
// 10 rounds). Both only veto v21's own rules — they never create actions.
namespace gates {

// atk5: true when a foe is in sight, this champion is under 60% health and
// the gate reads the fight as a loss — disengage and farm instead.
bool veto_fight();

// chan10: false when carrying 5+ gems or under 40% health, a foe is in sight
// and the gate expects the channel to be interrupted — keep moving instead.
bool allow_channel();

} // namespace gates
