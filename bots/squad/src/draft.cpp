#include <cambc.h>
#include "draft.h"
#include <algorithm>
#include <array>

void play_draft() {
    // Fixed lineup: the first team_size() entries, in this order.
    // Stellar is the carry the team feeds; Cassi & Issac is one pick that gives
    // two champions. 3: Stellar, Cassi & Issac, Ryth   4: + Varg   5: + Tavi
    using cambc::champion_draft;
    const std::array lineup{champion_draft::stellar, champion_draft::cassi_and_issac,
        champion_draft::ryth, champion_draft::varg, champion_draft::tavi};

    const auto size = std::min<std::size_t>(cambc::draft::team_size(), lineup.size());
    const auto chosen = cambc::draft::current_draft(cambc::team::ally);
    for (std::size_t i = 0; i < size; ++i)
        if (std::find(chosen.begin(), chosen.end(), lineup[i]) == chosen.end()) {
            cambc::draft::pick(lineup[i]);
            return;
        }
}
