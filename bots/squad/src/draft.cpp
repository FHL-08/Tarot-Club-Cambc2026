#include <cambc.h>
#include "draft.h"
#include <algorithm>
#include <array>

namespace {

// Camp groups one champion clears safely alone: gem/gold clusters and lone
// wolves. Varg farms these solo; on maps without them (elkring) Prism is the
// better fifth pick.
int soloable_groups() {
    int count = 0;
    for (const auto& g : cambc::map::camp_groups()) {
        if (g.gem_value() == 0)
            continue;
        int wolves = 0;
        bool heavy = false;
        for (const auto& c : g.camps()) {
            wolves += c.is_wolf();
            heavy |= c.is_elk() || c.is_monolith();
        }
        count += !heavy && wolves <= 1;
    }
    return count;
}

} // namespace

void play_draft() {
    // Fixed lineup: the first team_size() entries, in this order, and nothing else.
    // Cassi & Issac is one pick that gives two champions.
    // 3: Ryth, Cassi & Issac, Willow   4: + Tavi   5: + Varg (solo farmer) or Prism
    // Willow is the top-5 comp's support half; Tavi stays for the ranged
    // backbone — dropping both dealers collapsed champDmg ~40% (#113).
    using cambc::champion_draft;
    const auto size = std::min<std::size_t>(cambc::draft::team_size(), 5);
    const auto fifth = size >= 5 && soloable_groups() >= 4 ? champion_draft::varg : champion_draft::prism;
    const std::array lineup{champion_draft::ryth, champion_draft::cassi_and_issac,
        champion_draft::willow, champion_draft::tavi, fifth};

    const auto chosen = cambc::draft::current_draft(cambc::team::ally);
    for (std::size_t i = 0; i < size; ++i)
        if (std::find(chosen.begin(), chosen.end(), lineup[i]) == chosen.end()) {
            cambc::draft::pick(lineup[i]);
            return;
        }
}
