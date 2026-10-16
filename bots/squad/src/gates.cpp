// gates.cpp — learned advisory gates for fight/flight and channel decisions.
// The 117-float feature vector mirrors the replay featurizer exactly: entity
// slots are nearest-first by squared euclidean distance, dx = target - self.
// Rows the SDK cannot supply (champion kills, camp capture team, ward/spell
// entries) were constant in the training corpus and emit constants here.
#include <cambc.h>
#include "gates.h"
#include "gate_atk5.h"
#include "gate_chan10.h"
#include <algorithm>
#include <cmath>
#include <iostream>
#include <vector>

using cambc::team;
using cambc::vec2;

namespace {

constexpr int kFeatures = 117;
constexpr int kHidden = 32;

struct FeatureSet {
    float v[kFeatures];
    int foes;
};

void push_rel(float* v, int& n, int dx, int dy, float hp, float maxhp) {
    v[n++] = dx / 48.0f;
    v[n++] = dy / 48.0f;
    v[n++] = static_cast<float>(std::sqrt(dx * dx + dy * dy)) / 12.0f;
    v[n++] = hp / std::max(1.0f, maxhp);
}

struct Rel {
    int dx, dy;
    float hp, maxhp;
    int extra;
};

std::vector<Rel> champions_rel(team side, const cambc::champion& me, vec2 here) {
    std::vector<Rel> out;
    for (const auto& u : cambc::vision::champions(side)) {
        try {
            if (u == me || !u.is_alive())
                continue;
            const auto at = u.position();
            out.push_back({at.x - here.x, at.y - here.y, u.health(), u.max_health(), 0});
        } catch (const std::exception&) {
        }
    }
    std::sort(out.begin(), out.end(), [](const Rel& a, const Rel& b) {
        return a.dx * a.dx + a.dy * a.dy < b.dx * b.dx + b.dy * b.dy;
    });
    return out;
}

void featurize(FeatureSet& fs) {
    float* v = fs.v;
    const auto& me = cambc::self();
    const auto here = me.position();
    int n = 0;
    v[n++] = here.x / 48.0f;
    v[n++] = here.y / 48.0f;
    v[n++] = me.health() / std::max(1.0f, me.max_health());
    v[n++] = me.gem_count() / 40.0f;
    v[n++] = me.gold_count() / 2000.0f;
    v[n++] = 0.0f; // kills: the SDK does not report a champion's kill count
    v[n++] = cambc::round_number() / 2000.0f;
    v[n++] = me.buffs().has_stun() ? 1.0f : 0.0f;
    for (int t = 0; t < 12; ++t)
        v[n++] = static_cast<int>(me.champion_name()) == t ? 1.0f : 0.0f;

    const auto items = me.item_count();
    int nz = 0;
    for (const auto c : items)
        nz += c != 0;
    v[n++] = nz / 20.0f;
    const auto* counts = items.ptr();
    for (int k = 0; k < 8; ++k)
        v[n++] = std::min<float>(static_cast<float>(counts[k]), 20.0f) / 20.0f;

    const auto& cons = me.consumables();
    v[n++] = cons[cambc::consumable_type::tp_crystal].count() / 4.0f;
    v[n++] = cons[cambc::consumable_type::altar_lock].count() / 4.0f;
    v[n++] = 0.0f;
    v[n++] = 0.0f;

    const auto abilities = me.abilities();
    for (int k = 0; k < 2; ++k)
        v[n++] = abilities[k].valid_targets().is_empty() ? 0.0f : 1.0f;

    const auto foes = champions_rel(team::enemy, me, here);
    const auto allies = champions_rel(team::ally, me, here);
    fs.foes = static_cast<int>(foes.size());
    for (int k = 0; k < 4; ++k)
        if (k < static_cast<int>(foes.size()))
            push_rel(v, n, foes[k].dx, foes[k].dy, foes[k].hp, foes[k].maxhp);
        else
            n += 4;
    for (int k = 0; k < 4; ++k)
        if (k < static_cast<int>(allies.size()))
            push_rel(v, n, allies[k].dx, allies[k].dy, allies[k].hp, allies[k].maxhp);
        else
            n += 4;

    std::vector<Rel> camps;
    for (const auto& c : cambc::vision::camps()) {
        const auto at = c.position();
        camps.push_back({at.x - here.x, at.y - here.y, 0.0f, 0.0f, 0});
    }
    std::sort(camps.begin(), camps.end(), [](const Rel& a, const Rel& b) {
        return a.dx * a.dx + a.dy * a.dy < b.dx * b.dx + b.dy * b.dy;
    });
    for (int k = 0; k < 6; ++k) {
        if (k < static_cast<int>(camps.size())) {
            const auto& c = camps[k];
            v[n++] = c.dx / 48.0f;
            v[n++] = c.dy / 48.0f;
            v[n++] = static_cast<float>(std::sqrt(c.dx * c.dx + c.dy * c.dy)) / 12.0f;
        } else {
            n += 3;
        }
        // capture team and reward readiness: not exposed per camp, constant
        // (neutral, never ready) — the trained rows carry no weight.
        v[n++] = 0.0f;
        v[n++] = 0.0f;
    }

    std::vector<Rel> wards;
    for (const auto& w : cambc::vision::wards()) {
        const auto at = w.position();
        wards.push_back({at.x - here.x, at.y - here.y, 0.0f, 0.0f,
                         w.team() == team::ally ? 0 : 1});
    }
    std::sort(wards.begin(), wards.end(), [](const Rel& a, const Rel& b) {
        return a.dx * a.dx + a.dy * a.dy < b.dx * b.dx + b.dy * b.dy;
    });
    for (int k = 0; k < 3; ++k) {
        if (k < static_cast<int>(wards.size())) {
            const auto& w = wards[k];
            v[n++] = w.dx / 48.0f;
            v[n++] = w.dy / 48.0f;
            v[n++] = static_cast<float>(w.extra);
        } else {
            n += 3;
        }
    }

    // No spell entity exists in the SDK — the spell slots are constant zero.
    for (int k = 0; k < 9; ++k)
        v[n++] = 0.0f;

    v[n++] = cambc::global_submitted_gems(team::ally) / 1200.0f;
    v[n++] = cambc::global_submitted_gems(team::enemy) / 1200.0f;
}

const FeatureSet& features() {
    static std::uint32_t round = 0xFFFFFFFF;
    static vec2 pos{-1, -1};
    static FeatureSet fs;
    const auto& me = cambc::self();
    if (cambc::round_number() != round || me.position() != pos) {
        featurize(fs);
        round = cambc::round_number();
        pos = me.position();
    }
    return fs;
}

float infer(const float* x, const float w0[kFeatures][kHidden], const float* b0,
            const float* w1, float b1) {
    float h[kHidden];
    for (int i = 0; i < kHidden; ++i) {
        float s = b0[i];
        for (int j = 0; j < kFeatures; ++j)
            s += x[j] * w0[j][i];
        h[i] = std::max(0.0f, s);
    }
    float s = b1;
    for (int i = 0; i < kHidden; ++i)
        s += h[i] * w1[i];
    return 1.0f / (1.0f + std::exp(-s));
}

void log_gate(const char* tag, float p, float hp) {
    std::cout << tag << ' ' << cambc::round_number() << ' '
              << static_cast<int>(cambc::self().champion_name()) << ' ' << p << ' ' << hp << '\n';
}

} // namespace

namespace gates {

bool veto_fight() {
    const auto& me = cambc::self();
    const auto& fs = features();
    if (fs.foes == 0)
        return false;
    const float hp = me.health() / me.max_health();
    const float p = infer(fs.v, atk5_w0, atk5_b0, atk5_w1, atk5_b1);
    log_gate("GATE5", p, hp);
    return p < 0.5f && hp < 0.6f;
}

bool allow_channel() {
    const auto& me = cambc::self();
    if (me.gem_count() < 5 && me.health() >= me.max_health() * 0.4f)
        return true;
    const auto& fs = features();
    if (fs.foes == 0)
        return true;
    const float p = infer(fs.v, chan10_w0, chan10_b0, chan10_w1, chan10_b1);
    log_gate("GATE10", p, me.health() / me.max_health());
    return p >= 0.5f;
}

} // namespace gates
