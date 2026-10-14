"""Map-aware champion drafting."""
import cambc
from cambc import game_map

def play():
    chosen = set(cambc.draft.current_draft(cambc.Team.ALLY))

    for champion in (
        cambc.ChampionDraft.RYTH,
        cambc.ChampionDraft.VENDETTA,
        cambc.ChampionDraft.VARG,
        cambc.ChampionDraft.STELLAR,
        cambc.ChampionDraft.PRISM,
    ):
        if champion not in chosen:
            cambc.draft.pick(champion)
            return 