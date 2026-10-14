"""Map-aware champion drafting."""
import cambc
from cambc import game_map

RYTH = 0
VESPER_VALE = 1
VENDETTA = 2
CASSI_AND_ISSAC = 3
STELLAR = 4
PRISM = 5
MARCELLA = 6
WILLOW = 7
REN = 8
VARG = 9
TAVI = 10

# Baseline scores. Higher is better.
BASE_SCORES = {
    STELLAR: 10,
    VESPER_VALE: 9,
    VENDETTA: 8,
    VARG: 7,
    TAVI: 6,
    PRISM: 5,
    RYTH: 4,
    MARCELLA: 3,
    WILLOW: 2,
    REN: 1,
    CASSI_AND_ISSAC: 0,
}


def map_features():
    """Extract features using map methods used by the main bot."""
    groups = list(game_map.camp_groups())
    altars = game_map.altars().positions()

    camp_tiles = []
    for group in groups:
        camp_tiles.extend(group.spawn_tiles().positions())

    # Count camp spawn tiles clustered close together.
    clustered_pairs = 0
    for i, a in enumerate(camp_tiles):
        for b in camp_tiles[i + 1:]:
            if a.chebyshev_distance(b) <= 2:
                clustered_pairs += 1

    # Measure how close camp spawn tiles are to altar locations.
    nearest_altar_distances = []
    for camp in camp_tiles:
        if altars:
            nearest_altar_distances.append(
                min(camp.chebyshev_distance(altar) for altar in altars)
            )

    average_altar_distance = (
        sum(nearest_altar_distances) / len(nearest_altar_distances)
        if nearest_altar_distances else 0
    )

    return {
        "camp_groups": len(groups),
        "camp_tiles": len(camp_tiles),
        "altars": len(altars),
        "clustered_pairs": clustered_pairs,
        "average_altar_distance": average_altar_distance,
    }


def score_champions(features):
    scores = dict(BASE_SCORES)

    # Camp-focused strategy: favour economy and monster clearing.
    if features["camp_groups"] >= 4:
        scores[VESPER_VALE] += 3
        scores[VENDETTA] += 2
        scores[VARG] += 1

    # Clustered camp spawns make area damage potentially more valuable.
    if features["clustered_pairs"] >= 3:
        scores[VARG] += 4

    # A larger camp network favours sustained PvE and economy.
    if features["camp_tiles"] >= 10:
        scores[VESPER_VALE] += 2
        scores[VENDETTA] += 2

    # Widely separated camps and altars can reward flexibility.
    if features["average_altar_distance"] >= 5:
        scores[TAVI] += 3
        scores[STELLAR] += 1

    # Multiple altars create more potential deposit routes and objectives.
    if features["altars"] >= 3:
        scores[TAVI] += 2
        scores[STELLAR] += 1

    return scores


def play():
    chosen = set(cambc.draft.current_draft(cambc.Team.ALLY))
    available = set(range(11)) - chosen

    if not available:
        return

    # Keep a baseline fallback in case map queries are unavailable.
    try:
        features = map_features()
        scores = score_champions(features)
    except (AttributeError, TypeError, RuntimeError):
        scores = BASE_SCORES

    # Stable tie-breaking by champion ID.
    pick = max(
        available,
        key=lambda champion: (scores[champion], -champion),
    )

    cambc.draft_memory.submit(
        b"FARM: prioritise camps, upgrade efficiently, "
        b"and deposit carried gems safely"
    )
    cambc.draft.pick(pick)