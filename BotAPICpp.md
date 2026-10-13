# CAMBC Bot API — C++

Event: cambc2026a. Read alongside GameRules.md. Signatures and descriptions come from the same SDK documentation shown on the Bot API page.

Examples are fragments for an initialised bot. Check visibility and action preconditions. 

```cpp
namespace bc = cambc;
```

## cambc

### champions

```cpp
std::vector<cambc::champion> champions()
```

Get allied and enemy champion handles, including champions outside vision.

Use this value in your bot's draft or turn logic.

Inputs:
- subject: bc (a valid instance)

```cpp
auto result = subject.champions();
```

### champions

```cpp
std::vector<cambc::champion> champions(team side)
```

Get all champion handles on one side, including champions outside vision. @param side Team to include.

Use this value in your bot's draft or turn logic.

Inputs:
- subject: bc (a valid instance)
- side: team

```cpp
auto result = subject.champions(side);
```

### end_turn

```cpp
void end_turn()
```

Yield the authenticated bot's current turn to the host.

Stop optional planning when little fuel remains and yield the current turn.

```cpp
if (bc::fuel_remaining() < 1000) {
    bc::end_turn();
}
```

### fuel_remaining

```cpp
std::uint64_t fuel_remaining()
```

Get the fuel left in the current turn's budget; the first draft turn gets 10x and each champion's first turn gets 2x.

Stop optional planning when little fuel remains and yield the current turn.

```cpp
if (bc::fuel_remaining() < 1000) {
    bc::end_turn();
}
```

### global_submitted_gems

```cpp
std::uint32_t global_submitted_gems(team side)
```

Get the total gems submitted by a team. @param side Team to count.

Use this value in your bot's draft or turn logic.

Inputs:
- subject: bc (a valid instance)
- side: team

```cpp
auto result = subject.global_submitted_gems(side);
```

### is_draft

```cpp
bool is_draft() noexcept
```

Whether this guest has a draft perspective, without querying the host.

Use this value in your bot's draft or turn logic.

Inputs:
- subject: bc (a valid instance)

```cpp
auto is_draft = subject.is_draft();
```

### opponent

```cpp
constexpr team opponent(team side) noexcept
```

Get the other team. @param side Team to flip.

Use this value in your bot's draft or turn logic.

Inputs:
- subject: bc (a valid instance)
- side: team

```cpp
auto result = subject.opponent(side);
```

### resign

```cpp
void resign()
```

Resign the game. A successful resign never returns, so buffered output is flushed first.

Use this value in your bot's draft or turn logic.

Inputs:
- subject: bc (a valid instance)

```cpp
subject.resign();
```

### round_number

```cpp
std::uint32_t round_number()
```

Get the current round number.

Use this value in your bot's draft or turn logic.

Inputs:
- subject: bc (a valid instance)

```cpp
auto result = subject.round_number();
```

### self

```cpp
cambc::champion& self()
```

Return this guest's authenticated champion; throws std::bad_optional_access during drafting.

Use this value in your bot's draft or turn logic.

Inputs:
- subject: bc (a valid instance)

```cpp
auto result = subject.self();
```

### to_string

```cpp
std::string to_string(ability_kind value)
```

Get the enumerator name. @param value Value to format.

### to_string

```cpp
std::string to_string(buff_types value)
```

Get the enumerator name. @param value Value to format.

### to_string

```cpp
std::string to_string(camp_type value)
```

Get the enumerator name. @param value Value to format.

### to_string

```cpp
std::string to_string(champion_draft value)
```

Get the enumerator name. @param value Value to format.

### to_string

```cpp
std::string to_string(champion_type value)
```

Get the enumerator name. @param value Value to format.

### to_string

```cpp
std::string to_string(consumable_type value)
```

Get the enumerator name. @param value Value to format.

### to_string

```cpp
std::string to_string(error_code value)
```

Get the enumerator name. @param value Value to format.

### to_string

```cpp
std::string to_string(item_passive value)
```

Get the enumerator name. @param value Value to format.

### to_string

```cpp
std::string to_string(item_type value)
```

Get the enumerator name. @param value Value to format.

### to_string

```cpp
std::string to_string(passive_type value)
```

Get the enumerator name. @param value Value to format.

### to_string

```cpp
std::string to_string(symmetry value)
```

Get the enumerator name. @param value Value to format.

### to_string

```cpp
std::string to_string(team value)
```

Get the enumerator name. @param value Value to format.

### to_string

```cpp
std::string to_string(vec2 value)
```

Format as "(x, y)". @param value Value to format.

## cambc::ability

### ability

```cpp
explicit ability(const detail::ability_props& props, std::uint32_t slot)
```

Copy immutable design metadata.

Evaluate an ability before choosing a spell.

Inputs:
- props: const detail::ability_props&
- slot: std::uint32_t

```cpp
auto result = bc::ability(props, slot);
```

### can_target_allies

```cpp
bool can_target_allies() const
```

Check the allowed-target mask for allied champions.

Evaluate an ability before choosing a spell.

```cpp
auto subject = bc::self().abilities()[0];
auto can_target_allies = subject.can_target_allies();
```

### can_target_camps

```cpp
bool can_target_camps() const
```

Check target categories, not cast readiness, range or visibility; allies/enemies exclude self.

Evaluate an ability before choosing a spell.

```cpp
auto subject = bc::self().abilities()[0];
auto can_target_camps = subject.can_target_camps();
```

### can_target_enemies

```cpp
bool can_target_enemies() const
```

Check the allowed-target mask for enemy champions.

Evaluate an ability before choosing a spell.

```cpp
auto subject = bc::self().abilities()[0];
auto can_target_enemies = subject.can_target_enemies();
```

### can_target_ground

```cpp
bool can_target_ground() const
```

Check the allowed-target mask for a tile destination.

Evaluate an ability before choosing a spell.

```cpp
auto subject = bc::self().abilities()[0];
auto can_target_ground = subject.can_target_ground();
```

### can_target_self

```cpp
bool can_target_self() const
```

Check the allowed-target mask for the casting champion.

Evaluate an ability before choosing a spell.

```cpp
auto subject = bc::self().abilities()[0];
auto can_target_self = subject.can_target_self();
```

### cooldown

```cpp
std::int32_t cooldown() const
```

Get the remaining owner turn-end ticks; zero means ready.

Evaluate an ability before choosing a spell.

```cpp
auto subject = bc::self().abilities()[0];
auto result = subject.cooldown();
```

### damage_amount

```cpp
float damage_amount() const
```

Query damage before absorption/health limits; includes caster items and passives.

Evaluate an ability before choosing a spell.

```cpp
auto subject = bc::self().abilities()[0];
auto result = subject.damage_amount();
```

### damage_amount

```cpp
float damage_amount(const champion& target) const
```

Predict health lost after mitigation and shields, capped at current health. Target must be visible. @param target Visible champion that would be hit.

Evaluate an ability before choosing a spell.

Inputs:
- target: const champion&

```cpp
auto subject = bc::self().abilities()[0];
auto result = subject.damage_amount(target);
```

### get

```cpp
template <class T> T get() const
```

Extract a payload; throws std::bad_optional_access if absent, std::bad_variant_access for a different kind.

Evaluate an ability before choosing a spell.

Inputs:
- T: the payload/value type required by this template

```cpp
auto subject = bc::self().abilities()[0];
auto result = subject.get<T>();
```

### heal_amount

```cpp
float heal_amount() const
```

Query heal before absorption/health limits; includes caster items and passives.

Evaluate an ability before choosing a spell.

```cpp
auto subject = bc::self().abilities()[0];
auto result = subject.heal_amount();
```

### heal_amount

```cpp
float heal_amount(const champion& target) const
```

Cap healing at the visible target's missing health. Does not cast or consume resources. @param target Visible champion that would be healed.

Evaluate an ability before choosing a spell.

Inputs:
- target: const champion&

```cpp
auto subject = bc::self().abilities()[0];
auto result = subject.heal_amount(target);
```

### info

```cpp
const ability_info& info(ability_kind kind) const
```

Get selected metadata; unavailable or mismatched payloads throw. @param kind Ability kind the slot must hold.

Evaluate an ability before choosing a spell.

Inputs:
- kind: ability_kind

```cpp
auto subject = bc::self().abilities()[0];
auto result = subject.info(kind);
```

### info

```cpp
const std::optional<ability_info>& info() const
```

Get constructor-cached ability properties; empty for an unused slot. Rethrow the cached visibility error if the owner could not be inspected at construction.

Evaluate an ability before choosing a spell.

```cpp
auto subject = bc::self().abilities()[0];
auto result = subject.info();
```

### is

```cpp
bool is(ability_kind kind) const noexcept
```

Check the kind of constructor-cached ability metadata without querying the host. @param kind Ability kind to test for.

Evaluate an ability before choosing a spell.

Inputs:
- kind: ability_kind

```cpp
auto subject = bc::self().abilities()[0];
auto result = subject.is(kind);
```

### is

```cpp
template <class T> bool is(type_tag_t<T>) const noexcept
```

Check whether constructor-cached metadata holds the selected payload type.

Evaluate an ability before choosing a spell.

Inputs:
- value1: type_tag_t<T>
- T: the payload/value type required by this template

```cpp
auto subject = bc::self().abilities()[0];
auto result = subject.is(value1);
```

### is_aoe_damage

```cpp
bool is_aoe_damage() const noexcept
```

Check whether this is an area-damage ability.

Evaluate an ability before choosing a spell.

```cpp
auto subject = bc::self().abilities()[0];
auto is_aoe_damage = subject.is_aoe_damage();
```

### is_attack_reset

```cpp
bool is_attack_reset() const noexcept
```

Check whether this is an attack-reset ability.

Evaluate an ability before choosing a spell.

```cpp
auto subject = bc::self().abilities()[0];
auto is_attack_reset = subject.is_attack_reset();
```

### is_cc

```cpp
bool is_cc() const noexcept
```

Check whether this is a crowd-control ability.

Evaluate an ability before choosing a spell.

```cpp
auto subject = bc::self().abilities()[0];
auto is_cc = subject.is_cc();
```

### is_damage

```cpp
bool is_damage() const noexcept
```

Check whether this is a damage ability.

Evaluate an ability before choosing a spell.

```cpp
auto subject = bc::self().abilities()[0];
auto is_damage = subject.is_damage();
```

### is_dash

```cpp
bool is_dash() const noexcept
```

Check whether this is a dash ability.

Evaluate an ability before choosing a spell.

```cpp
auto subject = bc::self().abilities()[0];
auto is_dash = subject.is_dash();
```

### is_heal

```cpp
bool is_heal() const noexcept
```

Check whether this is a healing ability.

Evaluate an ability before choosing a spell.

```cpp
auto subject = bc::self().abilities()[0];
auto is_heal = subject.is_heal();
```

### is_on_cooldown

```cpp
bool is_on_cooldown() const
```

Query whether the live ability has a positive remaining cooldown.

Evaluate an ability before choosing a spell.

```cpp
auto subject = bc::self().abilities()[0];
auto is_on_cooldown = subject.is_on_cooldown();
```

### is_ready

```cpp
bool is_ready() const
```

Check whether this ability is ready to cast.

Evaluate an ability before choosing a spell.

```cpp
auto subject = bc::self().abilities()[0];
auto is_ready = subject.is_ready();
```

### is_shield

```cpp
bool is_shield() const noexcept
```

Check whether this is a shield ability.

Evaluate an ability before choosing a spell.

```cpp
auto subject = bc::self().abilities()[0];
auto is_shield = subject.is_shield();
```

### mask_into

```cpp
void mask_into(bitboard& out) const
```

Like `valid_tiles`, but writes into `out` instead of returning a new board. @param out Destination; its previous contents are overwritten.

Evaluate an ability before choosing a spell.

Inputs:
- out: bitboard&

```cpp
auto subject = bc::self().abilities()[0];
subject.mask_into(out);
```

### shield_amount

```cpp
float shield_amount() const
```

Query shield before absorption/health limits; includes caster items and passives.

Evaluate an ability before choosing a spell.

```cpp
auto subject = bc::self().abilities()[0];
auto result = subject.shield_amount();
```

### shield_amount

```cpp
float shield_amount(const champion& target) const
```

Query full shield strength for a visible target. Does not cast or consume resources. @param target Visible champion that would be shielded.

Evaluate an ability before choosing a spell.

Inputs:
- target: const champion&

```cpp
auto subject = bc::self().abilities()[0];
auto result = subject.shield_amount(target);
```

### slot

```cpp
std::uint32_t slot() const noexcept
```

Return this ability's fixed champion slot index.

Evaluate an ability before choosing a spell.

```cpp
auto subject = bc::self().abilities()[0];
auto result = subject.slot();
```

### use

```cpp
bool use() const
```

Cast without a target or destination. Returns false for a newly revealed hidden obstruction; known invalid destinations throw.

Evaluate an ability before choosing a spell.

```cpp
auto subject = bc::self().abilities()[0];
auto result = subject.use();
```

### use

```cpp
bool use(const camp& target) const
```

Cast on a camp; the cast lands on the target's tile. Returns false for a newly revealed hidden obstruction; known invalid destinations throw. @param target Camp to cast on.

Evaluate an ability before choosing a spell.

Inputs:
- target: const camp&

```cpp
auto subject = bc::self().abilities()[0];
auto result = subject.use(target);
```

### use

```cpp
bool use(const champion& target) const
```

Cast on a champion; unit casts land on the target's tile. Returns false for a newly revealed hidden obstruction; known invalid destinations throw. @param target Champion to cast on.

Evaluate an ability before choosing a spell.

Inputs:
- target: const champion&

```cpp
auto subject = bc::self().abilities()[0];
auto result = subject.use(target);
```

### use

```cpp
bool use(vec2 destination) const
```

Cast at a ground tile. Returns false for a newly revealed hidden obstruction; known invalid destinations throw. @param destination Ground tile to cast at.

Evaluate an ability before choosing a spell.

Inputs:
- destination: vec2

```cpp
auto subject = bc::self().abilities()[0];
auto result = subject.use(destination);
```

### valid_targets

```cpp
ability_target_mask valid_targets() const
```

Query the engine's live target-category flags.

Evaluate an ability before choosing a spell.

```cpp
auto subject = bc::self().abilities()[0];
auto result = subject.valid_targets();
```

### valid_tiles

```cpp
bitboard valid_tiles() const
```

Casting range only; does not filter visibility, occupancy or target categories. Throws for range-less or unused slots, hidden owners, and design-only abilities.

Evaluate an ability before choosing a spell.

```cpp
auto subject = bc::self().abilities()[0];
auto result = subject.valid_tiles();
```

## cambc::ability_aoe_damage

### operator==

```cpp
friend bool operator==(const ability_aoe_damage&, const ability_aoe_damage&) = default
```

Compare every field.

## cambc::ability_attack_reset

### operator==

```cpp
friend bool operator==(const ability_attack_reset&, const ability_attack_reset&) = default
```

Compare every field.

## cambc::ability_cc

### operator==

```cpp
friend bool operator==(const ability_cc&, const ability_cc&) = default
```

Compare every field.

## cambc::ability_damage

### operator==

```cpp
friend bool operator==(const ability_damage&, const ability_damage&) = default
```

Compare every field.

## cambc::ability_dash

### operator==

```cpp
friend bool operator==(const ability_dash&, const ability_dash&) = default
```

Compare every field.

## cambc::ability_heal

### operator==

```cpp
friend bool operator==(const ability_heal&, const ability_heal&) = default
```

Compare every field.

## cambc::ability_properties

### operator==

```cpp
friend bool operator==(const ability_properties&, const ability_properties&) = default
```

Compare every field.

## cambc::ability_shield

### operator==

```cpp
friend bool operator==(const ability_shield&, const ability_shield&) = default
```

Compare every field.

## cambc::ability_target_mask

### ability_target_mask

```cpp
constexpr ability_target_mask() noexcept = default
```

Construct a mask with no target categories.

Combine or inspect flags before deciding which actions or targets are allowed.

```cpp
auto result = bc::ability_target_mask();
```

### all

```cpp
static constexpr ability_target_mask all() noexcept
```

Construct a mask with every known flag set.

Combine or inspect flags before deciding which actions or targets are allowed.

```cpp
auto result = bc::ability_target_mask::all();
```

### begin

```cpp
constexpr iterator begin() const noexcept
```



Combine or inspect flags before deciding which actions or targets are allowed.

```cpp
auto subject = bc::self().abilities()[0].valid_targets();
auto result = subject.begin();
```

### bits

```cpp
constexpr std::uint32_t bits() const noexcept
```

Return the underlying engine bit pattern, including unknown flags.

Combine or inspect flags before deciding which actions or targets are allowed.

```cpp
auto subject = bc::self().abilities()[0].valid_targets();
auto result = subject.bits();
```

### contains

```cpp
constexpr bool contains(ability_target_mask flags) const noexcept
```

Check that all selected bits are present; an empty mask is always contained. @param flags Flags to test.

Combine or inspect flags before deciding which actions or targets are allowed.

Inputs:
- flags: ability_target_mask

```cpp
auto subject = bc::self().abilities()[0].valid_targets();
auto result = subject.contains(flags);
```

### empty

```cpp
static constexpr ability_target_mask empty() noexcept
```

Construct an empty mask.

Combine or inspect flags before deciding which actions or targets are allowed.

```cpp
auto result = bc::ability_target_mask::empty();
```

### end

```cpp
constexpr iterator end() const noexcept
```



Combine or inspect flags before deciding which actions or targets are allowed.

```cpp
auto subject = bc::self().abilities()[0].valid_targets();
auto result = subject.end();
```

### from_bits

```cpp
static constexpr std::optional<ability_target_mask> from_bits(std::uint32_t bits) noexcept
```

Decode raw engine bits, or return nullopt if any bit is not a known flag. @param bits Raw flag bits.

Combine or inspect flags before deciding which actions or targets are allowed.

Inputs:
- bits: std::uint32_t

```cpp
auto result = bc::ability_target_mask::from_bits(bits);
```

### from_bits_retain

```cpp
static constexpr ability_target_mask from_bits_retain(std::uint32_t bits) noexcept
```

Decode raw engine bits without discarding unknown future flags. @param bits Raw flag bits, kept as is including unnamed bits.

Combine or inspect flags before deciding which actions or targets are allowed.

Inputs:
- bits: std::uint32_t

```cpp
auto result = bc::ability_target_mask::from_bits_retain(bits);
```

### from_bits_truncate

```cpp
static constexpr ability_target_mask from_bits_truncate(std::uint32_t bits) noexcept
```

Decode raw engine bits, discarding unknown flags. @param bits Raw flag bits; unnamed bits are dropped.

Combine or inspect flags before deciding which actions or targets are allowed.

Inputs:
- bits: std::uint32_t

```cpp
auto result = bc::ability_target_mask::from_bits_truncate(bits);
```

### insert

```cpp
constexpr void insert(ability_target_mask flags) noexcept
```

Set selected flags. @param flags Flags to set.

Combine or inspect flags before deciding which actions or targets are allowed.

Inputs:
- flags: ability_target_mask

```cpp
auto subject = bc::self().abilities()[0].valid_targets();
subject.insert(flags);
```

### intersects

```cpp
constexpr bool intersects(ability_target_mask flags) const noexcept
```

Check whether any selected bit is present. @param flags Flags to test.

Combine or inspect flags before deciding which actions or targets are allowed.

Inputs:
- flags: ability_target_mask

```cpp
auto subject = bc::self().abilities()[0].valid_targets();
auto result = subject.intersects(flags);
```

### is_all

```cpp
constexpr bool is_all() const noexcept
```

Check whether every known flag is set.

Combine or inspect flags before deciding which actions or targets are allowed.

```cpp
auto subject = bc::self().abilities()[0].valid_targets();
auto is_all = subject.is_all();
```

### is_empty

```cpp
constexpr bool is_empty() const noexcept
```

Check whether no bits, known or unknown, are set.

Combine or inspect flags before deciding which actions or targets are allowed.

```cpp
auto subject = bc::self().abilities()[0].valid_targets();
auto is_empty = subject.is_empty();
```

### operator-

```cpp
friend constexpr ability_target_mask operator-(ability_target_mask a, ability_target_mask b) noexcept
```

Return the flags in a that are not in b.

Combine or inspect flags before deciding which actions or targets are allowed.

Inputs:
- a: ability_target_mask
- b: ability_target_mask

```cpp
auto result = a - b;
```

### operator-=

```cpp
constexpr ability_target_mask& operator-=(ability_target_mask flags) noexcept
```

Remove selected flags.

Combine or inspect flags before deciding which actions or targets are allowed.

Inputs:
- flags: ability_target_mask

```cpp
auto subject = bc::self().abilities()[0].valid_targets();
auto result = subject -= flags;
```

### operator&

```cpp
friend constexpr ability_target_mask operator&(ability_target_mask a, ability_target_mask b) noexcept
```

Intersect bitmasks.

Combine or inspect flags before deciding which actions or targets are allowed.

Inputs:
- a: ability_target_mask
- b: ability_target_mask

```cpp
auto result = a & b;
```

### operator&=

```cpp
constexpr ability_target_mask& operator&=(ability_target_mask flags) noexcept
```

Retain only selected flags.

Combine or inspect flags before deciding which actions or targets are allowed.

Inputs:
- flags: ability_target_mask

```cpp
auto subject = bc::self().abilities()[0].valid_targets();
auto result = subject &= flags;
```

### operator^

```cpp
friend constexpr ability_target_mask operator^(ability_target_mask a, ability_target_mask b) noexcept
```

Toggle selected bits.

Combine or inspect flags before deciding which actions or targets are allowed.

Inputs:
- a: ability_target_mask
- b: ability_target_mask

```cpp
auto result = a ^ b;
```

### operator^=

```cpp
constexpr ability_target_mask& operator^=(ability_target_mask flags) noexcept
```

Toggle selected flags.

Combine or inspect flags before deciding which actions or targets are allowed.

Inputs:
- flags: ability_target_mask

```cpp
auto subject = bc::self().abilities()[0].valid_targets();
auto result = subject ^= flags;
```

### operator==

```cpp
friend constexpr bool operator==(ability_target_mask a, ability_target_mask b) noexcept
```

Compare every bit, including unknown flags.

### operator|

```cpp
friend constexpr ability_target_mask operator|(ability_target_mask a, ability_target_mask b) noexcept
```

Compose bitmasks without discarding unknown flags.

Combine or inspect flags before deciding which actions or targets are allowed.

Inputs:
- a: ability_target_mask
- b: ability_target_mask

```cpp
auto result = a | b;
```

### operator|=

```cpp
constexpr ability_target_mask& operator|=(ability_target_mask flags) noexcept
```

Insert selected flags.

Combine or inspect flags before deciding which actions or targets are allowed.

Inputs:
- flags: ability_target_mask

```cpp
auto subject = bc::self().abilities()[0].valid_targets();
auto result = subject |= flags;
```

### operator~

```cpp
friend constexpr ability_target_mask operator~(ability_target_mask value) noexcept
```

Complement known flags; unknown flags are discarded.

Combine or inspect flags before deciding which actions or targets are allowed.

Inputs:
- value: ability_target_mask

```cpp
auto result = ~value;
```

### remove

```cpp
constexpr void remove(ability_target_mask flags) noexcept
```

Clear selected flags. @param flags Flags to clear.

Combine or inspect flags before deciding which actions or targets are allowed.

Inputs:
- flags: ability_target_mask

```cpp
auto subject = bc::self().abilities()[0].valid_targets();
subject.remove(flags);
```

### set

```cpp
constexpr void set(ability_target_mask flags, bool value) noexcept
```

Insert selected flags when value is true, otherwise remove them. @param flags Flags to update. @param value `true` to set the flags, `false` to clear them.

Combine or inspect flags before deciding which actions or targets are allowed.

Inputs:
- flags: ability_target_mask
- value: bool

```cpp
auto subject = bc::self().abilities()[0].valid_targets();
subject.set(flags, value);
```

### toggle

```cpp
constexpr void toggle(ability_target_mask flags) noexcept
```

Toggle selected flags. @param flags Flags to flip.

Combine or inspect flags before deciding which actions or targets are allowed.

Inputs:
- flags: ability_target_mask

```cpp
auto subject = bc::self().abilities()[0].valid_targets();
subject.toggle(flags);
```

## cambc::altar_lock

### altar_lock

```cpp
explicit altar_lock(detail::altar_lock_info value)
```

Copy a visible altar-lock observation; its fields remain a snapshot.

Check visible objects before choosing a destination.

Inputs:
- value: detail::altar_lock_info

```cpp
auto result = bc::altar_lock(value);
```

### hits_remaining

```cpp
std::uint32_t hits_remaining() const noexcept
```

Get the remaining hits from the captured altar-lock observation.

Check visible objects before choosing a destination.

Inputs:
- subject: bc::altar_lock (a valid instance)

```cpp
auto result = subject.hits_remaining();
```

### is_ally

```cpp
bool is_ally() const
```

Check whether this altar lock belongs to the observing team.

Check visible objects before choosing a destination.

Inputs:
- subject: bc::altar_lock (a valid instance)

```cpp
auto is_ally = subject.is_ally();
```

### is_enemy

```cpp
bool is_enemy() const
```

Check whether this altar lock belongs to the opposing team.

Check visible objects before choosing a destination.

Inputs:
- subject: bc::altar_lock (a valid instance)

```cpp
auto is_enemy = subject.is_enemy();
```

### operator==

```cpp
friend bool operator==(const altar_lock&, const altar_lock&) = default
```

Compare every captured field.

### position

```cpp
vec2 position() const noexcept
```

Get the tile captured in this altar-lock observation.

Check visible objects before choosing a destination.

Inputs:
- subject: bc::altar_lock (a valid instance)

```cpp
auto result = subject.position();
```

### team

```cpp
team team() const
```

Get the owning team relative to the authenticated bot.

Check visible objects before choosing a destination.

Inputs:
- subject: bc::altar_lock (a valid instance)

```cpp
auto result = subject.team();
```

## cambc::bitboard

### bitboard

```cpp
bitboard()
```

Allocate an empty mask.

Build a pathfinding or targeting mask.

```cpp
auto result = bc::bitboard();
```

### bitboard

```cpp
bitboard(bitboard&&) noexcept = default
```

Transfer board storage without querying the host.

Build a pathfinding or targeting mask.

Inputs:
- value1: bitboard&&

```cpp
auto result = bc::bitboard(std::move(value1));
```

### bitboard

```cpp
bitboard(const bitboard& other)
```

Copy the raw buffer, including padding.

Build a pathfinding or targeting mask.

Inputs:
- other: const bitboard&

```cpp
auto result = bc::bitboard(other);
```

### bitboard

```cpp
explicit bitboard(uninitialized_t)
```

Allocate aligned storage without initialising it.

Build a pathfinding or targeting mask.

Inputs:
- value1: uninitialized_t

```cpp
auto result = bc::bitboard(value1);
```

### clear

```cpp
void clear()
```

Clear all storage, including padding bits.

Build a pathfinding or targeting mask.

```cpp
auto subject = bc::map::walls();
subject.clear();
```

### count

```cpp
std::uint32_t count() const
```

Count set map tiles.

Build a pathfinding or targeting mask.

```cpp
auto subject = bc::map::walls();
auto result = subject.count();
```

### draw_chebyshev

```cpp
void draw_chebyshev(vec2 center, float radius)
```

Add tiles within a Chebyshev radius. @param center Centre tile. @param radius Chebyshev radius in tiles.

Build a pathfinding or targeting mask.

Inputs:
- center: vec2
- radius: float

```cpp
auto subject = bc::map::walls();
subject.draw_chebyshev(center, radius);
```

### draw_circle

```cpp
void draw_circle(vec2 center, float radius)
```

Add tiles within a Euclidean radius. @param center Centre tile. @param radius Euclidean radius in tiles.

Build a pathfinding or targeting mask.

Inputs:
- center: vec2
- radius: float

```cpp
auto subject = bc::map::walls();
subject.draw_circle(center, radius);
```

### export_linear_buffer

```cpp
std::vector<std::uint8_t> export_linear_buffer() const
```

Return a packed row-major copy.

Build a pathfinding or targeting mask.

```cpp
auto subject = bc::map::walls();
auto result = subject.export_linear_buffer();
```

### export_linear_buffer

```cpp
void export_linear_buffer(std::span<std::uint8_t> bytes) const
```

Export to a caller-provided row-major buffer. @param bytes Destination with one byte per tile in row-major order, exactly `linear_buffer_size()` long.

Build a pathfinding or targeting mask.

Inputs:
- bytes: std::span<std::uint8_t>

```cpp
auto subject = bc::map::walls();
subject.export_linear_buffer(bytes);
```

### export_raw_buffer

```cpp
std::vector<std::uint8_t> export_raw_buffer() const
```

Return a copy of raw storage.

Build a pathfinding or targeting mask.

```cpp
auto subject = bc::map::walls();
auto result = subject.export_raw_buffer();
```

### export_raw_buffer

```cpp
void export_raw_buffer(std::span<std::uint8_t> bytes) const
```

Copy raw storage to a caller-provided buffer. @param bytes Destination, exactly `raw_buffer_size()` long.

Build a pathfinding or targeting mask.

Inputs:
- bytes: std::span<std::uint8_t>

```cpp
auto subject = bc::map::walls();
subject.export_raw_buffer(bytes);
```

### fill

```cpp
void fill()
```

Set every in-bounds tile bit; padding is handled by the engine.

Build a pathfinding or targeting mask.

```cpp
auto subject = bc::map::walls();
subject.fill();
```

### height

```cpp
std::int32_t height() const
```

Get the map height in tiles.

### import_linear_buffer

```cpp
void import_linear_buffer(std::span<const std::uint8_t> bytes)
```

Import a packed row-major buffer. @param bytes One byte per tile in row-major order, exactly `linear_buffer_size()` long; nonzero sets the tile.

Build a pathfinding or targeting mask.

Inputs:
- bytes: std::span<const std::uint8_t>

```cpp
auto subject = bc::map::walls();
subject.import_linear_buffer(bytes);
```

### import_raw_buffer

```cpp
void import_raw_buffer(std::span<const std::uint8_t> bytes)
```

Copy an engine-chunked buffer into this object. @param bytes Engine-chunked bytes, exactly `raw_buffer_size()` long.

Build a pathfinding or targeting mask.

Inputs:
- bytes: std::span<const std::uint8_t>

```cpp
auto subject = bc::map::walls();
subject.import_raw_buffer(bytes);
```

### is_empty

```cpp
bool is_empty() const
```

Check whether all map tiles are clear.

Build a pathfinding or targeting mask.

```cpp
auto subject = bc::map::walls();
auto is_empty = subject.is_empty();
```

### is_set

```cpp
bool is_set(vec2 p) const
```

Read a tile; throws if it is outside the map. @param p Tile to query.

Build a pathfinding or targeting mask.

Inputs:
- p: vec2

```cpp
auto subject = bc::map::walls();
auto is_set = subject.is_set(p);
```

### linear_buffer_size

```cpp
static std::size_t linear_buffer_size()
```

Get the size of the packed row-major buffer in bytes.

Build a pathfinding or targeting mask.

```cpp
auto result = bc::bitboard::linear_buffer_size();
```

### operator&

```cpp
friend bitboard operator&(bitboard&& a, const bitboard& b)
```

Tiles set in both masks, reusing a's storage.

Build a pathfinding or targeting mask.

Inputs:
- a: bitboard&&
- b: const bitboard&

```cpp
auto result = std::move(a) & b;
```

### operator&

```cpp
friend bitboard operator&(const bitboard& a, const bitboard& b)
```

Tiles set in both masks.

Build a pathfinding or targeting mask.

Inputs:
- a: const bitboard&
- b: const bitboard&

```cpp
auto result = a & b;
```

### operator&=

```cpp
bitboard& operator&=(const bitboard& b)
```

Keep only tiles also set in b.

Build a pathfinding or targeting mask.

Inputs:
- b: const bitboard&

```cpp
auto subject = bc::map::walls();
auto result = subject &= b;
```

### operator^

```cpp
friend bitboard operator^(bitboard&& a, const bitboard& b)
```

Tiles set in exactly one mask, reusing a's storage.

Build a pathfinding or targeting mask.

Inputs:
- a: bitboard&&
- b: const bitboard&

```cpp
auto result = std::move(a) ^ b;
```

### operator^

```cpp
friend bitboard operator^(const bitboard& a, const bitboard& b)
```

Tiles set in exactly one mask.

Build a pathfinding or targeting mask.

Inputs:
- a: const bitboard&
- b: const bitboard&

```cpp
auto result = a ^ b;
```

### operator^=

```cpp
bitboard& operator^=(const bitboard& b)
```

Toggle every tile set in b.

Build a pathfinding or targeting mask.

Inputs:
- b: const bitboard&

```cpp
auto subject = bc::map::walls();
auto result = subject ^= b;
```

### operator=

```cpp
bitboard& operator=(bitboard&&) noexcept = default
```

Take over another mask's storage.

### operator=

```cpp
bitboard& operator=(const bitboard& other)
```

Copy another mask's tiles.

### operator==

```cpp
friend bool operator==(const bitboard& a, const bitboard& b)
```

Check whether both masks contain the same map tiles.

### operator|

```cpp
friend bitboard operator|(bitboard&& a, const bitboard& b)
```

(bitboard&& a|Tiles set in either mask, reusing a's storage.

Build a pathfinding or targeting mask.

Inputs:
- a: bitboard&&
- b: const bitboard&

```cpp
auto result = std::move(a) | b;
```

### operator|

```cpp
friend bitboard operator|(const bitboard& a, const bitboard& b)
```

(const bitboard& a|Tiles set in either mask.

Build a pathfinding or targeting mask.

Inputs:
- a: const bitboard&
- b: const bitboard&

```cpp
auto result = a | b;
```

### operator|=

```cpp
bitboard& operator|=(const bitboard& b)
```

Combine map tiles in place.

Build a pathfinding or targeting mask.

Inputs:
- b: const bitboard&

```cpp
auto subject = bc::map::walls();
auto result = subject |= b;
```

### operator~

```cpp
bitboard operator~() const
```

Return the complement of the map tiles.

Build a pathfinding or targeting mask.

```cpp
auto subject = bc::map::walls();
auto result = ~subject;
```

### positions

```cpp
std::vector<vec2> positions() const
```

Get the positions of all set map tiles.

Build a pathfinding or targeting mask.

```cpp
auto subject = bc::map::walls();
auto result = subject.positions();
```

### print

```cpp
void print() const
```

Print the buffer through the engine.

Build a pathfinding or targeting mask.

```cpp
auto subject = bc::map::walls();
subject.print();
```

### ptr

```cpp
const std::uint64_t* ptr() const noexcept
```

Access contiguous backing storage; preserve its layout when passing it to the host.

Build a pathfinding or targeting mask.

```cpp
auto subject = bc::map::walls();
auto result = subject.ptr();
```

### ptr

```cpp
std::uint64_t* ptr() noexcept
```

Access aligned raw storage for SDK imports.

Build a pathfinding or targeting mask.

```cpp
auto subject = bc::map::walls();
auto result = subject.ptr();
```

### raw_buffer_size

```cpp
static std::size_t raw_buffer_size()
```

Get the size of the engine-chunked buffer in bytes.

Build a pathfinding or targeting mask.

```cpp
auto result = bc::bitboard::raw_buffer_size();
```

### reachable_tiles

```cpp
bitboard reachable_tiles(vec2 origin, std::uint16_t distance, bitboard& frontier, bitboard& scratch) const
```

Get reachable tiles using caller-provided working buffers. @param origin Start tile. @param distance Maximum number of 8-directional steps from `origin`. @param frontier Working buffer; its contents are overwritten. @param scratch Working buffer; its contents are overwritten.

Build a pathfinding or targeting mask.

Inputs:
- origin: vec2
- distance: std::uint16_t
- frontier: bitboard&
- scratch: bitboard&

```cpp
auto subject = bc::map::walls();
auto result = subject.reachable_tiles(origin, distance, frontier, scratch);
```

### reachable_tiles

```cpp
bitboard reachable_tiles(vec2 origin, std::uint16_t distance) const
```

Get reachable tiles using this board as walls. @param origin Start tile. @param distance Maximum number of 8-directional steps from `origin`.

Build a pathfinding or targeting mask.

Inputs:
- origin: vec2
- distance: std::uint16_t

```cpp
auto subject = bc::map::walls();
auto result = subject.reachable_tiles(origin, distance);
```

### reachable_tiles_into

```cpp
void reachable_tiles_into(vec2 origin, std::uint16_t distance, bitboard& frontier, bitboard& scratch, bitboard& out) const
```

Like `reachable_tiles`, but writes into `out` instead of returning a new board. @param origin Start tile. @param distance Maximum number of 8-directional steps from `origin`. @param frontier Working buffer; its contents are overwritten. @param scratch Working buffer; its contents are overwritten. @param out Destination; its previous contents are overwritten.

Build a pathfinding or targeting mask.

Inputs:
- origin: vec2
- distance: std::uint16_t
- frontier: bitboard&
- scratch: bitboard&
- out: bitboard&

```cpp
auto subject = bc::map::walls();
subject.reachable_tiles_into(origin, distance, frontier, scratch, out);
```

### reachable_tiles_into

```cpp
void reachable_tiles_into(vec2 origin, std::uint16_t distance, bitboard& out) const
```

Like `reachable_tiles`, but writes into `out` instead of returning a new board. @param origin Start tile. @param distance Maximum number of 8-directional steps from `origin`. @param out Destination; its previous contents are overwritten.

Build a pathfinding or targeting mask.

Inputs:
- origin: vec2
- distance: std::uint16_t
- out: bitboard&

```cpp
auto subject = bc::map::walls();
subject.reachable_tiles_into(origin, distance, out);
```

### reflect

```cpp
bitboard reflect(symmetry value) const
```

Return a reflected copy using the requested map symmetry. @param value Map symmetry to apply.

Build a pathfinding or targeting mask.

Inputs:
- value: symmetry

```cpp
auto subject = bc::map::walls();
auto result = subject.reflect(value);
```

### reflect_horizontal

```cpp
bitboard reflect_horizontal() const
```

Reflect across the map horizontally, reversing tile x coordinates.

Build a pathfinding or targeting mask.

```cpp
auto subject = bc::map::walls();
auto result = subject.reflect_horizontal();
```

### reflect_horizontal_into

```cpp
void reflect_horizontal_into(bitboard& out) const
```

Like `reflect_horizontal`, but writes into `out`. @param out Destination; its previous contents are overwritten.

Build a pathfinding or targeting mask.

Inputs:
- out: bitboard&

```cpp
auto subject = bc::map::walls();
subject.reflect_horizontal_into(out);
```

### reflect_into

```cpp
void reflect_into(symmetry value, bitboard& out) const
```

Like `reflect`, but writes into `out` instead of returning a new board. @param value Map symmetry to apply. @param out Destination; its previous contents are overwritten.

Build a pathfinding or targeting mask.

Inputs:
- value: symmetry
- out: bitboard&

```cpp
auto subject = bc::map::walls();
subject.reflect_into(value, out);
```

### reflect_vertical

```cpp
bitboard reflect_vertical() const
```

Reflect across the map vertically, reversing tile y coordinates.

Build a pathfinding or targeting mask.

```cpp
auto subject = bc::map::walls();
auto result = subject.reflect_vertical();
```

### reflect_vertical_into

```cpp
void reflect_vertical_into(bitboard& out) const
```

Like `reflect_vertical`, but writes into `out`. @param out Destination; its previous contents are overwritten.

Build a pathfinding or targeting mask.

Inputs:
- out: bitboard&

```cpp
auto subject = bc::map::walls();
subject.reflect_vertical_into(out);
```

### rotate_180

```cpp
bitboard rotate_180() const
```

Rotate tiles 180 degrees within the map dimensions.

Build a pathfinding or targeting mask.

```cpp
auto subject = bc::map::walls();
auto result = subject.rotate_180();
```

### rotate_180_into

```cpp
void rotate_180_into(bitboard& out) const
```

Like `rotate_180`, but writes into `out`. @param out Destination; its previous contents are overwritten.

Build a pathfinding or targeting mask.

Inputs:
- out: bitboard&

```cpp
auto subject = bc::map::walls();
subject.rotate_180_into(out);
```

### set

```cpp
void set(vec2 p, bool value = true)
```

Set or clear a tile; throws if it is outside the map. @param p Tile to update. @param value `true` to set the tile, `false` to clear it.

Build a pathfinding or targeting mask.

Inputs:
- p: vec2
- value: bool

```cpp
auto subject = bc::map::walls();
subject.set(p, value);
```

### shadow_cast

```cpp
bitboard shadow_cast(vec2 origin, std::uint16_t distance) const
```

Get visible tiles using this board as opaque walls. @param origin Viewer tile. @param distance Maximum sight radius in tiles.

Build a pathfinding or targeting mask.

Inputs:
- origin: vec2
- distance: std::uint16_t

```cpp
auto subject = bc::map::walls();
auto result = subject.shadow_cast(origin, distance);
```

### shadow_cast_into

```cpp
void shadow_cast_into(vec2 origin, std::uint16_t distance, bitboard& out) const
```

Like `shadow_cast`, but writes into `out` instead of returning a new board. @param origin Viewer tile. @param distance Maximum sight radius in tiles. @param out Destination; its previous contents are overwritten.

Build a pathfinding or targeting mask.

Inputs:
- origin: vec2
- distance: std::uint16_t
- out: bitboard&

```cpp
auto subject = bc::map::walls();
subject.shadow_cast_into(origin, distance, out);
```

### shift_right

```cpp
bitboard shift_right(std::int32_t count) const
```

Shift tiles horizontally; negative counts shift left. @param count Columns to shift; negative values shift left.

Build a pathfinding or targeting mask.

Inputs:
- count: std::int32_t

```cpp
auto subject = bc::map::walls();
auto result = subject.shift_right(count);
```

### shift_right_into

```cpp
void shift_right_into(std::int32_t count, bitboard& out) const
```

Like `shift_right`, but writes into `out` instead of returning a new board. @param count Columns to shift; negative values shift left. @param out Destination; its previous contents are overwritten.

Build a pathfinding or targeting mask.

Inputs:
- count: std::int32_t
- out: bitboard&

```cpp
auto subject = bc::map::walls();
subject.shift_right_into(count, out);
```

### shift_up

```cpp
bitboard shift_up(std::int32_t count) const
```

Shift tiles vertically; negative counts shift down. @param count Rows to shift; negative values shift down.

Build a pathfinding or targeting mask.

Inputs:
- count: std::int32_t

```cpp
auto subject = bc::map::walls();
auto result = subject.shift_up(count);
```

### shift_up_into

```cpp
void shift_up_into(std::int32_t count, bitboard& out) const
```

Like `shift_up`, but writes into `out` instead of returning a new board. @param count Rows to shift; negative values shift down. @param out Destination; its previous contents are overwritten.

Build a pathfinding or targeting mask.

Inputs:
- count: std::int32_t
- out: bitboard&

```cpp
auto subject = bc::map::walls();
subject.shift_up_into(count, out);
```

### swap

```cpp
void swap(bitboard& other) noexcept
```

Exchange ownership of raw board storage without querying the host. @param other Bitboard to exchange contents with.

Build a pathfinding or targeting mask.

Inputs:
- other: bitboard&

```cpp
auto subject = bc::map::walls();
subject.swap(other);
```

### toggle

```cpp
void toggle(vec2 p)
```

Invert a tile; throws if it is outside the map. @param p Tile to flip.

Build a pathfinding or targeting mask.

Inputs:
- p: vec2

```cpp
auto subject = bc::map::walls();
subject.toggle(p);
```

### width

```cpp
std::int32_t width() const
```

Get the map dimensions in tiles.

## cambc::buff_collection

### begin

```cpp
const_iterator begin() const
```

Iterate captured buffs without refreshing; the collection must outlive the iterator.

Inspect a visible unit's active effects when evaluating a fight.

```cpp
auto subject = bc::self().buffs();
auto result = subject.begin();
```

### end

```cpp
std::default_sentinel_t end() const noexcept
```

Get the past-the-end iterator for the stored values or owning snapshot.

Inspect a visible unit's active effects when evaluating a fight.

```cpp
auto subject = bc::self().buffs();
auto result = subject.end();
```

### get

```cpp
buff_t get(buff_types kind) const
```

Get the requested buff snapshot; absent buffs throw std::out_of_range. @param kind Buff kind to look up.

Inspect a visible unit's active effects when evaluating a fight.

Inputs:
- kind: buff_types

```cpp
auto subject = bc::self().buffs();
auto result = subject.get(kind);
```

### get

```cpp
template <class T> T get() const
```

Get the requested buff snapshot; absent buffs throw std::out_of_range.

Inspect a visible unit's active effects when evaluating a fight.

Inputs:
- T: the payload/value type required by this template

```cpp
auto subject = bc::self().buffs();
auto result = subject.get<T>();
```

### has

```cpp
bool has(buff_types kind) const
```

Check the captured collection for this buff kind without querying the host. @param kind Buff kind to test for.

Inspect a visible unit's active effects when evaluating a fight.

Inputs:
- kind: buff_types

```cpp
auto subject = bc::self().buffs();
auto result = subject.has(kind);
```

### has

```cpp
template <class T> bool has() const
```

Check the captured collection for the selected buff payload type.

Inspect a visible unit's active effects when evaluating a fight.

Inputs:
- T: the payload/value type required by this template

```cpp
auto subject = bc::self().buffs();
auto result = subject.has<T>();
```

### has_enhanced_attack

```cpp
bool has_enhanced_attack() const
```

Check whether the captured collection has an enhanced-attack buff.

Inspect a visible unit's active effects when evaluating a fight.

```cpp
auto subject = bc::self().buffs();
auto has_enhanced_attack = subject.has_enhanced_attack();
```

### has_shield

```cpp
bool has_shield() const
```

Check whether the captured collection has a shield buff.

Inspect a visible unit's active effects when evaluating a fight.

```cpp
auto subject = bc::self().buffs();
auto has_shield = subject.has_shield();
```

### has_stun

```cpp
bool has_stun() const
```

Check whether the captured collection has a stun buff.

Inspect a visible unit's active effects when evaluating a fight.

```cpp
auto subject = bc::self().buffs();
auto has_stun = subject.has_stun();
```

### is_empty

```cpp
bool is_empty() const
```

Check whether the captured collection has no active buffs.

Inspect a visible unit's active effects when evaluating a fight.

```cpp
auto subject = bc::self().buffs();
auto is_empty = subject.is_empty();
```

### operator==

```cpp
friend bool operator==(const buff_collection&, const buff_collection&) = default
```

Compare every captured buff.

### size

```cpp
std::size_t size() const
```

Get the number of entries in this container.

## cambc::buff_collection::const_iterator

### const_iterator

```cpp
const_iterator() = default
```

Construct an iterator that compares equal to the end sentinel.

Inspect a visible unit's active effects when evaluating a fight.

```cpp
auto result = bc::buff_collection::const_iterator();
```

### const_iterator

```cpp
explicit const_iterator(const storage_type& values)
```

Iterate the captured buffs in values.

Inspect a visible unit's active effects when evaluating a fight.

Inputs:
- values: const storage_type&

```cpp
auto result = bc::buff_collection::const_iterator(values);
```

### operator->

```cpp
pointer operator->() const
```

Access the current buff.

### operator*

```cpp
reference operator*() const
```

Access the current buff.

Inspect a visible unit's active effects when evaluating a fight.

Inputs:
- subject: bc::buff_collection::const_iterator (a valid instance)

```cpp
auto result = *subject;
```

### operator++

```cpp
const_iterator operator++(int)
```

Advance to the next captured buff, returning the previous position.

### operator++

```cpp
const_iterator& operator++()
```

Advance to the next captured buff.

### operator==

```cpp
bool operator==(const const_iterator&) const = default
```

Compare iterator positions.

### operator==

```cpp
bool operator==(std::default_sentinel_t) const
```

Check whether every captured buff has been visited.

## cambc::buff_enhanced_attack

### operator==

```cpp
friend bool operator==(const buff_enhanced_attack&, const buff_enhanced_attack&) = default
```

Compare every field.

## cambc::buff_restrictions

### all

```cpp
static constexpr buff_restrictions all() noexcept
```

Construct a mask with every known flag set.

Combine or inspect flags before deciding which actions or targets are allowed.

```cpp
auto result = bc::buff_restrictions::all();
```

### begin

```cpp
constexpr iterator begin() const noexcept
```



Combine or inspect flags before deciding which actions or targets are allowed.

```cpp
auto subject = bc::self().restrictions();
auto result = subject.begin();
```

### bits

```cpp
constexpr std::uint8_t bits() const noexcept
```

Return the underlying engine bit pattern, including unknown flags.

Combine or inspect flags before deciding which actions or targets are allowed.

```cpp
auto subject = bc::self().restrictions();
auto result = subject.bits();
```

### buff_restrictions

```cpp
constexpr buff_restrictions() noexcept = default
```

Construct a mask with no restrictions.

Combine or inspect flags before deciding which actions or targets are allowed.

```cpp
auto result = bc::buff_restrictions();
```

### contains

```cpp
constexpr bool contains(buff_restrictions flags) const noexcept
```

Check that all selected bits are present; an empty mask is always contained. @param flags Flags to test.

Combine or inspect flags before deciding which actions or targets are allowed.

Inputs:
- flags: buff_restrictions

```cpp
auto subject = bc::self().restrictions();
auto result = subject.contains(flags);
```

### empty

```cpp
static constexpr buff_restrictions empty() noexcept
```

Construct an empty mask.

Combine or inspect flags before deciding which actions or targets are allowed.

```cpp
auto result = bc::buff_restrictions::empty();
```

### end

```cpp
constexpr iterator end() const noexcept
```



Combine or inspect flags before deciding which actions or targets are allowed.

```cpp
auto subject = bc::self().restrictions();
auto result = subject.end();
```

### from_bits

```cpp
static constexpr std::optional<buff_restrictions> from_bits(std::uint8_t bits) noexcept
```

Decode raw engine bits, or return nullopt if any bit is not a known flag. @param bits Raw flag bits.

Combine or inspect flags before deciding which actions or targets are allowed.

Inputs:
- bits: std::uint8_t

```cpp
auto result = bc::buff_restrictions::from_bits(bits);
```

### from_bits_retain

```cpp
static constexpr buff_restrictions from_bits_retain(std::uint8_t bits) noexcept
```

Decode raw engine bits without discarding unknown future flags. @param bits Raw flag bits, kept as is including unnamed bits.

Combine or inspect flags before deciding which actions or targets are allowed.

Inputs:
- bits: std::uint8_t

```cpp
auto result = bc::buff_restrictions::from_bits_retain(bits);
```

### from_bits_truncate

```cpp
static constexpr buff_restrictions from_bits_truncate(std::uint8_t bits) noexcept
```

Decode raw engine bits, discarding unknown flags. @param bits Raw flag bits; unnamed bits are dropped.

Combine or inspect flags before deciding which actions or targets are allowed.

Inputs:
- bits: std::uint8_t

```cpp
auto result = bc::buff_restrictions::from_bits_truncate(bits);
```

### insert

```cpp
constexpr void insert(buff_restrictions flags) noexcept
```

Set selected flags. @param flags Flags to set.

Combine or inspect flags before deciding which actions or targets are allowed.

Inputs:
- flags: buff_restrictions

```cpp
auto subject = bc::self().restrictions();
subject.insert(flags);
```

### intersects

```cpp
constexpr bool intersects(buff_restrictions flags) const noexcept
```

Check whether any selected bit is present. @param flags Flags to test.

Combine or inspect flags before deciding which actions or targets are allowed.

Inputs:
- flags: buff_restrictions

```cpp
auto subject = bc::self().restrictions();
auto result = subject.intersects(flags);
```

### is_all

```cpp
constexpr bool is_all() const noexcept
```

Check whether every known flag is set.

Combine or inspect flags before deciding which actions or targets are allowed.

```cpp
auto subject = bc::self().restrictions();
auto is_all = subject.is_all();
```

### is_empty

```cpp
constexpr bool is_empty() const noexcept
```

Check whether no bits, known or unknown, are set.

Combine or inspect flags before deciding which actions or targets are allowed.

```cpp
auto subject = bc::self().restrictions();
auto is_empty = subject.is_empty();
```

### operator-

```cpp
friend constexpr buff_restrictions operator-(buff_restrictions a, buff_restrictions b) noexcept
```

Return the flags in a that are not in b.

Combine or inspect flags before deciding which actions or targets are allowed.

Inputs:
- a: buff_restrictions
- b: buff_restrictions

```cpp
auto result = a - b;
```

### operator-=

```cpp
constexpr buff_restrictions& operator-=(buff_restrictions flags) noexcept
```

Remove selected flags.

Combine or inspect flags before deciding which actions or targets are allowed.

Inputs:
- flags: buff_restrictions

```cpp
auto subject = bc::self().restrictions();
auto result = subject -= flags;
```

### operator&

```cpp
friend constexpr buff_restrictions operator&(buff_restrictions a, buff_restrictions b) noexcept
```

Intersect bitmasks.

Combine or inspect flags before deciding which actions or targets are allowed.

Inputs:
- a: buff_restrictions
- b: buff_restrictions

```cpp
auto result = a & b;
```

### operator&=

```cpp
constexpr buff_restrictions& operator&=(buff_restrictions flags) noexcept
```

Retain only selected flags.

Combine or inspect flags before deciding which actions or targets are allowed.

Inputs:
- flags: buff_restrictions

```cpp
auto subject = bc::self().restrictions();
auto result = subject &= flags;
```

### operator^

```cpp
friend constexpr buff_restrictions operator^(buff_restrictions a, buff_restrictions b) noexcept
```

Toggle selected bits.

Combine or inspect flags before deciding which actions or targets are allowed.

Inputs:
- a: buff_restrictions
- b: buff_restrictions

```cpp
auto result = a ^ b;
```

### operator^=

```cpp
constexpr buff_restrictions& operator^=(buff_restrictions flags) noexcept
```

Toggle selected flags.

Combine or inspect flags before deciding which actions or targets are allowed.

Inputs:
- flags: buff_restrictions

```cpp
auto subject = bc::self().restrictions();
auto result = subject ^= flags;
```

### operator==

```cpp
friend constexpr bool operator==(buff_restrictions a, buff_restrictions b) noexcept
```

Compare every bit, including unknown flags.

### operator|

```cpp
friend constexpr buff_restrictions operator|(buff_restrictions a, buff_restrictions b) noexcept
```

Compose bitmasks without discarding unknown flags.

Combine or inspect flags before deciding which actions or targets are allowed.

Inputs:
- a: buff_restrictions
- b: buff_restrictions

```cpp
auto result = a | b;
```

### operator|=

```cpp
constexpr buff_restrictions& operator|=(buff_restrictions flags) noexcept
```

Insert selected flags.

Combine or inspect flags before deciding which actions or targets are allowed.

Inputs:
- flags: buff_restrictions

```cpp
auto subject = bc::self().restrictions();
auto result = subject |= flags;
```

### operator~

```cpp
friend constexpr buff_restrictions operator~(buff_restrictions value) noexcept
```

Complement known flags; unknown flags are discarded.

Combine or inspect flags before deciding which actions or targets are allowed.

Inputs:
- value: buff_restrictions

```cpp
auto result = ~value;
```

### remove

```cpp
constexpr void remove(buff_restrictions flags) noexcept
```

Clear selected flags. @param flags Flags to clear.

Combine or inspect flags before deciding which actions or targets are allowed.

Inputs:
- flags: buff_restrictions

```cpp
auto subject = bc::self().restrictions();
subject.remove(flags);
```

### set

```cpp
constexpr void set(buff_restrictions flags, bool value) noexcept
```

Insert selected flags when value is true, otherwise remove them. @param flags Flags to update. @param value `true` to set the flags, `false` to clear them.

Combine or inspect flags before deciding which actions or targets are allowed.

Inputs:
- flags: buff_restrictions
- value: bool

```cpp
auto subject = bc::self().restrictions();
subject.set(flags, value);
```

### toggle

```cpp
constexpr void toggle(buff_restrictions flags) noexcept
```

Toggle selected flags. @param flags Flags to flip.

Combine or inspect flags before deciding which actions or targets are allowed.

Inputs:
- flags: buff_restrictions

```cpp
auto subject = bc::self().restrictions();
subject.toggle(flags);
```

## cambc::buff_shield

### operator==

```cpp
friend bool operator==(const buff_shield&, const buff_shield&) = default
```

Compare every field.

## cambc::buff_stun

### operator==

```cpp
friend bool operator==(const buff_stun&, const buff_stun&) = default
```

Compare every field.

## cambc::camp

### attack_range

```cpp
std::int32_t attack_range() const override
```

Get attack range from this camp's design; melee camps have range 1.

Evaluate a visible camp before choosing where to farm.

Inputs:
- subject: bc::camp (a valid instance)

```cpp
auto result = subject.attack_range();
```

### base_stats

```cpp
camp_stats base_stats() const
```

Get an owning snapshot of the camp's engine-provided design stats.

Evaluate a visible camp before choosing where to farm.

Inputs:
- subject: bc::camp (a valid instance)

```cpp
auto result = subject.base_stats();
```

### camp_group

```cpp
map::camp_group camp_group() const
```

Query this camp's group from the engine on every call; invalid identities throw.

Evaluate a visible camp before choosing where to farm.

Inputs:
- subject: bc::camp (a valid instance)

```cpp
auto result = subject.camp_group();
```

### focus

```cpp
float focus() const override
```

Query current focus for this visible unit.

Evaluate a visible camp before choosing where to farm.

Inputs:
- subject: bc::camp (a valid instance)

```cpp
auto result = subject.focus();
```

### health

```cpp
float health() const override
```

Query current health; live state requires a visible runtime identity.

Evaluate a visible camp before choosing where to farm.

Inputs:
- subject: bc::camp (a valid instance)

```cpp
auto result = subject.health();
```

### is_elk

```cpp
bool is_elk() const noexcept
```

Check whether this camp is an elk.

Evaluate a visible camp before choosing where to farm.

Inputs:
- subject: bc::camp (a valid instance)

```cpp
auto is_elk = subject.is_elk();
```

### is_gem_cluster

```cpp
bool is_gem_cluster() const noexcept
```

Check whether this camp is a gem cluster.

Evaluate a visible camp before choosing where to farm.

Inputs:
- subject: bc::camp (a valid instance)

```cpp
auto is_gem_cluster = subject.is_gem_cluster();
```

### is_gold_cluster

```cpp
bool is_gold_cluster() const noexcept
```

Check whether this camp is a gold cluster.

Evaluate a visible camp before choosing where to farm.

Inputs:
- subject: bc::camp (a valid instance)

```cpp
auto is_gold_cluster = subject.is_gold_cluster();
```

### is_monolith

```cpp
bool is_monolith() const noexcept
```

Check whether this camp is a monolith.

Evaluate a visible camp before choosing where to farm.

Inputs:
- subject: bc::camp (a valid instance)

```cpp
auto is_monolith = subject.is_monolith();
```

### is_visible

```cpp
bool is_visible() const override
```

Check whether live state is available; hidden units and design-only views return false.

Evaluate a visible camp before choosing where to farm.

Inputs:
- subject: bc::camp (a valid instance)

```cpp
auto is_visible = subject.is_visible();
```

### is_wolf

```cpp
bool is_wolf() const noexcept
```

Check whether this camp is a wolf.

Evaluate a visible camp before choosing where to farm.

Inputs:
- subject: bc::camp (a valid instance)

```cpp
auto is_wolf = subject.is_wolf();
```

### kind

```cpp
camp_type kind() const noexcept
```

Get the entity or inventory entry kind.

Evaluate a visible camp before choosing where to farm.

Inputs:
- subject: bc::camp (a valid instance)

```cpp
auto result = subject.kind();
```

### max_health

```cpp
float max_health() const override
```

Query current maximum health; live state requires a visible runtime identity.

Evaluate a visible camp before choosing where to farm.

Inputs:
- subject: bc::camp (a valid instance)

```cpp
auto result = subject.max_health();
```

### operator handle_camp_t

```cpp
operator handle_camp_t() const noexcept
```

Convert to the strong camp identity.

### operator==

```cpp
friend bool operator==(const camp& a, const camp& b) noexcept
```

Compare camp identities.

### position

```cpp
vec2 position() const override
```

Get the tile position; live entity views query current engine state.

Evaluate a visible camp before choosing where to farm.

Inputs:
- subject: bc::camp (a valid instance)

```cpp
auto result = subject.position();
```

### spawn_position

```cpp
vec2 spawn_position() const noexcept
```

Get the camp's original spawn tile, not its current live position.

Evaluate a visible camp before choosing where to farm.

Inputs:
- subject: bc::camp (a valid instance)

```cpp
auto result = subject.spawn_position();
```

### strength

```cpp
float strength() const override
```

Query current strength for this visible unit.

Evaluate a visible camp before choosing where to farm.

Inputs:
- subject: bc::camp (a valid instance)

```cpp
auto result = subject.strength();
```

## cambc::champion

### abilities

```cpp
std::array<ability, 2> abilities() const
```

Return a copy of the two champion-bound ability views with immutable cached metadata.

Evaluate your champion's options during its turn.

```cpp
auto subject = bc::self();
auto result = subject.abilities();
```

### attack

```cpp
void attack(const altar_lock& target) const
```

Attack an altar lock; requires the authenticated self and host-validated action availability. @param target Visible altar lock within attack range.

Evaluate your champion's options during its turn.

Inputs:
- target: const altar_lock&

```cpp
auto subject = bc::self();
subject.attack(target);
```

### attack

```cpp
void attack(const unit& target) const
```

Attack a champion or camp view; requires the authenticated self and host-validated action availability. @param target Visible champion or camp within attack range.

Evaluate your champion's options during its turn.

Inputs:
- target: const unit&

```cpp
auto subject = bc::self();
subject.attack(target);
```

### attack_range

```cpp
std::int32_t attack_range() const override
```

Get the unit's attack range in tiles; champion state is queried live.

Evaluate your champion's options during its turn.

```cpp
auto subject = bc::self();
auto result = subject.attack_range();
```

### base_stats

```cpp
champion_stats base_stats() const
```

Get the champion design base stats.

Evaluate your champion's options during its turn.

```cpp
auto subject = bc::self();
auto result = subject.base_stats();
```

### buy

```cpp
void buy(consumable_type type) const
```

Buy a consumable for self. @param type Consumable to buy.

Evaluate your champion's options during its turn.

Inputs:
- type: consumable_type

```cpp
auto subject = bc::self();
subject.buy(type);
```

### buy

```cpp
void buy(item_type type, std::uint32_t count = 1) const
```

Buy items for self. @param type Item to buy. @param count Number of copies to buy.

Evaluate your champion's options during its turn.

Inputs:
- type: item_type
- count: std::uint32_t

```cpp
auto subject = bc::self();
subject.buy(type, count);
```

### can_attack

```cpp
bool can_attack() const
```

Check whether this champion can attack.

Check the action allowance before spending fuel to choose an attack target.

```cpp
const auto me = bc::self();
const bool action_available = me.can_attack();
```

### can_buy

```cpp
bool can_buy(consumable_type consumable) const
```

Check whether a consumable can be bought in the current state. @param consumable Consumable to buy.

Evaluate your champion's options during its turn.

Inputs:
- consumable: consumable_type

```cpp
auto subject = bc::self();
auto can_buy = subject.can_buy(consumable);
```

### can_buy

```cpp
bool can_buy(item_type item, std::uint32_t count = 1) const
```

Check whether an item can be bought in the current state. @param item Item to buy. @param count Number of copies to buy.

Evaluate your champion's options during its turn.

Inputs:
- item: item_type
- count: std::uint32_t

```cpp
auto subject = bc::self();
auto can_buy = subject.can_buy(item, count);
```

### can_cast

```cpp
bool can_cast() const
```

Check whether this champion can cast.

Check the action allowance before spending fuel to evaluate ready abilities.

```cpp
const auto me = bc::self();
const bool action_available = me.can_cast();
```

### can_move

```cpp
bool can_move() const
```

Check whether this champion can move.

Check the action allowance before spending fuel to plan a path.

```cpp
const auto me = bc::self();
const bool action_available = me.can_move();
```

### can_sell

```cpp
bool can_sell() const
```

Check whether the authenticated self currently has permission to sell.

Evaluate your champion's options during its turn.

```cpp
auto subject = bc::self();
auto can_sell = subject.can_sell();
```

### cancel_recall

```cpp
void cancel_recall() const
```

Cancel an active recall.

Evaluate your champion's options during its turn.

```cpp
auto subject = bc::self();
subject.cancel_recall();
```

### cancel_submit_gems

```cpp
void cancel_submit_gems() const
```

Cancel an active gem submission.

Evaluate your champion's options during its turn.

```cpp
auto subject = bc::self();
subject.cancel_submit_gems();
```

### champion

```cpp
explicit champion(champion_type type)
```

A design-only champion: immutable properties are available, live queries require vision.

Evaluate your champion's options during its turn.

Inputs:
- type: champion_type

```cpp
auto result = bc::champion(type);
```

### champion_name

```cpp
champion_type champion_name() const
```

Get the entity or inventory entry kind.

Evaluate your champion's options during its turn.

```cpp
auto subject = bc::self();
auto result = subject.champion_name();
```

### consumables

```cpp
const enum_map<consumable_type, consumable>& consumables() const noexcept
```

Action wrappers by consumable type; each count is queried live.

Evaluate your champion's options during its turn.

```cpp
auto subject = bc::self();
auto result = subject.consumables();
```

### end_turn

```cpp
void end_turn() const
```

Yield the current champion turn.

Evaluate your champion's options during its turn.

```cpp
auto subject = bc::self();
subject.end_turn();
```

### focus

```cpp
float focus() const override
```

Query current focus for this visible unit.

Evaluate your champion's options during its turn.

```cpp
auto subject = bc::self();
auto result = subject.focus();
```

### gem_count

```cpp
std::uint32_t gem_count() const
```

Get carried gems.

Evaluate your champion's options during its turn.

```cpp
auto subject = bc::self();
auto result = subject.gem_count();
```

### gold_count

```cpp
std::uint32_t gold_count() const
```

Get gold; this query requires self.

Evaluate your champion's options during its turn.

```cpp
auto subject = bc::self();
auto result = subject.gold_count();
```

### has_completed_turn

```cpp
bool has_completed_turn() const
```

Check whether this champion has completed its turn.

Evaluate your champion's options during its turn.

```cpp
auto subject = bc::self();
auto has_completed_turn = subject.has_completed_turn();
```

### health

```cpp
float health() const override
```

Get current and maximum health.

Recall when badly injured and safe from interruption.

Inputs:
- safe_to_recall: your bot's assessment that recalling here is safe and legal

```cpp
auto me = bc::self();
if (safe_to_recall && me.health() < me.max_health() * 0.3f) {
    me.recall();
}
```

### is_alive

```cpp
bool is_alive() const
```

Check whether the champion is alive.

Evaluate your champion's options during its turn.

```cpp
auto subject = bc::self();
auto is_alive = subject.is_alive();
```

### is_ally

```cpp
bool is_ally() const
```

Check whether this champion belongs to the observing team.

Evaluate your champion's options during its turn.

```cpp
auto subject = bc::self();
auto is_ally = subject.is_ally();
```

### is_enemy

```cpp
bool is_enemy() const
```

Check whether this champion belongs to the opposing team.

Evaluate your champion's options during its turn.

```cpp
auto subject = bc::self();
auto is_enemy = subject.is_enemy();
```

### is_recalling

```cpp
bool is_recalling() const
```

Check whether this champion is recalling.

Evaluate your champion's options during its turn.

```cpp
auto subject = bc::self();
auto is_recalling = subject.is_recalling();
```

### is_submitting_gems

```cpp
bool is_submitting_gems() const
```

Check whether this champion is submitting gems.

Evaluate your champion's options during its turn.

```cpp
auto subject = bc::self();
auto is_submitting_gems = subject.is_submitting_gems();
```

### is_teleporting

```cpp
bool is_teleporting() const
```

Check whether this champion is teleporting.

Evaluate your champion's options during its turn.

```cpp
auto subject = bc::self();
auto is_teleporting = subject.is_teleporting();
```

### is_visible

```cpp
bool is_visible() const override
```

Check whether the champion can be inspected from this perspective.

Evaluate your champion's options during its turn.

```cpp
auto subject = bc::self();
auto is_visible = subject.is_visible();
```

### item_count

```cpp
enum_map<item_type> item_count() const
```

Get item counts indexed by item type.

Evaluate your champion's options during its turn.

```cpp
auto subject = bc::self();
auto result = subject.item_count();
```

### max_health

```cpp
float max_health() const override
```

Query current maximum health; live state requires a visible runtime identity.

Recall when badly injured and safe from interruption.

Inputs:
- safe_to_recall: your bot's assessment that recalling here is safe and legal

```cpp
auto me = bc::self();
if (safe_to_recall && me.health() < me.max_health() * 0.3f) {
    me.recall();
}
```

### move

```cpp
bool move(vec2 delta) const
```

Attempt to move by a delta from the current position. Returns false for a newly revealed hidden obstruction; known invalid destinations throw. @param delta Displacement from the current position.

Take a planned step; false means no movement occurred.

Inputs:
- direction: a legal unit direction, such as east

```cpp
auto me = bc::self();
if (me.can_move()) {
    const bool moved = me.move(direction);
}
```

### move_absolute

```cpp
bool move_absolute(vec2 destination) const
```

Attempt to move to an absolute destination tile. Returns false for a newly revealed hidden obstruction; known invalid destinations throw. @param destination Map tile to move to.

Take a planned step; false means no movement occurred.

Inputs:
- destination: a destination selected by your pathfinder

```cpp
auto me = bc::self();
if (me.can_move()) {
    const bool moved = me.move_absolute(destination);
}
```

### move_towards

```cpp
bool move_towards(vec2 target) const
```

Move to the reachable tile on a shortest path to `target`, avoiding walls, holes, altars, the enemy spawn and visible units. Returns false without moving when already there, unable to move, or `target` is blocked or unreachable, and false for a newly revealed hidden obstruction. @param target Map tile to approach.

Take a planned step; false means no movement occurred.

Inputs:
- destination: a destination selected by your pathfinder

```cpp
auto me = bc::self();
if (me.can_move()) {
    const bool moved = me.move_towards(destination);
}
```

### operator handle_champion_t

```cpp
operator handle_champion_t() const noexcept
```

Access the underlying engine handle.

### operator==

```cpp
friend bool operator==(const champion& a, const champion& b) noexcept
```

Compare champion identities.

### passive

```cpp
passive_type passive() const
```

Get the passive effect tag from the champion's immutable design.

Evaluate your champion's options during its turn.

```cpp
auto subject = bc::self();
auto result = subject.passive();
```

### place_ward

```cpp
bool place_ward(vec2 destination) const
```

Place a ward at a destination tile. Returns false for a newly revealed hidden obstruction; known invalid destinations throw. @param destination Tile to place the ward on.

Evaluate your champion's options during its turn.

Inputs:
- destination: vec2

```cpp
auto subject = bc::self();
auto result = subject.place_ward(destination);
```

### position

```cpp
vec2 position() const override
```

Get the tile position; live entity views query current engine state.

Evaluate your champion's options during its turn.

```cpp
auto subject = bc::self();
auto result = subject.position();
```

### prev_priority

```cpp
std::int32_t prev_priority() const
```

Get the scheduling priority used for the current round.

Evaluate your champion's options during its turn.

```cpp
auto subject = bc::self();
auto result = subject.prev_priority();
```

### priority

```cpp
std::int32_t priority() const
```

Get the scheduling priority for the next round.

Evaluate your champion's options during its turn.

```cpp
auto subject = bc::self();
auto result = subject.priority();
```

### recall

```cpp
void recall() const
```

Start recalling to spawn.

Recall when badly injured and safe from interruption.

Inputs:
- safe_to_recall: your bot's assessment that recalling here is safe and legal

```cpp
auto me = bc::self();
if (safe_to_recall && me.health() < me.max_health() * 0.3f) {
    me.recall();
}
```

### remaining_channel_time

```cpp
std::int32_t remaining_channel_time() const
```

Query the visible champion's remaining channel rounds; no active channel throws.

Evaluate your champion's options during its turn.

```cpp
auto subject = bc::self();
auto result = subject.remaining_channel_time();
```

### restrictions

```cpp
buff_restrictions restrictions() const
```

Union of active buff restrictions; requires visibility.

Evaluate your champion's options during its turn.

```cpp
auto subject = bc::self();
auto result = subject.restrictions();
```

### sell

```cpp
void sell(item_type type, std::uint32_t count = 1) const
```

Sell items for self; the engine validates champion and shop restrictions. @param type Item to sell. @param count Number of copies to sell.

Evaluate your champion's options during its turn.

Inputs:
- type: item_type
- count: std::uint32_t

```cpp
auto subject = bc::self();
subject.sell(type, count);
```

### set_priority

```cpp
void set_priority(std::int32_t priority) const
```

Set the scheduling priority for the next round. @param priority New scheduling priority; takes effect from the next round and persists.

Evaluate your champion's options during its turn.

Inputs:
- priority: std::int32_t

```cpp
auto subject = bc::self();
subject.set_priority(priority);
```

### state

```cpp
champion_state state() const
```

Get current state and action availability.

Evaluate your champion's options during its turn.

```cpp
auto subject = bc::self();
auto result = subject.state();
```

### strength

```cpp
float strength() const override
```

Query current strength for this visible unit.

Evaluate your champion's options during its turn.

```cpp
auto subject = bc::self();
auto result = subject.strength();
```

### submit_gems

```cpp
void submit_gems(vec2 altar) const
```

Start submitting gems at an altar. @param altar Altar tile to tribute at.

Evaluate your champion's options during its turn.

Inputs:
- altar: vec2

```cpp
auto subject = bc::self();
subject.submit_gems(altar);
```

### team

```cpp
team team() const
```

Get the owning team relative to the authenticated bot.

Evaluate your champion's options during its turn.

```cpp
auto subject = bc::self();
auto result = subject.team();
```

### visible_tiles

```cpp
bitboard visible_tiles() const
```

Get tiles visible to this champion.

Evaluate your champion's options during its turn.

```cpp
auto subject = bc::self();
auto result = subject.visible_tiles();
```

### visible_tiles_from

```cpp
bitboard visible_tiles_from(vec2 position) const
```

Vision at a hypothetical position, using this champion's design. @param position Hypothetical tile to see from.

Evaluate your champion's options during its turn.

Inputs:
- position: vec2

```cpp
auto subject = bc::self();
auto result = subject.visible_tiles_from(position);
```

### visible_tiles_from_into

```cpp
void visible_tiles_from_into(vec2 position, bitboard& out) const
```

Like `visible_tiles_from`, but writes into `out` instead of returning a new board. @param position Hypothetical tile to see from. @param out Destination; its previous contents are overwritten.

Evaluate your champion's options during its turn.

Inputs:
- position: vec2
- out: bitboard&

```cpp
auto subject = bc::self();
subject.visible_tiles_from_into(position, out);
```

### visible_tiles_into

```cpp
void visible_tiles_into(bitboard& out) const
```

Like `visible_tiles`, but writes into `out` instead of returning a new board. @param out Destination; its previous contents are overwritten.

Evaluate your champion's options during its turn.

Inputs:
- out: bitboard&

```cpp
auto subject = bc::self();
subject.visible_tiles_into(out);
```

### ward_cooldown

```cpp
std::int32_t ward_cooldown() const
```

Query the authenticated champion's remaining ward recharge rounds.

Evaluate your champion's options during its turn.

```cpp
auto subject = bc::self();
auto result = subject.ward_cooldown();
```

### ward_placement_tiles

```cpp
bitboard ward_placement_tiles() const
```

Compute self-only ward placement range from current position and engine settings.

Evaluate your champion's options during its turn.

```cpp
auto subject = bc::self();
auto result = subject.ward_placement_tiles();
```

### ward_placement_tiles_into

```cpp
void ward_placement_tiles_into(bitboard& out) const
```

Like `ward_placement_tiles`, but writes into `out` instead of returning a new board. @param out Destination; its previous contents are overwritten.

Evaluate your champion's options during its turn.

Inputs:
- out: bitboard&

```cpp
auto subject = bc::self();
subject.ward_placement_tiles_into(out);
```

### ward_positions

```cpp
std::vector<vec2> ward_positions() const
```

Query an owning list of wards placed by the authenticated self champion.

Evaluate your champion's options during its turn.

```cpp
auto subject = bc::self();
auto result = subject.ward_positions();
```

## cambc::champion_state

### all

```cpp
static constexpr champion_state all() noexcept
```

Construct a mask with every known flag set.

Combine or inspect flags before deciding which actions or targets are allowed.

```cpp
auto result = bc::champion_state::all();
```

### begin

```cpp
constexpr iterator begin() const noexcept
```



Combine or inspect flags before deciding which actions or targets are allowed.

```cpp
auto subject = bc::self().state();
auto result = subject.begin();
```

### bits

```cpp
constexpr std::uint32_t bits() const noexcept
```

Return the underlying engine bit pattern, including unknown flags.

Combine or inspect flags before deciding which actions or targets are allowed.

```cpp
auto subject = bc::self().state();
auto result = subject.bits();
```

### champion_state

```cpp
constexpr champion_state() noexcept = default
```

Construct a state with no flags set.

Combine or inspect flags before deciding which actions or targets are allowed.

```cpp
auto result = bc::champion_state();
```

### contains

```cpp
constexpr bool contains(champion_state flags) const noexcept
```

Check that all selected bits are present; an empty mask is always contained. @param flags Flags to test.

Combine or inspect flags before deciding which actions or targets are allowed.

Inputs:
- flags: champion_state

```cpp
auto subject = bc::self().state();
auto result = subject.contains(flags);
```

### empty

```cpp
static constexpr champion_state empty() noexcept
```

Construct an empty mask.

Combine or inspect flags before deciding which actions or targets are allowed.

```cpp
auto result = bc::champion_state::empty();
```

### end

```cpp
constexpr iterator end() const noexcept
```



Combine or inspect flags before deciding which actions or targets are allowed.

```cpp
auto subject = bc::self().state();
auto result = subject.end();
```

### from_bits

```cpp
static constexpr std::optional<champion_state> from_bits(std::uint32_t bits) noexcept
```

Decode raw engine bits, or return nullopt if any bit is not a known flag. @param bits Raw flag bits.

Combine or inspect flags before deciding which actions or targets are allowed.

Inputs:
- bits: std::uint32_t

```cpp
auto result = bc::champion_state::from_bits(bits);
```

### from_bits_retain

```cpp
static constexpr champion_state from_bits_retain(std::uint32_t bits) noexcept
```

Decode raw engine bits without discarding unknown future flags. @param bits Raw flag bits, kept as is including unnamed bits.

Combine or inspect flags before deciding which actions or targets are allowed.

Inputs:
- bits: std::uint32_t

```cpp
auto result = bc::champion_state::from_bits_retain(bits);
```

### from_bits_truncate

```cpp
static constexpr champion_state from_bits_truncate(std::uint32_t bits) noexcept
```

Decode raw engine bits, discarding unknown flags. @param bits Raw flag bits; unnamed bits are dropped.

Combine or inspect flags before deciding which actions or targets are allowed.

Inputs:
- bits: std::uint32_t

```cpp
auto result = bc::champion_state::from_bits_truncate(bits);
```

### insert

```cpp
constexpr void insert(champion_state flags) noexcept
```

Set selected flags. @param flags Flags to set.

Combine or inspect flags before deciding which actions or targets are allowed.

Inputs:
- flags: champion_state

```cpp
auto subject = bc::self().state();
subject.insert(flags);
```

### intersects

```cpp
constexpr bool intersects(champion_state flags) const noexcept
```

Check whether any selected bit is present. @param flags Flags to test.

Combine or inspect flags before deciding which actions or targets are allowed.

Inputs:
- flags: champion_state

```cpp
auto subject = bc::self().state();
auto result = subject.intersects(flags);
```

### is_all

```cpp
constexpr bool is_all() const noexcept
```

Check whether every known flag is set.

Combine or inspect flags before deciding which actions or targets are allowed.

```cpp
auto subject = bc::self().state();
auto is_all = subject.is_all();
```

### is_dead

```cpp
constexpr bool is_dead() const noexcept
```

Check whether the alive flag is clear.

Combine or inspect flags before deciding which actions or targets are allowed.

```cpp
auto subject = bc::self().state();
auto is_dead = subject.is_dead();
```

### is_empty

```cpp
constexpr bool is_empty() const noexcept
```

Check whether no bits, known or unknown, are set.

Combine or inspect flags before deciding which actions or targets are allowed.

```cpp
auto subject = bc::self().state();
auto is_empty = subject.is_empty();
```

### operator-

```cpp
friend constexpr champion_state operator-(champion_state a, champion_state b) noexcept
```

Return the flags in a that are not in b.

Combine or inspect flags before deciding which actions or targets are allowed.

Inputs:
- a: champion_state
- b: champion_state

```cpp
auto result = a - b;
```

### operator-=

```cpp
constexpr champion_state& operator-=(champion_state flags) noexcept
```

Remove selected flags.

Combine or inspect flags before deciding which actions or targets are allowed.

Inputs:
- flags: champion_state

```cpp
auto subject = bc::self().state();
auto result = subject -= flags;
```

### operator&

```cpp
friend constexpr champion_state operator&(champion_state a, champion_state b) noexcept
```

Intersect bitmasks.

Combine or inspect flags before deciding which actions or targets are allowed.

Inputs:
- a: champion_state
- b: champion_state

```cpp
auto result = a & b;
```

### operator&=

```cpp
constexpr champion_state& operator&=(champion_state flags) noexcept
```

Retain only selected flags.

Combine or inspect flags before deciding which actions or targets are allowed.

Inputs:
- flags: champion_state

```cpp
auto subject = bc::self().state();
auto result = subject &= flags;
```

### operator^

```cpp
friend constexpr champion_state operator^(champion_state a, champion_state b) noexcept
```

Toggle selected bits.

Combine or inspect flags before deciding which actions or targets are allowed.

Inputs:
- a: champion_state
- b: champion_state

```cpp
auto result = a ^ b;
```

### operator^=

```cpp
constexpr champion_state& operator^=(champion_state flags) noexcept
```

Toggle selected flags.

Combine or inspect flags before deciding which actions or targets are allowed.

Inputs:
- flags: champion_state

```cpp
auto subject = bc::self().state();
auto result = subject ^= flags;
```

### operator==

```cpp
friend constexpr bool operator==(champion_state a, champion_state b) noexcept
```

Compare every bit, including unknown flags.

### operator|

```cpp
friend constexpr champion_state operator|(champion_state a, champion_state b) noexcept
```

Compose bitmasks without discarding unknown flags.

Combine or inspect flags before deciding which actions or targets are allowed.

Inputs:
- a: champion_state
- b: champion_state

```cpp
auto result = a | b;
```

### operator|=

```cpp
constexpr champion_state& operator|=(champion_state flags) noexcept
```

Insert selected flags.

Combine or inspect flags before deciding which actions or targets are allowed.

Inputs:
- flags: champion_state

```cpp
auto subject = bc::self().state();
auto result = subject |= flags;
```

### operator~

```cpp
friend constexpr champion_state operator~(champion_state value) noexcept
```

Complement known flags; unknown flags are discarded.

Combine or inspect flags before deciding which actions or targets are allowed.

Inputs:
- value: champion_state

```cpp
auto result = ~value;
```

### remove

```cpp
constexpr void remove(champion_state flags) noexcept
```

Clear selected flags. @param flags Flags to clear.

Combine or inspect flags before deciding which actions or targets are allowed.

Inputs:
- flags: champion_state

```cpp
auto subject = bc::self().state();
subject.remove(flags);
```

### set

```cpp
constexpr void set(champion_state flags, bool value) noexcept
```

Insert selected flags when value is true, otherwise remove them. @param flags Flags to update. @param value `true` to set the flags, `false` to clear them.

Combine or inspect flags before deciding which actions or targets are allowed.

Inputs:
- flags: champion_state
- value: bool

```cpp
auto subject = bc::self().state();
subject.set(flags, value);
```

### toggle

```cpp
constexpr void toggle(champion_state flags) noexcept
```

Toggle selected flags. @param flags Flags to flip.

Combine or inspect flags before deciding which actions or targets are allowed.

Inputs:
- flags: champion_state

```cpp
auto subject = bc::self().state();
subject.toggle(flags);
```

## cambc::consumable

### consumable

```cpp
consumable() = default
```

Create an inventory entry; live queries require a champion-bound entry.

Plan a purchase or use an owned consumable.

```cpp
auto result = bc::consumable();
```

### count

```cpp
std::uint32_t count() const
```

Get the number currently owned.

Plan a purchase or use an owned consumable.

Inputs:
- subject: bc::consumable (a valid instance)

```cpp
auto result = subject.count();
```

### kind

```cpp
consumable_type kind() const noexcept
```

Get the entity or inventory entry kind.

Plan a purchase or use an owned consumable.

Inputs:
- subject: bc::consumable (a valid instance)

```cpp
auto result = subject.kind();
```

### use

```cpp
bool use() const
```

Use the entry on a target or tile, if required; the host validates the self-only action. Returns false for a newly revealed hidden obstruction; known invalid destinations throw.

Plan a purchase or use an owned consumable.

Inputs:
- subject: bc::consumable (a valid instance)

```cpp
auto result = subject.use();
```

### use

```cpp
bool use(vec2 destination) const
```

Use the entry on a target or tile, if required; the host validates the self-only action. Returns false for a newly revealed hidden obstruction; known invalid destinations throw. @param destination Tile to use the consumable at.

Plan a purchase or use an owned consumable.

Inputs:
- subject: bc::consumable (a valid instance)
- destination: vec2

```cpp
auto result = subject.use(destination);
```

## cambc::consumable_info

### operator==

```cpp
friend bool operator==(const consumable_info&, const consumable_info&) = default
```

Compare every field.

## cambc::consumable_type

### carry_limit

```cpp
std::uint32_t carry_limit() const
```

Get the maximum carried inventory count.

Plan a purchase or use an owned consumable.

Inputs:
- subject: bc::consumable_type (a valid instance)

```cpp
auto result = subject.carry_limit();
```

### consumable_type

```cpp
constexpr consumable_type() noexcept = default
```

Construct the first enum value.

Plan a purchase or use an owned consumable.

```cpp
auto result = bc::consumable_type();
```

### consumable_type

```cpp
constexpr consumable_type(enum_type value) noexcept
```

Wrap an underlying enum value without changing its identity.

Plan a purchase or use an owned consumable.

Inputs:
- value: enum_type

```cpp
auto result = bc::consumable_type(value);
```

### consumable_type

```cpp
explicit constexpr consumable_type(std::uint32_t value) noexcept
```

Explicitly decode an ABI integer; metadata queries reject invalid values.

Plan a purchase or use an owned consumable.

Inputs:
- value: std::uint32_t

```cpp
auto result = bc::consumable_type(value);
```

### cost

```cpp
std::uint32_t cost() const
```

Get the purchase cost in gold.

Plan a purchase or use an owned consumable.

Inputs:
- subject: bc::consumable_type (a valid instance)

```cpp
auto result = subject.cost();
```

### info

```cpp
consumable_info info() const
```

Get an owning metadata value; invalid kinds and host failures throw.

Plan a purchase or use an owned consumable.

Inputs:
- subject: bc::consumable_type (a valid instance)

```cpp
auto result = subject.info();
```

### operator enum_type

```cpp
constexpr operator enum_type() const noexcept
```

Preserve compatibility with functions expecting the underlying enum.

### operator T

```cpp
template <class T> requires std::is_integral_v<T> constexpr explicit operator T() const noexcept
```

Explicit integer conversion for indexing and host ABI calls.

## cambc::debug

### draw_dot

```cpp
void draw_dot(vec2 position, std::uint32_t rgb)
```

Emit a debug dot on a tile using a packed 0xRRGGBB colour. @param position Tile to mark. @param rgb Packed `0xRRGGBB` colour.

Mark a decision in your replay debug overlay to understand the bot's route or target.

Inputs:
- position: vec2
- rgb: std::uint32_t

```cpp
bc::debug::draw_dot(position, rgb);
```

### draw_line

```cpp
void draw_line(vec2 from, vec2 to, std::uint32_t rgb)
```

Emit a debug line between tiles using a packed 0xRRGGBB colour. @param from Start tile. @param to End tile. @param rgb Packed `0xRRGGBB` colour.

Mark a decision in your replay debug overlay to understand the bot's route or target.

Inputs:
- from: vec2
- to: vec2
- rgb: std::uint32_t

```cpp
bc::debug::draw_line(from, to, rgb);
```

## cambc::distance_field

### compute

```cpp
void compute(const bitboard& walls, const bitboard& roots, bitboard& visited, std::uint16_t max_distance = unreachable)
```

Compute distances from roots, reusing the visited buffer. @param walls Impassable tiles. @param roots Tiles at distance zero. @param visited Receives the tiles the search reached. @param max_distance Tiles farther than this are left unreached.

Find a step towards the goal tiles. The result is a direction for move.

Inputs:
- blocked: impassable/occupied tiles, excluding your own tile
- goals: legal destination tiles

```cpp
bc::distance_field field;
bc::bitboard visited;
field.compute(blocked, goals, visited);
auto direction = field.next_move_from(bc::self().position());
```

### compute_to

```cpp
void compute_to(const bitboard& walls, const bitboard& roots, const bitboard& destinations, bitboard& visited, std::uint16_t max_distance = unreachable)
```

Compute distances until destinations are reached or the search is exhausted. @param walls Impassable tiles. @param roots Tiles at distance zero. @param destinations The search stops once these tiles are reached. @param visited Receives the tiles the search reached. @param max_distance Tiles farther than this are left unreached.

Choose a route around blocked tiles.

Inputs:
- subject: bc::distance_field (a valid instance)
- walls: const bitboard&
- roots: const bitboard&
- destinations: const bitboard&
- visited: bitboard&
- max_distance: std::uint16_t

```cpp
subject.compute_to(walls, roots, destinations, visited, max_distance);
```

### compute_to_with_expansion_count

```cpp
std::uint16_t compute_to_with_expansion_count(const bitboard& walls, const bitboard& roots, const bitboard& destinations, bitboard& visited, std::uint16_t max_distance = unreachable)
```

Compute towards destinations and return the number of expansion steps. @param walls Impassable tiles. @param roots Tiles at distance zero. @param destinations The search stops once these tiles are reached. @param visited Receives the tiles the search reached. @param max_distance Tiles farther than this are left unreached.

Choose a route around blocked tiles.

Inputs:
- subject: bc::distance_field (a valid instance)
- walls: const bitboard&
- roots: const bitboard&
- destinations: const bitboard&
- visited: bitboard&
- max_distance: std::uint16_t

```cpp
auto result = subject.compute_to_with_expansion_count(walls, roots, destinations, visited, max_distance);
```

### compute_with_expansion_count

```cpp
std::uint16_t compute_with_expansion_count(const bitboard& walls, const bitboard& roots, bitboard& visited, std::uint16_t max_distance = unreachable)
```

Compute distances and return the number of expansion steps. @param walls Impassable tiles. @param roots Tiles at distance zero. @param visited Receives the tiles the search reached. @param max_distance Tiles farther than this are left unreached.

Choose a route around blocked tiles.

Inputs:
- subject: bc::distance_field (a valid instance)
- walls: const bitboard&
- roots: const bitboard&
- visited: bitboard&
- max_distance: std::uint16_t

```cpp
auto result = subject.compute_with_expansion_count(walls, roots, visited, max_distance);
```

### distance_field

```cpp
distance_field()
```

Allocate a distance buffer that reads as zero until written.

Choose a route around blocked tiles.

```cpp
auto result = bc::distance_field();
```

### distance_field

```cpp
distance_field(const distance_field& other)
```

Copy another field's distances.

Choose a route around blocked tiles.

Inputs:
- other: const distance_field&

```cpp
auto result = bc::distance_field(other);
```

### distance_field

```cpp
distance_field(distance_field&&) noexcept = default
```

Take over another field's storage.

Choose a route around blocked tiles.

Inputs:
- value1: distance_field&&

```cpp
auto result = bc::distance_field(std::move(value1));
```

### export_linear_buffer

```cpp
std::vector<std::uint16_t> export_linear_buffer() const
```

Return a row-major copy of the distances.

Choose a route around blocked tiles.

Inputs:
- subject: bc::distance_field (a valid instance)

```cpp
auto result = subject.export_linear_buffer();
```

### export_linear_buffer

```cpp
void export_linear_buffer(std::span<std::uint16_t> values) const
```

Export to a caller-provided row-major buffer. @param values Destination with one distance per tile in row-major order.

Choose a route around blocked tiles.

Inputs:
- subject: bc::distance_field (a valid instance)
- values: std::span<std::uint16_t>

```cpp
subject.export_linear_buffer(values);
```

### export_raw_buffer

```cpp
std::vector<std::uint8_t> export_raw_buffer() const
```

Return a copy of raw storage.

Choose a route around blocked tiles.

Inputs:
- subject: bc::distance_field (a valid instance)

```cpp
auto result = subject.export_raw_buffer();
```

### export_raw_buffer

```cpp
void export_raw_buffer(std::span<std::uint8_t> bytes) const
```

Copy raw storage to a caller-provided buffer. @param bytes Destination, exactly `raw_buffer_size()` long.

Choose a route around blocked tiles.

Inputs:
- subject: bc::distance_field (a valid instance)
- bytes: std::span<std::uint8_t>

```cpp
subject.export_raw_buffer(bytes);
```

### get

```cpp
std::uint16_t get(vec2 p) const
```

Read a tile distance; throws if it is outside the map. @param p Tile to query.

Choose a route around blocked tiles.

Inputs:
- subject: bc::distance_field (a valid instance)
- p: vec2

```cpp
auto result = subject.get(p);
```

### height

```cpp
std::int32_t height() const
```

Get the map height in tiles.

### import_linear_buffer

```cpp
void import_linear_buffer(std::span<const std::uint16_t> values)
```

Import a packed row-major buffer. @param values One distance per tile in row-major order.

Choose a route around blocked tiles.

Inputs:
- subject: bc::distance_field (a valid instance)
- values: std::span<const std::uint16_t>

```cpp
subject.import_linear_buffer(values);
```

### import_raw_buffer

```cpp
void import_raw_buffer(std::span<const std::uint8_t> bytes)
```

Copy an engine-chunked buffer into this object. @param bytes Engine-chunked bytes, exactly `raw_buffer_size()` long.

Choose a route around blocked tiles.

Inputs:
- subject: bc::distance_field (a valid instance)
- bytes: std::span<const std::uint8_t>

```cpp
subject.import_raw_buffer(bytes);
```

### linear_buffer_size

```cpp
static std::size_t linear_buffer_size()
```

Get the size of the packed row-major buffer in bytes.

Choose a route around blocked tiles.

```cpp
auto result = bc::distance_field::linear_buffer_size();
```

### next_move_from

```cpp
std::optional<vec2> next_move_from(vec2 from) const
```

First optimal step from `from` towards a root, as a direction delta in compass order from north. Returns nullopt at a root or unreached tile; throws if `from` is outside the map. @param from Tile to step from.

Find a step towards the goal tiles. The result is a direction for move.

Inputs:
- blocked: impassable/occupied tiles, excluding your own tile
- goals: legal destination tiles

```cpp
bc::distance_field field;
bc::bitboard visited;
field.compute(blocked, goals, visited);
auto direction = field.next_move_from(bc::self().position());
```

### next_moves_from

```cpp
std::vector<vec2> next_moves_from(vec2 from) const
```

Every optimal step from `from` towards a root, as direction deltas in compass order from north. @param from Tile to step from.

Choose a route around blocked tiles.

Inputs:
- subject: bc::distance_field (a valid instance)
- from: vec2

```cpp
auto result = subject.next_moves_from(from);
```

### operator=

```cpp
distance_field& operator=(const distance_field& other)
```

Copy another field's distances.

### operator=

```cpp
distance_field& operator=(distance_field&&) noexcept = default
```

Take over another field's storage.

### print

```cpp
void print() const
```

Print the buffer through the engine.

Choose a route around blocked tiles.

Inputs:
- subject: bc::distance_field (a valid instance)

```cpp
subject.print();
```

### ptr

```cpp
const std::uint16_t* ptr() const noexcept
```

Access contiguous backing storage; preserve its layout when passing it to the host.

Choose a route around blocked tiles.

Inputs:
- subject: bc::distance_field (a valid instance)

```cpp
auto result = subject.ptr();
```

### ptr

```cpp
std::uint16_t* ptr() noexcept
```

Access raw distance storage for SDK imports.

Choose a route around blocked tiles.

Inputs:
- subject: bc::distance_field (a valid instance)

```cpp
auto result = subject.ptr();
```

### raw_buffer_size

```cpp
static std::size_t raw_buffer_size()
```

Get the size of the engine-chunked buffer in bytes.

Choose a route around blocked tiles.

```cpp
auto result = bc::distance_field::raw_buffer_size();
```

### set

```cpp
void set(vec2 p, std::uint16_t value)
```

Write a tile distance; throws if it is outside the map. @param p Tile to update. @param value Distance to store.

Choose a route around blocked tiles.

Inputs:
- subject: bc::distance_field (a valid instance)
- p: vec2
- value: std::uint16_t

```cpp
subject.set(p, value);
```

### width

```cpp
std::int32_t width() const
```

Get the map dimensions in tiles.

## cambc::draft

### current_draft

```cpp
std::vector<champion_draft> current_draft(team side)
```

Get an owning list of completed draft picks, recomputed each call. @param side Team whose picks to return.

Choose your team's roster during the draft phase.

Inputs:
- side: team

```cpp
auto result = bc::draft::current_draft(side);
```

### pick

```cpp
void pick(champion_draft pick)
```

Submit a champion pick for the current draft round. @param pick Champion to pick.

Choose your team's roster during the draft phase.

Inputs:
- pick: champion_draft

```cpp
bc::draft::pick(pick);
```

### team_size

```cpp
std::uint32_t team_size()
```

Query the number of champions per team; gameplay perspectives propagate engine errors.

Choose your team's roster during the draft phase.

```cpp
auto result = bc::draft::team_size();
```

## cambc::draft_memory_buffer

### begin

```cpp
auto begin() const
```

Iterate over read-only captured bytes without querying the host.

Pass your drafting decisions to the bot's later turns using draft memory.

Inputs:
- subject: bc::draft_memory_buffer (a valid instance)

```cpp
auto result = subject.begin();
```

### data

```cpp
const std::byte* data() const
```

Borrow bytes captured at startup; assignment does not change this snapshot.

Pass your drafting decisions to the bot's later turns using draft memory.

Inputs:
- subject: bc::draft_memory_buffer (a valid instance)

```cpp
auto result = subject.data();
```

### draft_memory_buffer

```cpp
draft_memory_buffer()
```

Load the current draft bytes into owning, read-only storage.

Pass your drafting decisions to the bot's later turns using draft memory.

```cpp
auto result = bc::draft_memory_buffer();
```

### draft_memory_buffer

```cpp
draft_memory_buffer(const draft_memory_buffer&) = default
```

Copy the captured bytes without querying or submitting to the engine.

Pass your drafting decisions to the bot's later turns using draft memory.

Inputs:
- value1: const draft_memory_buffer&

```cpp
auto result = bc::draft_memory_buffer(value1);
```

### end

```cpp
auto end() const
```

Get the end of the read-only captured bytes.

Pass your drafting decisions to the bot's later turns using draft memory.

Inputs:
- subject: bc::draft_memory_buffer (a valid instance)

```cpp
auto result = subject.end();
```

### is_empty

```cpp
bool is_empty() const
```

Check whether the captured buffer is empty.

Pass your drafting decisions to the bot's later turns using draft memory.

Inputs:
- subject: bc::draft_memory_buffer (a valid instance)

```cpp
auto is_empty = subject.is_empty();
```

### operator[]

```cpp
const std::byte& operator[](std::size_t offset) const
```

Read one startup-snapshot byte; an out-of-range offset throws std::out_of_range.

Pass your drafting decisions to the bot's later turns using draft memory.

Inputs:
- subject: bc::draft_memory_buffer (a valid instance)
- offset: std::size_t

```cpp
auto result = subject[offset];
```

### operator=

```cpp
draft_memory_buffer& operator=(const draft_memory_buffer& other)
```

Submit another buffer's captured bytes, including self-assignment.

### operator=

```cpp
draft_memory_buffer& operator=(std::initializer_list<std::byte> bytes)
```

Submit byte initialiser lists; an empty list submits no data.

### operator=

```cpp
draft_memory_buffer& operator=(std::span<const std::byte> bytes)
```

Submit bytes immediately; errors throw and leave captured bytes unchanged. Oversized input is truncated to capacity; shorter input leaves the remaining engine bytes unchanged.

### operator=

```cpp
template <std::ranges::contiguous_range Range> requires (std::ranges::sized_range<Range> && std::is_trivially_copyable_v<std::ranges::range_value_t<Range>>) draft_memory_buffer& operator=(Range&& values)
```

Submit contiguous arrays, vectors or spans of trivially copyable values using native layout.

### size

```cpp
std::size_t size() const
```

Get the captured byte count without querying the host.

## cambc::enum_map

### begin

```cpp
auto begin() const noexcept
```

Get a const iterator to the first enum-indexed value.

Inspect map geometry when choosing a route or objective.

Inputs:
- subject: bc::enum_map (a valid instance)

```cpp
auto result = subject.begin();
```

### begin

```cpp
auto begin() noexcept
```

Get an iterator to the first enum-indexed value.

Inspect map geometry when choosing a route or objective.

Inputs:
- subject: bc::enum_map (a valid instance)

```cpp
auto result = subject.begin();
```

### end

```cpp
auto end() const noexcept
```

Get the const past-the-end iterator for enum-indexed values.

Inspect map geometry when choosing a route or objective.

Inputs:
- subject: bc::enum_map (a valid instance)

```cpp
auto result = subject.end();
```

### end

```cpp
auto end() noexcept
```

Get the past-the-end iterator for enum-indexed values.

Inspect map geometry when choosing a route or objective.

Inputs:
- subject: bc::enum_map (a valid instance)

```cpp
auto result = subject.end();
```

### operator[]

```cpp
const Value& operator[](T key) const
```

Access the value for key; throws std::out_of_range for an invalid key.

Inspect map geometry when choosing a route or objective.

Inputs:
- subject: bc::enum_map (a valid instance)
- key: T
- T: the payload/value type required by this template

```cpp
auto result = subject[key];
```

### operator[]

```cpp
Value& operator[](T key)
```

Access the value for key; throws std::out_of_range for an invalid key.

Inspect map geometry when choosing a route or objective.

Inputs:
- subject: bc::enum_map (a valid instance)
- key: T
- T: the payload/value type required by this template

```cpp
auto result = subject[key];
```

### operator==

```cpp
friend bool operator==(const enum_map&, const enum_map&) = default
```

Compare every value.

### ptr

```cpp
const Value* ptr() const noexcept
```

Access contiguous backing storage; preserve its layout when passing it to the host.

Inspect map geometry when choosing a route or objective.

Inputs:
- subject: bc::enum_map (a valid instance)

```cpp
auto result = subject.ptr();
```

### ptr

```cpp
Value* ptr() noexcept
```

Access contiguous backing storage; preserve its layout when passing it to the host.

Inspect map geometry when choosing a route or objective.

Inputs:
- subject: bc::enum_map (a valid instance)

```cpp
auto result = subject.ptr();
```

### size

```cpp
constexpr std::size_t size() const noexcept
```

Get the number of entries in this container.

## cambc::error

### code

```cpp
error_code code() const noexcept
```

Get the engine error code.

Inspect a caught SDK error when diagnosing a rejected action.

Inputs:
- subject: bc::error (a valid instance)

```cpp
auto result = subject.code();
```

### error

```cpp
error(error_code code, std::string message)
```

Construct an error with an explicit code and message.

Inspect a caught SDK error when diagnosing a rejected action.

Inputs:
- code: error_code
- message: std::string

```cpp
auto result = bc::error(code, message);
```

### error

```cpp
explicit error(error_code code)
```

Construct an engine error with its code and diagnostic message.

Inspect a caught SDK error when diagnosing a rejected action.

Inputs:
- code: error_code

```cpp
auto result = bc::error(code);
```

### error

```cpp
explicit error(std::int32_t code)
```

Capture the engine diagnostic before another SDK call changes it.

Inspect a caught SDK error when diagnosing a rejected action.

Inputs:
- code: std::int32_t

```cpp
auto result = bc::error(code);
```

### message

```cpp
const std::string& message() const noexcept
```

Get the engine diagnostic captured when this error was created.

Inspect a caught SDK error when diagnosing a rejected action.

Inputs:
- subject: bc::error (a valid instance)

```cpp
auto result = subject.message();
```

## cambc::item_info

### operator==

```cpp
friend bool operator==(const item_info&, const item_info&) = default
```

Compare every field.

## cambc::item_type

### combine_count

```cpp
std::uint32_t combine_count() const
```

Get the ingredient count needed to create the result item.

Plan a purchase or use an owned consumable.

Inputs:
- subject: bc::item_type (a valid instance)

```cpp
auto result = subject.combine_count();
```

### cost

```cpp
std::uint32_t cost() const
```

Get the full value in gold: the purchase price, refunded in full when sold.

Plan a purchase or use an owned consumable.

Inputs:
- subject: bc::item_type (a valid instance)

```cpp
auto result = subject.cost();
```

### focus_points

```cpp
std::int32_t focus_points() const
```

Get the focus bonus.

Plan a purchase or use an owned consumable.

Inputs:
- subject: bc::item_type (a valid instance)

```cpp
auto result = subject.focus_points();
```

### health_points

```cpp
std::int32_t health_points() const
```

Get the health bonus.

Plan a purchase or use an owned consumable.

Inputs:
- subject: bc::item_type (a valid instance)

```cpp
auto result = subject.health_points();
```

### info

```cpp
item_info info() const
```

Get an owning metadata value; invalid kinds and host failures throw.

Plan a purchase or use an owned consumable.

Inputs:
- subject: bc::item_type (a valid instance)

```cpp
auto result = subject.info();
```

### is_purchasable

```cpp
bool is_purchasable() const
```

Whether the item can be bought directly; emblems must be combined.

Plan a purchase or use an owned consumable.

Inputs:
- subject: bc::item_type (a valid instance)

```cpp
auto is_purchasable = subject.is_purchasable();
```

### item_type

```cpp
constexpr item_type() noexcept = default
```

Construct the first enum value.

Plan a purchase or use an owned consumable.

```cpp
auto result = bc::item_type();
```

### item_type

```cpp
constexpr item_type(enum_type value) noexcept
```

Wrap an underlying enum value without changing its identity.

Plan a purchase or use an owned consumable.

Inputs:
- value: enum_type

```cpp
auto result = bc::item_type(value);
```

### item_type

```cpp
explicit constexpr item_type(std::uint32_t value) noexcept
```

Explicitly decode an ABI integer; metadata queries reject invalid values.

Plan a purchase or use an owned consumable.

Inputs:
- value: std::uint32_t

```cpp
auto result = bc::item_type(value);
```

### operator enum_type

```cpp
constexpr operator enum_type() const noexcept
```

Preserve compatibility with functions expecting the underlying enum.

### operator T

```cpp
template <class T> requires std::is_integral_v<T> constexpr explicit operator T() const noexcept
```

Explicit integer conversion for indexing and host ABI calls.

### passive

```cpp
std::optional<item_passive> passive() const
```

Get the optional passive effect.

Plan a purchase or use an owned consumable.

Inputs:
- subject: bc::item_type (a valid instance)

```cpp
auto result = subject.passive();
```

### passive_magnitude

```cpp
float passive_magnitude() const
```

Get the passive magnitude.

Plan a purchase or use an owned consumable.

Inputs:
- subject: bc::item_type (a valid instance)

```cpp
auto result = subject.passive_magnitude();
```

### result

```cpp
std::optional<item_type> result() const
```

Get the combination result, if this item can be combined.

Plan a purchase or use an owned consumable.

Inputs:
- subject: bc::item_type (a valid instance)

```cpp
auto result = subject.result();
```

### strength_points

```cpp
std::int32_t strength_points() const
```

Get the strength bonus.

Plan a purchase or use an owned consumable.

Inputs:
- subject: bc::item_type (a valid instance)

```cpp
auto result = subject.strength_points();
```

## cambc::map

### altars

```cpp
const bitboard& altars()
```

Get the immutable altar mask; the reference lasts for the bot lifetime.

Build a terrain-only walking mask. Also exclude occupied tiles and the enemy spawn when selecting a legal step.

```cpp
auto blocked = bc::map::walls() | bc::map::holes() | bc::map::altars();
auto walkable = ~blocked;
```

### bushes

```cpp
const bitboard& bushes()
```

Get the immutable bush mask; the reference lasts for the bot lifetime.

Inspect map geometry when choosing a route or objective.

```cpp
auto result = bc::map::bushes();
```

### camp

```cpp
cambc::camp camp(vec2 position)
```

Find a camp by spawn tile, querying the engine on every call; throws if there is no camp. @param position Spawn tile of the camp.

Inspect map geometry when choosing a route or objective.

Inputs:
- position: vec2

```cpp
auto result = bc::map::camp(position);
```

### camp_groups

```cpp
std::vector<camp_group> camp_groups()
```

Query physical camp groups from the engine on every call; also available in draft.

Inspect map geometry when choosing a route or objective.

```cpp
auto result = bc::map::camp_groups();
```

### height

```cpp
std::int32_t height()
```

Get the map height in tiles.

### holes

```cpp
const bitboard& holes()
```

Get the immutable hole mask; the reference lasts for the bot lifetime.

Build a terrain-only walking mask. Also exclude occupied tiles and the enemy spawn when selecting a legal step.

```cpp
auto blocked = bc::map::walls() | bc::map::holes() | bc::map::altars();
auto walkable = ~blocked;
```

### is_in_bounds

```cpp
bool is_in_bounds(vec2 position)
```

Check whether a tile is within the map dimensions. @param position Position to check.

Inspect map geometry when choosing a route or objective.

Inputs:
- position: vec2

```cpp
auto is_in_bounds = bc::map::is_in_bounds(position);
```

### reflect_horizontal

```cpp
vec2 reflect_horizontal(vec2 p)
```

Reflect across the map horizontally, reversing tile x coordinates. @param p Map position to reflect.

Inspect map geometry when choosing a route or objective.

Inputs:
- p: vec2

```cpp
auto result = bc::map::reflect_horizontal(p);
```

### reflect_vertical

```cpp
vec2 reflect_vertical(vec2 p)
```

Reflect across the map vertically, reversing tile y coordinates. @param p Map position to reflect.

Inspect map geometry when choosing a route or objective.

Inputs:
- p: vec2

```cpp
auto result = bc::map::reflect_vertical(p);
```

### rotate_180

```cpp
vec2 rotate_180(vec2 p)
```

Rotate tiles 180 degrees within the map dimensions. @param p Map position to rotate.

Inspect map geometry when choosing a route or objective.

Inputs:
- p: vec2

```cpp
auto result = bc::map::rotate_180(p);
```

### spawn_region

```cpp
const bitboard& spawn_region(team value)
```

Get the cached spawn mask for a team. @param value Team whose spawn region to return.

Inspect map geometry when choosing a route or objective.

Inputs:
- value: team

```cpp
auto result = bc::map::spawn_region(value);
```

### symmetry

```cpp
symmetry symmetry()
```

Get the map symmetry.

Inspect map geometry when choosing a route or objective.

```cpp
auto result = bc::map::symmetry();
```

### walls

```cpp
const bitboard& walls()
```

Get cached terrain masks; references remain valid for the bot lifetime.

Build a terrain-only walking mask. Also exclude occupied tiles and the enemy spawn when selecting a legal step.

```cpp
auto blocked = bc::map::walls() | bc::map::holes() | bc::map::altars();
auto walkable = ~blocked;
```

### width

```cpp
std::int32_t width()
```

Get the map width in tiles.

## cambc::map::camp_group

### begin

```cpp
auto begin() const noexcept
```

Iterate immutable camp members stored in this physical group.

Evaluate a visible camp before choosing where to farm.

Inputs:
- subject: bc::map::camp_group (a valid instance)

```cpp
auto result = subject.begin();
```

### camps

```cpp
std::span<const camp> camps() const noexcept
```

Get the camp members stored in this physical group.

Evaluate a visible camp before choosing where to farm.

Inputs:
- subject: bc::map::camp_group (a valid instance)

```cpp
auto result = subject.camps();
```

### end

```cpp
auto end() const noexcept
```

Get the past-the-end iterator for this group's camp members.

Evaluate a visible camp before choosing where to farm.

Inputs:
- subject: bc::map::camp_group (a valid instance)

```cpp
auto result = subject.end();
```

### gem_value

```cpp
std::uint32_t gem_value() const noexcept
```

Get the gem reward for capturing this camp group.

Evaluate a visible camp before choosing where to farm.

Inputs:
- subject: bc::map::camp_group (a valid instance)

```cpp
auto result = subject.gem_value();
```

### gold_value

```cpp
std::uint32_t gold_value() const noexcept
```

Get the gold reward for capturing this camp group.

Evaluate a visible camp before choosing where to farm.

Inputs:
- subject: bc::map::camp_group (a valid instance)

```cpp
auto result = subject.gold_value();
```

### max_respawn_timer

```cpp
std::int32_t max_respawn_timer() const noexcept
```

Get the configured camp-group respawn duration in rounds.

Evaluate a visible camp before choosing where to farm.

Inputs:
- subject: bc::map::camp_group (a valid instance)

```cpp
auto result = subject.max_respawn_timer();
```

### operator handle_camp_group_t

```cpp
operator handle_camp_group_t() const noexcept
```

Convert to the strong camp-group identity.

### operator==

```cpp
friend bool operator==(const camp_group& a, const camp_group& b) noexcept
```

Compare camp-group identities.

### spawn_tiles

```cpp
bitboard spawn_tiles() const
```

Build the mask of original camp spawn tiles in this group; computed on every call.

Evaluate a visible camp before choosing where to farm.

Inputs:
- subject: bc::map::camp_group (a valid instance)

```cpp
auto result = subject.spawn_tiles();
```

### spawn_tiles_into

```cpp
void spawn_tiles_into(bitboard& out) const
```

Like `spawn_tiles`, but writes into `out` instead of returning a new board. @param out Destination; its previous contents are overwritten.

Evaluate a visible camp before choosing where to farm.

Inputs:
- subject: bc::map::camp_group (a valid instance)
- out: bitboard&

```cpp
subject.spawn_tiles_into(out);
```

### state

```cpp
camp_group_state state() const
```

Current lifecycle state; remembered state outside current vision is not exposed.

Evaluate a visible camp before choosing where to farm.

Inputs:
- subject: bc::map::camp_group (a valid instance)

```cpp
auto result = subject.state();
```

## cambc::shared_memory_buffer

### operator[]

```cpp
reference operator[](std::size_t offset) const
```

Access one byte; throws if the offset is outside the buffer.

Share an objective using your team's agreed memory layout.

Inputs:
- subject: bc::shared_memory_buffer (a valid instance)
- offset: std::size_t

```cpp
auto result = subject[offset];
```

### read

```cpp
template <class T, std::size_t Extent> requires (std::is_trivially_copyable_v<T> && !std::is_const_v<T>) void read(std::size_t offset, std::span<T, Extent> values) const
```

Read a checked byte range into trivially copyable values. @param offset Byte offset into shared memory. @param values Receives the values; its size sets how many are read.

Share an objective using your team's agreed memory layout.

Inputs:
- subject: bc::shared_memory_buffer (a valid instance)
- offset: std::size_t
- values: std::span<T, Extent>
- T: the payload/value type required by this template

```cpp
subject.read(offset, values);
```

### read

```cpp
void read(std::size_t offset, std::span<std::byte> bytes) const
```

Read a checked byte range into caller-owned storage. @param offset Byte offset into shared memory. @param bytes Receives the bytes; its size sets how many are read.

Reserve byte zero in your team's memory layout for a shared objective ID. All allied bots must agree on its meaning.

```cpp
std::byte objective_id{};
bc::shared_memory.read(0, std::span<std::byte>{&objective_id, 1});
```

### size

```cpp
static std::size_t size()
```

Get the shared-memory capacity in bytes.

### write

```cpp
template <class T, std::size_t Extent> requires std::is_trivially_copyable_v<T> void write(std::size_t offset, std::span<T, Extent> values) const
```

Write the object representations of trivially copyable values to a checked byte range.

Share an objective using your team's agreed memory layout.

Inputs:
- subject: bc::shared_memory_buffer (a valid instance)
- offset: std::size_t
- values: std::span<T, Extent>
- T: the payload/value type required by this template

```cpp
subject.write(offset, values);
```

### write

```cpp
void write(std::size_t offset, std::span<const std::byte> bytes) const
```

Write bytes at an offset; the entire range must fit. @param offset Byte offset into shared memory. @param bytes Bytes to write.

Reserve byte zero in your team's memory layout for a shared objective ID. All allied bots must agree on its meaning.

```cpp
const std::byte objective_id{2};
bc::shared_memory.write(0, std::span<const std::byte>{&objective_id, 1});
```

## cambc::shared_memory_buffer::reference

### operator std::uint8_t

```cpp
operator std::uint8_t() const
```

Read the current byte at this proxy's checked offset.

### operator=

```cpp
reference& operator=(const reference& other)
```



### operator=

```cpp
reference& operator=(std::byte value)
```



### operator=

```cpp
reference& operator=(std::uint8_t value)
```

Write one byte at this proxy's checked offset.

### reference

```cpp
reference(const reference&) = default
```



Share an objective using your team's agreed memory layout.

Inputs:
- value1: const reference&

```cpp
auto result = bc::shared_memory_buffer::reference(value1);
```

## cambc::teleport

### champion

```cpp
cambc::champion champion() const
```

Construct a live view of the champion in the teleport reservation.

Check visible objects before choosing a destination.

Inputs:
- subject: bc::teleport (a valid instance)

```cpp
auto result = subject.champion();
```

### is_ally

```cpp
bool is_ally() const
```

Check whether this teleport belongs to the observing team.

Check visible objects before choosing a destination.

Inputs:
- subject: bc::teleport (a valid instance)

```cpp
auto is_ally = subject.is_ally();
```

### is_enemy

```cpp
bool is_enemy() const
```

Check whether this teleport belongs to the opposing team.

Check visible objects before choosing a destination.

Inputs:
- subject: bc::teleport (a valid instance)

```cpp
auto is_enemy = subject.is_enemy();
```

### operator==

```cpp
friend bool operator==(const teleport&, const teleport&) = default
```

Compare every captured field.

### position

```cpp
vec2 position() const noexcept
```

Get the destination tile captured in this teleport reservation.

Check visible objects before choosing a destination.

Inputs:
- subject: bc::teleport (a valid instance)

```cpp
auto result = subject.position();
```

### remaining_time

```cpp
std::int32_t remaining_time() const noexcept
```

Get the remaining round count captured in this observation.

Check visible objects before choosing a destination.

Inputs:
- subject: bc::teleport (a valid instance)

```cpp
auto result = subject.remaining_time();
```

### team

```cpp
team team() const
```

Get the owning team relative to the authenticated bot.

Check visible objects before choosing a destination.

Inputs:
- subject: bc::teleport (a valid instance)

```cpp
auto result = subject.team();
```

### teleport

```cpp
explicit teleport(detail::teleport_info value)
```

Copy a visible teleport reservation; its metadata remains a snapshot.

Check visible objects before choosing a destination.

Inputs:
- value: detail::teleport_info

```cpp
auto result = bc::teleport(value);
```

### visible_tiles

```cpp
bitboard visible_tiles() const
```

Hypothetical caster vision after arriving at the reserved destination, not current visibility.

Check visible objects before choosing a destination.

Inputs:
- subject: bc::teleport (a valid instance)

```cpp
auto result = subject.visible_tiles();
```

### visible_tiles_into

```cpp
void visible_tiles_into(bitboard& out) const
```

Like `visible_tiles`, but writes into `out` instead of returning a new board. @param out Destination; its previous contents are overwritten.

Check visible objects before choosing a destination.

Inputs:
- subject: bc::teleport (a valid instance)
- out: bitboard&

```cpp
subject.visible_tiles_into(out);
```

## cambc::uninitialized_t

### uninitialized_t

```cpp
explicit uninitialized_t() = default
```

Explicit so that `{}` cannot select the tag by accident.

Use this value in your bot's draft or turn logic.

```cpp
auto result = bc::uninitialized_t();
```

## cambc::unit

### ~unit

```cpp
virtual ~unit() = default
```

Destroy a derived unit view through the shared interface.

### attack_range

```cpp
virtual std::int32_t attack_range() const = 0
```

Attack range in Chebyshev tiles; melee units have range 1.

Evaluate your champion's options during its turn.

```cpp
const bc::unit& subject = bc::self();
auto result = subject.attack_range();
```

### attack_tiles

```cpp
bitboard attack_tiles() const
```

Build the live unit's Chebyshev attack-range mask; does not filter targets or attack availability. Hidden units and design-only champions propagate position/range query errors.

Evaluate your champion's options during its turn.

```cpp
const bc::unit& subject = bc::self();
auto result = subject.attack_tiles();
```

### attack_tiles_into

```cpp
void attack_tiles_into(bitboard& out) const
```

Like `attack_tiles`, but writes into `out` instead of returning a new board. @param out Destination; its previous contents are overwritten.

Evaluate your champion's options during its turn.

Inputs:
- out: bitboard&

```cpp
const bc::unit& subject = bc::self();
subject.attack_tiles_into(out);
```

### buffs

```cpp
buff_collection buffs() const
```

Construct a fresh owning buff collection; previous collections retain their captured values.

Evaluate your champion's options during its turn.

```cpp
const bc::unit& subject = bc::self();
auto result = subject.buffs();
```

### focus

```cpp
virtual float focus() const = 0
```

Query current focus for this visible unit.

Evaluate your champion's options during its turn.

```cpp
const bc::unit& subject = bc::self();
auto result = subject.focus();
```

### health

```cpp
virtual float health() const = 0
```

Query current health; live state requires a visible runtime identity.

Evaluate your champion's options during its turn.

```cpp
const bc::unit& subject = bc::self();
auto result = subject.health();
```

### is_visible

```cpp
virtual bool is_visible() const = 0
```

Check whether live state is available; hidden units and design-only views return false.

Evaluate your champion's options during its turn.

```cpp
const bc::unit& subject = bc::self();
auto is_visible = subject.is_visible();
```

### max_health

```cpp
virtual float max_health() const = 0
```

Query current maximum health; live state requires a visible runtime identity.

Evaluate your champion's options during its turn.

```cpp
const bc::unit& subject = bc::self();
auto result = subject.max_health();
```

### position

```cpp
virtual vec2 position() const = 0
```

Get the tile position; live entity views query current engine state.

Evaluate your champion's options during its turn.

```cpp
const bc::unit& subject = bc::self();
auto result = subject.position();
```

### strength

```cpp
virtual float strength() const = 0
```

Query current strength for this visible unit.

Evaluate your champion's options during its turn.

```cpp
const bc::unit& subject = bc::self();
auto result = subject.strength();
```

## cambc::vec2

### chebyshev_distance

```cpp
constexpr std::int64_t chebyshev_distance(vec2 b) const
```

Get the largest absolute coordinate difference. @param b Other vector.

Compare positions or adjust a movement direction.

Inputs:
- b: vec2

```cpp
bc::vec2 subject{3, 4};
auto result = subject.chebyshev_distance(b);
```

### cross

```cpp
constexpr std::int64_t cross(vec2 b) const
```

Get the signed two-dimensional cross product with a 64-bit result. @param b Other vector.

Compare positions or adjust a movement direction.

Inputs:
- b: vec2

```cpp
bc::vec2 subject{3, 4};
auto result = subject.cross(b);
```

### dot

```cpp
constexpr std::int64_t dot(vec2 b) const
```

Dot and cross products use 64-bit arithmetic. @param b Other vector.

Compare positions or adjust a movement direction.

Inputs:
- b: vec2

```cpp
bc::vec2 subject{3, 4};
auto result = subject.dot(b);
```

### length_squared

```cpp
constexpr std::int64_t length_squared() const
```

Get the squared Euclidean length.

Compare positions or adjust a movement direction.

```cpp
bc::vec2 subject{3, 4};
auto result = subject.length_squared();
```

### operator-

```cpp
constexpr vec2 operator-() const
```

Negate both components.

### operator-

```cpp
friend constexpr vec2 operator-(vec2 a, vec2 b)
```

Subtract b from a.

### operator-=

```cpp
constexpr vec2& operator-=(vec2 b)
```

Subtract a displacement in place.

### operator*

```cpp
friend constexpr vec2 operator*(std::int32_t n, vec2 a)
```

Scale a vector by an integer.

### operator*

```cpp
friend constexpr vec2 operator*(vec2 a, std::int32_t n)
```

Scale a vector by an integer.

### operator*=

```cpp
constexpr vec2& operator*=(std::int32_t n)
```

Scale both components in place.

### operator/

```cpp
friend constexpr vec2 operator/(vec2 a, std::int32_t n)
```

Divide both components, truncating towards zero. Throws std::invalid_argument when n is zero.

### operator/=

```cpp
constexpr vec2& operator/=(std::int32_t n)
```

Divide both components in place, truncating towards zero. Throws std::invalid_argument when n is zero.

### operator+

```cpp
constexpr vec2 operator+() const
```

Return the vector unchanged.

### operator+

```cpp
friend constexpr vec2 operator+(vec2 a, vec2 b)
```

Add two vectors.

### operator+=

```cpp
constexpr vec2& operator+=(vec2 b)
```

Add a displacement in place.

### operator<=>

```cpp
constexpr auto operator<=>(const vec2&) const = default
```

Compare by x, then by y.

### reflect_horizontal

```cpp
constexpr vec2 reflect_horizontal() const
```

Reflect the vector horizontally across the origin.

Compare positions or adjust a movement direction.

```cpp
bc::vec2 subject{3, 4};
auto result = subject.reflect_horizontal();
```

### reflect_vertical

```cpp
constexpr vec2 reflect_vertical() const
```

Reflect the vector vertically across the origin.

Compare positions or adjust a movement direction.

```cpp
bc::vec2 subject{3, 4};
auto result = subject.reflect_vertical();
```

### rotate_180

```cpp
constexpr vec2 rotate_180() const
```

Rotate the vector 180 degrees about the origin.

Compare positions or adjust a movement direction.

```cpp
bc::vec2 subject{3, 4};
auto result = subject.rotate_180();
```

### rotate_ccw

```cpp
constexpr vec2 rotate_ccw() const
```

Rotate the vector 90 degrees counterclockwise about the origin.

Compare positions or adjust a movement direction.

```cpp
bc::vec2 subject{3, 4};
auto result = subject.rotate_ccw();
```

### rotate_cw

```cpp
constexpr vec2 rotate_cw() const
```

Rotate a displacement by a quarter turn.

Compare positions or adjust a movement direction.

```cpp
bc::vec2 subject{3, 4};
auto result = subject.rotate_cw();
```

### vec2

```cpp
constexpr vec2() = default
```

Construct an integer tile vector; the default value is the origin.

Compare positions or adjust a movement direction.

```cpp
auto result = bc::vec2();
```

### vec2

```cpp
constexpr vec2(std::int32_t x, std::int32_t y)
```

Construct an integer tile vector; the default value is the origin.

Compare positions or adjust a movement direction.

Inputs:
- x: std::int32_t
- y: std::int32_t

```cpp
auto result = bc::vec2(x, y);
```

## cambc::vision

### altar_lock_at

```cpp
cambc::altar_lock altar_lock_at(vec2 position)
```

Prefer the allied lock if both teams have a lock on this altar. @param position Tile to query.

Check visible objects before choosing a destination.

Inputs:
- position: vec2

```cpp
auto result = bc::vision::altar_lock_at(position);
```

### altar_lock_tiles

```cpp
bitboard altar_lock_tiles()
```

Query current altar-lock occupancy for the selected team or both teams.

Check visible objects before choosing a destination.

```cpp
auto result = bc::vision::altar_lock_tiles();
```

### altar_lock_tiles

```cpp
bitboard altar_lock_tiles(team side)
```

Query current altar-lock occupancy for the selected team or both teams. @param side Team to include.

Check visible objects before choosing a destination.

Inputs:
- side: team

```cpp
auto result = bc::vision::altar_lock_tiles(side);
```

### altar_lock_tiles_into

```cpp
void altar_lock_tiles_into(bitboard& out)
```

Like `altar_lock_tiles()`, but writes into `out`. @param out Destination; its previous contents are overwritten.

Check visible objects before choosing a destination.

Inputs:
- out: bitboard&

```cpp
bc::vision::altar_lock_tiles_into(out);
```

### altar_lock_tiles_into

```cpp
void altar_lock_tiles_into(team side, bitboard& out)
```

Like `altar_lock_tiles`, but writes into `out` instead of returning a new board. @param side Team to include. @param out Destination; its previous contents are overwritten.

Check visible objects before choosing a destination.

Inputs:
- side: team
- out: bitboard&

```cpp
bc::vision::altar_lock_tiles_into(side, out);
```

### altar_locks

```cpp
std::vector<cambc::altar_lock> altar_locks()
```

Get fresh, owning visible altar-lock snapshots, optionally filtered by team.

Check visible objects before choosing a destination.

```cpp
auto result = bc::vision::altar_locks();
```

### altar_locks

```cpp
std::vector<cambc::altar_lock> altar_locks(team side)
```

Get fresh, owning visible altar-lock snapshots, optionally filtered by team. @param side Team to include.

Check visible objects before choosing a destination.

Inputs:
- side: team

```cpp
auto result = bc::vision::altar_locks(side);
```

### camp_at

```cpp
cambc::camp camp_at(vec2 position)
```

Find a currently visible camp. @param position Current tile of the camp.

Check visible objects before choosing a destination.

Inputs:
- position: vec2

```cpp
auto result = bc::vision::camp_at(position);
```

### camp_groups

```cpp
std::vector<map::camp_group> camp_groups()
```

A group is currently visible when any original spawn tile is in effective vision, even if empty.

Check visible objects before choosing a destination.

```cpp
auto result = bc::vision::camp_groups();
```

### camp_tiles

```cpp
bitboard camp_tiles()
```

Get camp occupancy from the current perspective.

Check visible objects before choosing a destination.

```cpp
auto result = bc::vision::camp_tiles();
```

### camp_tiles_into

```cpp
void camp_tiles_into(bitboard& out)
```

Like `camp_tiles`, but writes into `out` instead of returning a new board. @param out Destination; its previous contents are overwritten.

Check visible objects before choosing a destination.

Inputs:
- out: bitboard&

```cpp
bc::vision::camp_tiles_into(out);
```

### camps

```cpp
std::vector<cambc::camp> camps()
```

Get an owning list of currently visible camps, recomputed each call.

Check visible objects before choosing a destination.

```cpp
auto result = bc::vision::camps();
```

### champion_at

```cpp
cambc::champion champion_at(vec2 position)
```

Create a fresh view of the currently visible champion at a position. @param position Tile to query.

Check visible objects before choosing a destination.

Inputs:
- position: vec2

```cpp
auto result = bc::vision::champion_at(position);
```

### champion_tiles

```cpp
bitboard champion_tiles()
```

Get visible champion occupancy for both sides.

Check visible objects before choosing a destination.

```cpp
auto result = bc::vision::champion_tiles();
```

### champion_tiles

```cpp
bitboard champion_tiles(team side)
```

Get visible champion occupancy for one side. @param side Team to include.

Check visible objects before choosing a destination.

Inputs:
- side: team

```cpp
auto result = bc::vision::champion_tiles(side);
```

### champion_tiles_into

```cpp
void champion_tiles_into(bitboard& out)
```

Like `champion_tiles()`, but writes into `out`. @param out Destination; its previous contents are overwritten.

Check visible objects before choosing a destination.

Inputs:
- out: bitboard&

```cpp
bc::vision::champion_tiles_into(out);
```

### champion_tiles_into

```cpp
void champion_tiles_into(team side, bitboard& out)
```

Like `champion_tiles`, but writes into `out` instead of returning a new board. @param side Team to include. @param out Destination; its previous contents are overwritten.

Check visible objects before choosing a destination.

Inputs:
- side: team
- out: bitboard&

```cpp
bc::vision::champion_tiles_into(side, out);
```

### champions

```cpp
std::vector<cambc::champion> champions()
```

Get a fresh champion list for the current view and optional team filter.

Check visible objects before choosing a destination.

```cpp
auto result = bc::vision::champions();
```

### champions

```cpp
std::vector<cambc::champion> champions(team side)
```

Get an owning list of currently visible champions, recomputed each call. @param side Team to include.

Check visible objects before choosing a destination.

Inputs:
- side: team

```cpp
auto result = bc::vision::champions(side);
```

### has_altar_lock

```cpp
bool has_altar_lock(vec2 position)
```

Check for a visible altar lock for either team; missing or hidden locks return false. @param position Tile to query.

Check visible objects before choosing a destination.

Inputs:
- position: vec2

```cpp
auto has_altar_lock = bc::vision::has_altar_lock(position);
```

### has_camp

```cpp
bool has_camp(vec2 position)
```

False on hidden or empty tiles; invalid positions retain the engine's error. @param position Tile to query.

Check visible objects before choosing a destination.

Inputs:
- position: vec2

```cpp
auto has_camp = bc::vision::has_camp(position);
```

### has_champion

```cpp
bool has_champion(vec2 position)
```

Check visible champion occupancy at a tile; invalid positions retain engine errors. @param position Tile to query.

Check visible objects before choosing a destination.

Inputs:
- position: vec2

```cpp
auto has_champion = bc::vision::has_champion(position);
```

### has_teleport

```cpp
bool has_teleport(vec2 position)
```

Check whether a tile is occupied by a visible teleport reservation. @param position Tile to query.

Check visible objects before choosing a destination.

Inputs:
- position: vec2

```cpp
auto has_teleport = bc::vision::has_teleport(position);
```

### has_ward

```cpp
bool has_ward(vec2 position, team side)
```

Check ward occupancy for `side`; hidden or absent wards return false. @param position Tile to query. @param side Team the ward must belong to.

Check visible objects before choosing a destination.

Inputs:
- position: vec2
- side: team

```cpp
auto has_ward = bc::vision::has_ward(position, side);
```

### has_ward

```cpp
bool has_ward(vec2 position)
```

Check ward occupancy at a tile; hidden or absent wards return false. @param position Tile to query.

Check visible objects before choosing a destination.

Inputs:
- position: vec2

```cpp
auto has_ward = bc::vision::has_ward(position);
```

### teleport_at

```cpp
teleport teleport_at(vec2 position)
```

Get the visible teleport reservation on a tile. @param position Tile to query.

Check visible objects before choosing a destination.

Inputs:
- position: vec2

```cpp
auto result = bc::vision::teleport_at(position);
```

### teleport_tiles

```cpp
bitboard teleport_tiles()
```

Query current visible teleport-reservation occupancy.

Check visible objects before choosing a destination.

```cpp
auto result = bc::vision::teleport_tiles();
```

### teleport_tiles_into

```cpp
void teleport_tiles_into(bitboard& out)
```

Like `teleport_tiles`, but writes into `out` instead of returning a new board. @param out Destination; its previous contents are overwritten.

Check visible objects before choosing a destination.

Inputs:
- out: bitboard&

```cpp
bc::vision::teleport_tiles_into(out);
```

### teleports

```cpp
std::vector<teleport> teleports()
```

Get fresh, owning visible teleport-reservation snapshots.

Check visible objects before choosing a destination.

```cpp
auto result = bc::vision::teleports();
```

### visible_tiles

```cpp
bitboard visible_tiles()
```

Query current visible tiles.

Check visible objects before choosing a destination.

```cpp
auto result = bc::vision::visible_tiles();
```

### visible_tiles_into

```cpp
void visible_tiles_into(bitboard& out)
```

Like `visible_tiles`, but writes into `out` instead of returning a new board. @param out Destination; its previous contents are overwritten.

Check visible objects before choosing a destination.

Inputs:
- out: bitboard&

```cpp
bc::vision::visible_tiles_into(out);
```

### ward_at

```cpp
cambc::ward ward_at(vec2 position, team side)
```

Get the ordinary ward at a tile for one team. @param position Tile to query. @param side Team the ward must belong to.

Check visible objects before choosing a destination.

Inputs:
- position: vec2
- side: team

```cpp
auto result = bc::vision::ward_at(position, side);
```

### ward_at

```cpp
cambc::ward ward_at(vec2 position)
```

Get the ordinary ward at a tile. @param position Tile to query.

Check visible objects before choosing a destination.

Inputs:
- position: vec2

```cpp
auto result = bc::vision::ward_at(position);
```

### ward_tiles

```cpp
bitboard ward_tiles()
```

Query visible ordinary-ward occupancy for both teams.

Check visible objects before choosing a destination.

```cpp
auto result = bc::vision::ward_tiles();
```

### ward_tiles

```cpp
bitboard ward_tiles(team side)
```

Query visible ordinary-ward occupancy for one team. @param side Team to include.

Check visible objects before choosing a destination.

Inputs:
- side: team

```cpp
auto result = bc::vision::ward_tiles(side);
```

### ward_tiles_into

```cpp
void ward_tiles_into(bitboard& out)
```

Like `ward_tiles()`, but writes into `out`. @param out Destination; its previous contents are overwritten.

Check visible objects before choosing a destination.

Inputs:
- out: bitboard&

```cpp
bc::vision::ward_tiles_into(out);
```

### ward_tiles_into

```cpp
void ward_tiles_into(team side, bitboard& out)
```

Like `ward_tiles`, but writes into `out` instead of returning a new board. @param side Team to include. @param out Destination; its previous contents are overwritten.

Check visible objects before choosing a destination.

Inputs:
- side: team
- out: bitboard&

```cpp
bc::vision::ward_tiles_into(side, out);
```

### wards

```cpp
std::vector<cambc::ward> wards()
```

Fresh visible ordinary-ward observations; overlapping records remain distinct.

Check visible objects before choosing a destination.

```cpp
auto result = bc::vision::wards();
```

### wards

```cpp
std::vector<cambc::ward> wards(team side)
```

Get fresh ordinary-ward observations filtered by team. @param side Team to include.

Check visible objects before choosing a destination.

Inputs:
- side: team

```cpp
auto result = bc::vision::wards(side);
```

## cambc::ward

### is_ally

```cpp
bool is_ally() const noexcept
```

Check whether this ward belongs to the observing team.

Check visible objects before choosing a destination.

Inputs:
- subject: bc::ward (a valid instance)

```cpp
auto is_ally = subject.is_ally();
```

### is_enemy

```cpp
bool is_enemy() const noexcept
```

Check whether this ward belongs to the opposing team.

Check visible objects before choosing a destination.

Inputs:
- subject: bc::ward (a valid instance)

```cpp
auto is_enemy = subject.is_enemy();
```

### operator==

```cpp
friend bool operator==(const ward&, const ward&) = default
```

Compare every captured field.

### position

```cpp
vec2 position() const noexcept
```

Get the ward's tile.

Check visible objects before choosing a destination.

Inputs:
- subject: bc::ward (a valid instance)

```cpp
auto result = subject.position();
```

### remaining_time

```cpp
std::optional<std::int32_t> remaining_time() const noexcept
```

Get the remaining lifetime in rounds; empty for an unlimited lifetime.

Check visible objects before choosing a destination.

Inputs:
- subject: bc::ward (a valid instance)

```cpp
auto result = subject.remaining_time();
```

### team

```cpp
cambc::team team() const noexcept
```

Get the owning team relative to the authenticated bot.

Check visible objects before choosing a destination.

Inputs:
- subject: bc::ward (a valid instance)

```cpp
auto result = subject.team();
```

### visible_tiles

```cpp
bitboard visible_tiles() const
```

Hypothetical vision from this ward's position, ignoring current team visibility.

Check visible objects before choosing a destination.

Inputs:
- subject: bc::ward (a valid instance)

```cpp
auto result = subject.visible_tiles();
```

### visible_tiles_from

```cpp
static bitboard visible_tiles_from(vec2 position)
```

Query the mask for ordinary-ward vision at a placement tile. @param position Hypothetical ward tile.

Check visible objects before choosing a destination.

Inputs:
- position: vec2

```cpp
auto result = bc::ward::visible_tiles_from(position);
```

### visible_tiles_from_into

```cpp
static void visible_tiles_from_into(vec2 position, bitboard& out)
```

Like `visible_tiles_from`, but writes into `out` instead of returning a new board. @param position Hypothetical ward tile. @param out Destination; its previous contents are overwritten.

Check visible objects before choosing a destination.

Inputs:
- position: vec2
- out: bitboard&

```cpp
bc::ward::visible_tiles_from_into(position, out);
```

### visible_tiles_into

```cpp
void visible_tiles_into(bitboard& out) const
```

Like `visible_tiles`, but writes into `out`. @param out Destination; its previous contents are overwritten.

Check visible objects before choosing a destination.

Inputs:
- subject: bc::ward (a valid instance)
- out: bitboard&

```cpp
subject.visible_tiles_into(out);
```

## std

### operator()

```cpp
std::size_t operator()(cambc::vec2 v) const noexcept
```

Hash a tile position.

Use this value in your bot's draft or turn logic.

Inputs:
- subject: std (a valid instance)
- v: cambc::vec2

```cpp
auto result = subject () v;
```

### operator()

```cpp
std::size_t operator()(const cambc::camp& c) const noexcept
```

Hash a camp identity.

Use this value in your bot's draft or turn logic.

Inputs:
- subject: std (a valid instance)
- c: const cambc::camp&

```cpp
auto result = subject () c;
```

### operator()

```cpp
std::size_t operator()(const cambc::champion& c) const noexcept
```

Hash a champion identity.

Use this value in your bot's draft or turn logic.

Inputs:
- subject: std (a valid instance)
- c: const cambc::champion&

```cpp
auto result = subject () c;
```

### operator()

```cpp
std::size_t operator()(const cambc::map::camp_group& g) const noexcept
```

Hash a camp-group identity.

Use this value in your bot's draft or turn logic.

Inputs:
- subject: std (a valid instance)
- g: const cambc::map::camp_group&

```cpp
auto result = subject () g;
```
