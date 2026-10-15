import cambc
from randomness import below

# draft mappings 
D_RYTH = 0
D_VESPER_VALE = 1
D_VENDETTA = 2
D_CASSI_AND_ISSAC = 3
D_STELLAR = 4
D_PRISM = 5
D_MARCELLA = 6
D_WILLOW = 7
D_REN = 8
D_VARG = 9
D_TAVI = 10

# hero mappings 
RYTH = 0
VESPER_VALE = 1
VENDETTA = 2
CASSI = 3
ISSAC = 4
STELLAR = 5
PRISM = 6
MARCELLA = 7
WILLOW = 8
REN = 9
VARG = 10
TAVI = 11

# conversion 
conversion = {
    D_RYTH: [RYTH],
    D_VESPER_VALE: [VESPER_VALE],
    D_VENDETTA: [VENDETTA],
    D_CASSI_AND_ISSAC: [CASSI, ISSAC],
    D_STELLAR: [STELLAR],
    D_PRISM: [PRISM],
    D_MARCELLA: [MARCELLA],
    D_WILLOW: [WILLOW],
    D_REN: [REN],
    D_VARG: [VARG],
    D_TAVI: [TAVI],
}

# stats mapping 
stats = {
    RYTH : {
        "health": 1200, 
        "health_growth": 100, 
        "strength": 50, 
        "strength_growth": 5, 
        "focus": 100, 
        "focus_growth": 10,
        "range": 1,
        "mobility": 1,                # mobility based on dashes
        "economy": 1,  
        "ability_scale": [1, 1, 1]    # health, strength, focus scaling (attack reset count as str, health passives count as health scaling) 
    }, 
    VESPER_VALE : {
        "health" : 900, 
        "health_growth": 100, 
        "strength": 45, 
        "strength_growth": 5, 
        "focus": 100, 
        "focus_growth": 10, 
        "range": 3, 
        "mobility": 2, 
        "economy": 2, 
        "ability_scale": [0, 0, 0]
    },
    CASSI: {
        "health": 0,
        "health_growth": 0,
        "strength": 0,
        "strength_growth": 0,
        "focus": 0,
        "focus_growth": 0,
        "range": 0,
        "mobility": 0,
        "economy": 0,
        "ability_scale": [0, 0, 0],
    },
    ISSAC: {
        "health": 0,
        "health_growth": 0,
        "strength": 0,
        "strength_growth": 0,
        "focus": 0,
        "focus_growth": 0,
        "range": 0,
        "mobility": 0,
        "economy": 0,
        "ability_scale": [0, 0, 0],
    },
    STELLAR: {
        "health": 0,
        "health_growth": 0,
        "strength": 0,
        "strength_growth": 0,
        "focus": 0,
        "focus_growth": 0,
        "range": 0,
        "mobility": 0,
        "economy": 0,
        "ability_scale": [0, 0, 0],
    },
    PRISM: {
        "health": 0,
        "health_growth": 0,
        "strength": 0,
        "strength_growth": 0,
        "focus": 0,
        "focus_growth": 0,
        "range": 0,
        "mobility": 0,
        "economy": 0,
        "ability_scale": [0, 0, 0],
    },
    MARCELLA: {
        "health": 0,
        "health_growth": 0,
        "strength": 0,
        "strength_growth": 0,
        "focus": 0,
        "focus_growth": 0,
        "range": 0,
        "mobility": 0,
        "economy": 0,
        "ability_scale": [0, 0, 0],
    },
    WILLOW: {
        "health": 0,
        "health_growth": 0,
        "strength": 0,
        "strength_growth": 0,
        "focus": 0,
        "focus_growth": 0,
        "range": 0,
        "mobility": 0,
        "economy": 0,
        "ability_scale": [0, 0, 0],
    },
    REN: {
        "health": 0,
        "health_growth": 0,
        "strength": 0,
        "strength_growth": 0,
        "focus": 0,
        "focus_growth": 0,
        "range": 0,
        "mobility": 0,
        "economy": 0,
        "ability_scale": [0, 0, 0],
    },
    VARG: {
        "health": 0,
        "health_growth": 0,
        "strength": 0,
        "strength_growth": 0,
        "focus": 0,
        "focus_growth": 0,
        "range": 0,
        "mobility": 0,
        "economy": 0,
        "ability_scale": [0, 0, 0],
    },
    TAVI: {
        "health": 0,
        "health_growth": 0,
        "strength": 0,
        "strength_growth": 0,
        "focus": 0,
        "focus_growth": 0,
        "range": 0,
        "mobility": 0,
        "economy": 0,
        "ability_scale": [0, 0, 0],
    },

}

class ChampionEval: 
    def __init__(self, draft_id): 
        self.draft_id = draft_id 
        self.champion_id = conversion[draft_id] 
        draft_type = cambc.ChampionDraft(draft_id) 
        champion_type = cambc.ChampionType(self.champion_id)
        stats = stats[self.champion_id]





def play():
    # Choose randomly from the champions our team has not picked.
    chosen = cambc.draft.current_draft(cambc.Team.ALLY)
    available = [pick for pick in range(11) if pick not in chosen]  # Released champions only.

    # Draft memory starts at byte zero and is copied to each champion.
    cambc.draft_memory.submit(b"hello world")
    cambc.draft.pick(available[below(len(available))])
