"""Champion decisions. main.py dispatches here after the draft."""
import struct
from randomness import below

import cambc
from cambc import game_map

Team, Item = cambc.Team, cambc.ItemType


def distance(a, b):
    return a.chebyshev_distance(b)


def tiles_around(position, radius):
    tiles = cambc.Bitboard()
    tiles.draw_chebyshev(position, radius)
    return tiles


def blocked_tiles():
    blocked = (
        game_map.walls()
        | game_map.holes()
        | game_map.altars()
        | game_map.spawn_region(Team.ENEMY)
        | cambc.vision.champion_tiles()
        | cambc.vision.camp_tiles()
        | cambc.vision.teleport_tiles()
    )
    blocked.set(cambc.me().position(), False)
    return blocked


def move_towards_closest(goals):
    me = cambc.me()
    origin = me.position()

    if not me.can_move() or goals.is_set(origin):
        return False

    blocked = blocked_tiles()
    goals &= ~blocked | cambc.vision.camp_tiles()

    if goals.is_empty():
        return False

    if goals.count() == 1:
        goal = goals.positions()[0]
        if not blocked.is_set(goal):
            return me.move_towards(goal)

    field = cambc.DistanceField()
    field.compute_to(
        blocked & ~goals,
        goals,
        tiles_around(origin, 0),
        cambc.Bitboard(),
    )

    reachable = blocked.reachable_tiles(
        origin, me.base_stats().base_move_speed
    )
    candidates = [origin, *reachable.positions()]
    step = min(candidates, key=field.get)

    return step != origin and me.move_absolute(step)


def attack_if_in_range(target):
    me = cambc.me()

    if (
        not me.can_attack()
        or not target.is_visible()
        or target.health() <= 0
        or not me.attack_tiles().is_set(target.position())
    ):
        return False

    me.attack(target)
    return True


def farm_camps():
    # Kill a camp already in range before spending movement.
    for camp in cambc.vision.camps():
        if attack_if_in_range(camp):
            return

    goals = cambc.Bitboard()
    visible = cambc.vision.visible_tiles()

    for group in game_map.camp_groups():
        # Skip groups visibly in their respawn phase.
        if (
            not (group.spawn_tiles() & visible).is_empty()
            and isinstance(group.state(), game_map.CampGroupRespawning)
        ):
            continue

        goals |= group.spawn_tiles()

    if move_towards_closest(goals):
        # Movement can put a camp in attack range.
        for camp in cambc.vision.camps():
            if attack_if_in_range(camp):
                return

# Champion-specific stat upgrades.
STAT_UPGRADES = {
    cambc.ChampionType.RYTH: Item.POWER_SHARD_OF_STRENGTH,
    cambc.ChampionType.VESPER_VALE: Item.POWER_SHARD_OF_STRENGTH,
    cambc.ChampionType.VENDETTA: Item.POWER_SHARD_OF_STRENGTH,
    cambc.ChampionType.CASSI: Item.POWER_SHARD_OF_FOCUS,
    cambc.ChampionType.ISSAC: Item.POWER_SHARD_OF_STRENGTH,
    cambc.ChampionType.STELLAR: Item.POWER_SHARD_OF_FOCUS,
    cambc.ChampionType.PRISM: Item.POWER_SHARD_OF_FOCUS,
    cambc.ChampionType.MARCELLA: Item.POWER_SHARD_OF_FOCUS,
    cambc.ChampionType.WILLOW: Item.POWER_SHARD_OF_STRENGTH,
    cambc.ChampionType.REN: Item.POWER_SHARD_OF_FOCUS,
    cambc.ChampionType.VARG: Item.POWER_SHARD_OF_STRENGTH,
    cambc.ChampionType.TAVI: Item.POWER_SHARD_OF_FOCUS,
}


class Bot:
    def buy_items(self):
        me = cambc.me()

        champion = me.champion_name()
        item = STAT_UPGRADES[champion]

        count = me.gold_count() // item.cost()

        if count > 0 and me.can_buy(item, count):
            me.buy(item, count)

    def play_turn(self):
        me = cambc.me()

        # Do not interrupt actions already in progress.
        if (
            not me.is_alive()
            or me.is_recalling()
            or me.is_submitting_gems()
            or me.is_teleporting()
        ):
            return

        at_spawn = game_map.spawn_region(Team.ALLY).is_set(
            me.position()
        )

        # Buy upgrades whenever at spawn.
        if at_spawn:
            self.buy_items()

        # Recall at low health when away from spawn.
        if (
            not at_spawn
            and me.health() < me.max_health() * 0.35
        ):
            me.recall()
            return

        # If unable to act, don't issue another action.
        if not me.can_move() and not me.can_attack():
            return

        # Attack an enemy champion in range.
        for enemy in cambc.vision.champions(Team.ENEMY):
            if attack_if_in_range(enemy):
                return

        # Deposit carried gems before returning to camp farming.
        if me.gem_count() > 0:
            goals = cambc.Bitboard()

            for altar in game_map.altars().positions():
                if cambc.vision.has_altar_lock(altar):
                    continue

                if (
                    distance(me.position(), altar)
                    <= cambc.constants.SUBMIT_GEMS_RANGE
                ):
                    me.submit_gems(altar)
                    return

                goals |= tiles_around(
                    altar, cambc.constants.SUBMIT_GEMS_RANGE
                )

            move_towards_closest(goals)
            return

        farm_camps()

        # Retain the SDK's priority mechanism.
        me.set_priority(1 + below(100))