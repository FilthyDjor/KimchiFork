/**
 * @file
 * @brief Damage-dealing spells not already handled elsewhere.
 *           Other targeted spells are covered in spl-zap.cc.
**/

#include "AppHdr.h"


#include "spl-damage.h"

#include <functional>

#include "act-iter.h"
#include "areas.h"
#include "attack.h"
#include "beam.h"
#include "butcher.h"
#include "cloud.h"
#include "colour.h"
#include "coordit.h"
#include "directn.h"
#include "english.h"
#include "env.h"
#include "fight.h"
#include "fineff.h"
#include "food.h"
#include "fprop.h"
#include "god-abil.h"
#include "god-conduct.h"
#include "god-passive.h"
#include "invent.h"
#include "item-name.h"
#include "items.h"
#include "level-state-type.h"
#include "los.h"
#include "losglobal.h"
#include "macro.h"
#include "mapmark.h"
#include "message.h"
#include "misc.h"
#include "mon-behv.h"
#include "mon-death.h"
#include "mon-tentacle.h"
#include "mutation.h"
#include "ouch.h"
#include "pakellas.h"
#include "prompt.h"
#include "random.h"
#include "religion.h"
#include "rot.h"
#include "shout.h"
#include "spl-goditem.h"
#include "spl-summoning.h"
#include "spl-util.h"
#include "spl-zap.h"
#include "stepdown.h"
#include "stringutil.h"
#include "target.h"
#include "terrain.h"
#include "transform.h"
#include "unicode.h"
#include "viewchar.h"
#include "view.h"
#include "xp-evoker-data.h"

void setup_fire_storm(const actor *source, int pow, bolt &beam)
{
    zappy(ZAP_FIRE_STORM, pow, source->is_monster(), beam);
    beam.ex_size      = 2 + (random2(1000) < pow);
    beam.source_id    = source->mid;
    // XXX: Should this be KILL_MON_MISSILE?
    beam.thrower      =
        source->is_player() ? KILL_YOU_MISSILE : KILL_MON;
    beam.aux_source.clear();
    beam.is_tracer    = false;
    beam.origin_spell = SPELL_FIRE_STORM;
}

spret cast_fire_storm(int pow, bolt &beam, bool fail)
{
    if (grid_distance(beam.target, beam.source) > beam.range)
    {
        mpr("That is beyond the maximum range.");
        return spret::abort;
    }

    if (cell_is_solid(beam.target))
    {
        const char *feat = feat_type_name(grd(beam.target));
        mprf("You can't place the storm on %s.", article_a(feat).c_str());
        return spret::abort;
    }

    setup_fire_storm(&you, pow, beam);

    bolt tempbeam = beam;
    tempbeam.ex_size = (pow > 76) ? 3 : 2;
    tempbeam.is_tracer = true;

    tempbeam.explode(false);
    if (tempbeam.beam_cancelled)
        return spret::abort;

    fail_check();

    beam.apply_beam_conducts();
    beam.refine_for_explosion();
    beam.explode(false);

    viewwindow();
    return spret::success;
}

// No setup/cast split here as monster damnation is completely different.
// XXX make this not true
bool cast_smitey_damnation(int pow, bolt &beam)
{
    beam.name              = "damnation";
    beam.aux_source        = "damnation";
    beam.ex_size           = 1;
    beam.flavour           = BEAM_DAMNATION;
    beam.real_flavour      = beam.flavour;
    beam.glyph             = dchar_glyph(DCHAR_FIRED_BURST);
    beam.colour            = LIGHTRED;
    beam.source_id         = MID_PLAYER;
    beam.thrower           = KILL_YOU;
    beam.obvious_effect    = false;
    beam.pierce            = false;
    beam.is_explosion      = true;
    beam.ench_power        = pow;      // used for radius
    beam.hit               = 20 + pow / 10;
    beam.damage            = calc_dice(6, 30 + pow);
    beam.attitude          = ATT_FRIENDLY;
    beam.friend_info.count = 0;
    beam.is_tracer         = true;

    beam.explode(false);

    if (beam.beam_cancelled)
    {
        canned_msg(MSG_OK);
        return false;
    }

    mpr("You call forth a pillar of damnation!");

    beam.is_tracer = false;
    beam.in_explosion_phase = false;
    beam.explode(true);

    return true;
}

string desc_chain_lightning_dam(int pow)
{
    // Damage is 5d(9.2 + pow / 30), but if lots of targets are around
    // it can hit the player precisely once at very low (e.g. 1) power
    // and deal 5 damage.
    int min = 5;

    // Max damage per bounce is 46 + pow / 6; in the worst case every other
    // bounce hits the player, losing 8 pow on the bounce away and 8 on the
    // bounce back for a total of 16; thus, for n bounces, it's:
    // (46 + pow/6) * n less 16/6 times the (n - 1)th triangular number.
    int n = (pow + 15) / 16;
    int max = (46 + (pow / 6)) * n - 4 * n * (n - 1) / 3;

    return make_stringf("%d-%d", min, max);
}

// XXX no friendly check
spret cast_chain_spell(spell_type spell_cast, int pow,
                            const actor *caster, bool fail)
{
    fail_check();
    bolt beam;

    // initialise beam structure
    switch (spell_cast)
    {
        case SPELL_CHAIN_LIGHTNING:
            beam.name           = "lightning arc";
            beam.aux_source     = "chain lightning";
            beam.glyph          = dchar_glyph(DCHAR_FIRED_ZAP);
            beam.flavour        = BEAM_ELECTRICITY;
            break;
        case SPELL_CHAIN_OF_CHAOS:
            beam.name           = "arc of chaos";
            beam.aux_source     = "chain of chaos";
            beam.glyph          = dchar_glyph(DCHAR_FIRED_ZAP);
            beam.flavour        = BEAM_CHAOS;
            break;
        default:
            die("buggy chain spell %d cast", spell_cast);
            break;
    }
    beam.source_id      = caster->mid;
    beam.thrower        = caster->is_player() ? KILL_YOU_MISSILE : KILL_MON_MISSILE;
    beam.range          = 8;
    beam.hit            = AUTOMATIC_HIT;
    beam.obvious_effect = true;
    beam.pierce         = false;       // since we want to stop at our target
    beam.is_explosion   = false;
    beam.is_tracer      = false;
    beam.origin_spell   = spell_cast;

    if (const monster* mons = caster->as_monster())
        beam.source_name = mons->name(DESC_PLAIN, true);

    bool first = true;
    coord_def source, target;

    for (source = caster->pos(); pow > 0;
         pow -= 8 + random2(13), source = target)
    {
        // infinity as far as this spell is concerned
        // (Range - 1) is used because the distance is randomised and
        // may be shifted by one.
        int min_dist = LOS_DEFAULT_RANGE - 1;

        int dist;
        int count = 0;

        target.x = -1;
        target.y = -1;

        for (monster_iterator mi; mi; ++mi)
        {
            if (invalid_monster(*mi))
                continue;

            // Don't arc to things we cannot hit.
            if (beam.ignores_monster(*mi))
                continue;

            dist = grid_distance(source, mi->pos());

            // check for the source of this arc
            if (!dist)
                continue;

            // randomise distance (arcs don't care about a couple of feet)
            dist += (random2(3) - 1);

            // always ignore targets further than current one
            if (dist > min_dist)
                continue;

            if (!cell_see_cell(source, mi->pos(), LOS_SOLID)
                || !cell_see_cell(caster->pos(), mi->pos(), LOS_SOLID_SEE))
            {
                continue;
            }

            // check for actors along the arc path
            ray_def ray;
            if (!find_ray(source, mi->pos(), ray, opc_solid))
                continue;

            while (ray.advance())
                if (actor_at(ray.pos()))
                    break;

            if (ray.pos() != mi->pos())
                continue;

            count++;

            if (dist < min_dist)
            {
                // switch to looking for closer targets (but not always)
                if (!one_chance_in(10))
                {
                    min_dist = dist;
                    target = mi->pos();
                    count = 0;
                }
            }
            else if (target.x == -1 || one_chance_in(count))
            {
                // either first target, or new selected target at
                // min_dist == dist.
                target = mi->pos();
            }
        }

        // now check if the player is a target
        dist = grid_distance(source, you.pos());

        if (dist)       // i.e., player was not the source
        {
            // distance randomised (as above)
            dist += (random2(3) - 1);

            // select player if only, closest, or randomly selected
            if ((target.x == -1
                    || dist < min_dist
                    || (dist == min_dist && one_chance_in(count + 1)))
                && cell_see_cell(source, you.pos(), LOS_SOLID))
            {
                target = you.pos();
            }
        }

        const bool see_source = you.see_cell(source);
        const bool see_targ   = you.see_cell(target);

        if (target.x == -1)
        {
            if (see_source)
                mprf("The %s grounds out.", beam.name.c_str());

            break;
        }

        // Trying to limit message spamming here so we'll only mention
        // the thunder at the start or when it's out of LoS.
        switch (spell_cast)
        {
            case SPELL_CHAIN_LIGHTNING:
            {
                const char* msg = "You hear a mighty clap of thunder!";
                noisy(spell_effect_noise(SPELL_CHAIN_LIGHTNING), source,
                      (first || !see_source) ? msg : nullptr);
                break;
            }
            case SPELL_CHAIN_OF_CHAOS:
                if (first && see_source)
                    mpr("A swirling arc of seething chaos appears!");
                break;
            default:
                break;
        }
        first = false;

        if (see_source && !see_targ)
            mprf("The %s arcs out of your line of sight!", beam.name.c_str());
        else if (!see_source && see_targ)
            mprf("The %s suddenly appears!", beam.name.c_str());

        beam.source = source;
        beam.target = target;
        switch (spell_cast)
        {
            case SPELL_CHAIN_LIGHTNING:
                beam.colour = LIGHTBLUE;
                beam.damage = caster->is_player()
                    ? calc_dice(5, 10 + pow * 2 / 3)
                    : calc_dice(5, 46 + pow / 6);
                break;
            case SPELL_CHAIN_OF_CHAOS:
                beam.colour       = ETC_RANDOM;
                beam.ench_power   = pow;
                beam.damage       = calc_dice(3, 5 + pow / 6);
                beam.real_flavour = BEAM_CHAOS;
                beam.flavour      = BEAM_CHAOS;
            default:
                break;
        }

        // Be kinder to the caster.
        if (target == caster->pos())
        {
            // This should not hit the caster, too scary as a player effect and
            // too kind to the player as a monster effect.
            if (spell_cast == SPELL_CHAIN_OF_CHAOS)
            {
                beam.real_flavour = BEAM_VISUAL;
                beam.flavour      = BEAM_VISUAL;
            }

            // Reduce damage when the spell arcs to the caster.
            beam.damage.num = max(1, beam.damage.num / 2);
            beam.damage.size = max(3, beam.damage.size / 2);
        }
        beam.fire();
    }

    return spret::success;
}

/*
 * Handle the application of damage from a player spell that doesn't apply these
 * through struct bolt. This applies any Zin sancuary violation and can apply
 * god conducts as well.
 * @param mon          The monster.
 * @param damage       The damage to apply, if any. Regardless of damage done,
 *                     the monster will have death cleanup applied via
 *                     monster_die() if it's now dead.
 * @param flavour      The beam flavour of damage.
 * @param god_conducts If true, apply any god conducts. Some callers need to
 *                     apply effects prior to damage that might kill the
 *                     monster, hence handle conducts on their own.
*/
static void _player_hurt_monster(monster &mon, int damage, beam_type flavour,
                                 bool god_conducts = true)
{
    if (is_sanctuary(you.pos()) || is_sanctuary(mon.pos()))
        remove_sanctuary(true);

    if (god_conducts && god_protects(&mon, false))
        return;

    god_conduct_trigger conducts[3];
    if (god_conducts)
        set_attack_conducts(conducts, mon, you.can_see(mon));

    // Don't let monster::hurt() do death cleanup here. We're handling death
    // cleanup at the end to cover cases where we've done no damage and the
    // monster is dead from previous effects.
    if (damage)
    {
        majin_bo_vampirism(mon, min(damage, mon.stat_hp()));
        mon.hurt(&you, damage, flavour, KILLED_BY_BEAM);
    }
    
    if (mon.alive())
    {
        behaviour_event(&mon, ME_WHACK, &you);

        if (damage && you.can_see(mon))
            print_wounds(mon);
    }
    // monster::hurt() wasn't called, so we do death cleanup.
    else if (!damage)
        monster_die(mon, KILL_YOU, NON_MONSTER);
}

static counted_monster_list _counted_monster_list_from_vector(
    vector<monster *> affected_monsters)
{
    counted_monster_list mons;
    for (auto mon : affected_monsters)
        mons.add(mon);
    return mons;
}

static bool _drain_lifeable(const actor* agent, const actor* act)
{
    if (act->res_negative_energy() >= 3)
        return false;

    if (!agent)
        return true;

    const monster* mons = agent->as_monster();
    const monster* m = act->as_monster();

    return !(agent->is_player() && act->wont_attack()
             || mons && act->is_player() && mons->wont_attack()
             || mons && m && mons_atts_aligned(mons->attitude, m->attitude));
}

static void _los_spell_pre_damage_monsters(const actor* agent,
                                           vector<monster *> affected_monsters,
                                           const char *verb)
{
    // Filter out affected monsters that we don't know for sure are there
    vector<monster*> seen_monsters;
    for (monster *mon : affected_monsters)
        if (you.can_see(*mon))
            seen_monsters.push_back(mon);

    if (!seen_monsters.empty())
    {
        counted_monster_list mons_list =
            _counted_monster_list_from_vector(seen_monsters);
        const string message = make_stringf("%s %s %s.",
                mons_list.describe(DESC_THE).c_str(),
                conjugate_verb("be", mons_list.count() > 1).c_str(), verb);
        if (strwidth(message) < get_number_of_cols() - 2)
            mpr(message);
        else
        {
            // Exclamation mark to suggest that a lot of creatures were
            // affected.
            mprf("The monsters around %s are %s!",
                agent && agent->is_monster() && you.can_see(*agent)
                ? agent->as_monster()->name(DESC_THE).c_str()
                : "you", verb);
        }
    }
}

static int _los_spell_damage_player(const actor* agent, bolt &beam,
                                    bool actual)
{
    int hurted = actual ? beam.damage.roll()
                        // Monsters use the average for foe calculations.
                        : (1 + beam.damage.num * beam.damage.size) / 2;
    hurted = check_your_resists(hurted, beam.flavour, beam.name, 0,
            // Drain life doesn't apply drain effects.
            actual && beam.origin_spell != SPELL_DRAIN_LIFE);
    if (actual && hurted > 0)
    {
        if (beam.origin_spell == SPELL_OZOCUBUS_REFRIGERATION)
            mpr("You feel very cold.");

        if (agent && !agent->is_player())
        {
            ouch(hurted, KILLED_BY_BEAM, agent->mid,
                 make_stringf("by %s", beam.name.c_str()).c_str(), true,
                 agent->as_monster()->name(DESC_A).c_str());
            you.expose_to_element(beam.flavour, 5);
        }
        // -harm from player casting Ozo's Refridge.
        // we don't actually take damage, but can get slowed and lose potions
        else if (beam.origin_spell == SPELL_OZOCUBUS_REFRIGERATION)
        {
            you.expose_to_element(beam.flavour, 5);
            int old_duration = you.duration[DUR_NO_POTIONS];
            you.increase_duration(DUR_NO_POTIONS, 7 + random2(9), 15);
            int dur_delta = you.duration[DUR_NO_POTIONS] - old_duration;
            refrigerate_food(dur_delta);
        }
    }

    return hurted;
}

static int _los_spell_damage_monster(const actor* agent, monster &target,
                                     bolt &beam, bool actual)
{

    beam.thrower = (agent && agent->is_player()) ? KILL_YOU :
                    agent                        ? KILL_MON
                                                 : KILL_MISC;

    // Set conducts here. The monster needs to be alive when this is done, and
    // mons_adjust_flavoured() could kill it.
    god_conduct_trigger conducts[3];
    if (YOU_KILL(beam.thrower))
        set_attack_conducts(conducts, target, you.can_see(target));

    int hurted = actual ? beam.damage.roll()
                        // Monsters use the average for foe calculations.
                        : (1 + beam.damage.num * beam.damage.size) / 2;
    hurted = mons_adjust_flavoured(&target, beam, hurted,
                 // Drain life doesn't apply drain effects.
                 actual && beam.origin_spell != SPELL_DRAIN_LIFE);
    dprf("damage done: %d", hurted);

    if (actual)
    {
        if (YOU_KILL(beam.thrower))
            _player_hurt_monster(target, hurted, beam.flavour, false);
        else if (hurted)
            target.hurt(agent, hurted, beam.flavour);

        // Cold-blooded creatures can be slowed.
        if (beam.origin_spell == SPELL_OZOCUBUS_REFRIGERATION
            && target.alive())
        {
            target.expose_to_element(beam.flavour, 5);
        }
    }

    // So that summons don't restore HP.
    if (beam.origin_spell == SPELL_DRAIN_LIFE && target.is_summoned())
        return 0;

    return hurted;
}


static spret _cast_los_attack_spell(spell_type spell, int pow,
                                         const actor* agent, actor* /*defender*/,
                                         bool actual, bool fail,
                                         int* damage_done)
{
    const monster* mons = agent ? agent->as_monster() : nullptr;

    const zap_type zap = spell_to_zap(spell);
    if (zap == NUM_ZAPS)
        return spret::abort;

    bolt beam;
    zappy(zap, pow, mons, beam);
    beam.source_id = agent ? agent->mid : MID_NOBODY;
    beam.foe_ratio = 80;

    const char *player_msg = nullptr, *global_msg = nullptr,
               *mons_vis_msg = nullptr, *mons_invis_msg = nullptr,
               *verb = nullptr, *prompt_verb = nullptr;
    bool (*vulnerable)(const actor *, const actor *) = nullptr;

    switch (spell)
    {
        case SPELL_OZOCUBUS_REFRIGERATION:
            player_msg = "The heat is drained from your surroundings.";
            global_msg = "Something drains the heat from around you.";
            mons_vis_msg = " drains the heat from the surrounding"
                           " environment!";
            mons_invis_msg = "The ambient heat is drained!";
            verb = "frozen";
            prompt_verb = "refrigerate";
            vulnerable = [](const actor *caster, const actor *act) {
                return act->is_player() || act->res_cold() < 3
                    && !god_protects(caster, act->as_monster());
            };
            break;

        case SPELL_DRAIN_LIFE:
            player_msg = "You draw life from your surroundings.";
            global_msg = "Something draws the life force from your"
                         " surroundings.";
            mons_vis_msg = " draws from the surrounding life force!";
            mons_invis_msg = "The surrounding life force dissipates!";
            verb = "drained of life";
            prompt_verb = "drain life";
            vulnerable = &_drain_lifeable;
            break;

        case SPELL_SONIC_WAVE:
            player_msg = "You send a blast of sound all around you.";
            global_msg = "Something sends a blast of sound all around you.";
            mons_vis_msg = " sends a blast of sound all around you!";
            mons_invis_msg = "Sound blasts the surrounding area!";
            verb = "blasted";
            // prompt_verb = "sing" The singing sword prompts in melee-attack
            vulnerable = [](const actor *caster, const actor *act) {
                return act != caster
                    && !god_protects(caster, act->as_monster());
            };
            break;

        default:
            return spret::abort;
    }

    auto vul_hitfunc = [vulnerable](const actor *act) -> bool
    {
        return (*vulnerable)(&you, act);
    };

    if (agent && agent->is_player())
    {
        ASSERT(actual);

        targeter_radius hitfunc(&you, LOS_NO_TRANS);
        // Singing Sword's spell shouldn't give a prompt at this time.
        if (spell != SPELL_SONIC_WAVE)
        {
            if (!you.is_auto_spell() && stop_attack_prompt(hitfunc, prompt_verb, vul_hitfunc))
                return spret::abort;

            fail_check();
        }

        mpr(player_msg);
        flash_view_delay(UA_PLAYER, beam.colour, 300, &hitfunc);
    }
    else if (actual)
    {
        if (!agent)
            mpr(global_msg);
        else if (you.can_see(*agent))
            simple_monster_message(*mons, mons_vis_msg);
        else if (you.see_cell(agent->pos()))
            mpr(mons_invis_msg);

        if (!agent || you.see_cell(agent->pos()))
            flash_view_delay(UA_MONSTER, beam.colour, 300);
    }

    bool affects_you = false;
    vector<monster *> affected_monsters;

    for (actor_near_iterator ai((agent ? agent : &you)->pos(), LOS_NO_TRANS);
         ai; ++ai)
    {
        if ((*vulnerable)(agent, *ai))
        {
            if (ai->is_player())
                affects_you = true;
            else
                affected_monsters.push_back(ai->as_monster());
        }
    }

    const int avg_damage = (1 + beam.damage.num * beam.damage.size) / 2;
    int total_damage = 0;
    // XXX: This ordering is kind of broken; it's to preserve the message
    // order from the original behaviour in the case of refrigerate.
    if (affects_you)
    {
        total_damage = _los_spell_damage_player(agent, beam, actual);
        if (!actual && mons)
        {
            if (mons->wont_attack())
            {
                beam.friend_info.count++;
                beam.friend_info.power +=
                    (you.get_experience_level() * total_damage / avg_damage);
            }
            else
            {
                beam.foe_info.count++;
                beam.foe_info.power +=
                    (you.get_experience_level() * total_damage / avg_damage);
            }
        }
    }

    if (actual && !affected_monsters.empty())
        _los_spell_pre_damage_monsters(agent, affected_monsters, verb);

    for (auto m : affected_monsters)
    {
        // Watch out for invalidation. Example: Ozocubu's refrigeration on
        // a bunch of ballistomycete spores that blow each other up.
        if (!m->alive())
            continue;

        int this_damage = _los_spell_damage_monster(agent, *m, beam, actual);
        total_damage += this_damage;

        if (!actual && mons)
        {
            if (mons_atts_aligned(m->attitude, mons->attitude))
            {
                beam.friend_info.count++;
                beam.friend_info.power +=
                    (m->get_hit_dice() * this_damage / avg_damage);
            }
            else
            {
                beam.foe_info.count++;
                beam.foe_info.power +=
                    (m->get_hit_dice() * this_damage / avg_damage);
            }
        }
    }

    if (damage_done)
        *damage_done = total_damage;

    if (actual)
        return spret::success;
    return mons_should_fire(beam) ? spret::success : spret::abort;
}

spret trace_los_attack_spell(spell_type spell, int pow, const actor* agent)
{
    return _cast_los_attack_spell(spell, pow, agent, nullptr, false, false,
                                  nullptr);
}

spret fire_los_attack_spell(spell_type spell, int pow, const actor* agent,
                                 actor *defender, bool fail, int* damage_done)
{
    return _cast_los_attack_spell(spell, pow, agent, defender, true, fail,
                                  damage_done);
}

spret vampiric_drain(int pow, monster* mons, bool fail)
{
    const bool observable = mons && mons->observable();
    if (!mons
        || mons->submerged()
        || !observable && !actor_is_susceptible_to_vampirism(*mons))
    {
        fail_check();

        canned_msg(MSG_NOTHING_CLOSE_ENOUGH);
        // Cost to disallow freely locating invisible/submerged
        // monsters.
        return spret::success;
    }

    // TODO: check known rN instead of holiness
    if (observable && !actor_is_susceptible_to_vampirism(*mons))
    {
        mpr("You can't drain life from that!");
        return spret::abort;
    }

    if (!you.is_auto_spell() && stop_attack_prompt(mons, false, you.pos()))
    {
        canned_msg(MSG_OK);
        return spret::abort;
    }

    fail_check();

    if (!mons->alive())
    {
        canned_msg(MSG_NOTHING_HAPPENS);
        return spret::success;
    }

    // The practical maximum of this is about 25 (pow @ 100). - bwr
    // If you update this, also update spell_damage_string().
    int dam = 3 + random2avg(9, 2) + random2(pow) / 7;
    dam = resist_adjust_damage(mons, BEAM_NEG, dam);

    if (!dam)
    {
        canned_msg(MSG_NOTHING_HAPPENS);
        return spret::success;
    }

    int hp_gain = min(mons->hit_points, dam);

    hp_gain = div_rand_round(hp_gain, 2);
    hp_gain = min(you.hp_max - you.hp, hp_gain);

    _player_hurt_monster(*mons, dam, BEAM_NEG);

    if (hp_gain && !you.duration[DUR_DEATHS_DOOR])
    {
        mprf("You feel life coursing into your body%s",
             attack_strength_punctuation(hp_gain).c_str());
        inc_hp(hp_gain);
    }

    return spret::success;
}

dice_def freeze_damage(int pow)
{
    return dice_def(1, 3 + pow / 3);
}

spret cast_freeze(int pow, monster* mons, bool fail)
{
    pow = min(25, pow);

    if (!mons || mons->submerged())
    {
        fail_check();
        canned_msg(MSG_NOTHING_CLOSE_ENOUGH);
        // If there's no monster there, you still pay the costs in
        // order to prevent locating invisible/submerged monsters.
        return spret::success;
    }

    if (!you.is_auto_spell() && stop_attack_prompt(mons, false, you.pos()))
    {
        canned_msg(MSG_OK);
        return spret::abort;
    }

    fail_check();

    // Set conducts here. The monster needs to be alive when this is done, and
    // mons_adjust_flavoured() could kill it.
    god_conduct_trigger conducts[3];
    set_attack_conducts(conducts, *mons);

    bolt beam;
    beam.flavour = BEAM_COLD;
    beam.thrower = KILL_YOU;

    const int orig_hurted = freeze_damage(pow).roll();
    int hurted = mons_adjust_flavoured(mons, beam, orig_hurted);
    mprf("You freeze %s%s%s",
         mons->name(DESC_THE).c_str(),
         hurted ? "" : " but do no damage",
         attack_strength_punctuation(hurted).c_str());

    _player_hurt_monster(*mons, hurted, beam.flavour, false);

    if (mons->alive())
        mons->expose_to_element(BEAM_COLD, orig_hurted);

    return spret::success;
}

spret cast_airstrike(int pow, const dist &beam, bool fail)
{
    if (cell_is_solid(beam.target))
    {
        canned_msg(MSG_UNTHINKING_ACT);
        return spret::abort;
    }

    monster* mons = monster_at(beam.target);
    if (!mons || mons->submerged())
    {
        fail_check();
        canned_msg(MSG_SPELL_FIZZLES);
        return spret::success; // still losing a turn
    }

    if (!you.is_auto_spell() 
        && !god_protects(mons)
        && stop_attack_prompt(mons, false, you.pos()))
    {
        return spret::abort;
    }
    fail_check();

    god_conduct_trigger conducts[3];
    set_attack_conducts(conducts, *mons, you.can_see(*mons));

    noisy(spell_effect_noise(SPELL_AIRSTRIKE), beam.target);

    bolt pbeam;
    pbeam.name = "airstrike";
    pbeam.flavour = BEAM_AIR;
    pbeam.glyph = dchar_glyph(DCHAR_FIRED_ZAP);
    pbeam.colour = WHITE;
#ifdef USE_TILE
    pbeam.tile_beam = -1;
#endif
    pbeam.draw_delay = 0;


    int empty_space = 0;
    for (adjacent_iterator ai(beam.target); ai; ++ai)
        if (!monster_at(*ai) && !cell_is_solid(*ai))
            empty_space++;

    empty_space = max(3, empty_space);

    int hurted = 5 + empty_space + random2avg(2 + div_rand_round(pow, 7),
                                              empty_space - 2);
#ifdef DEBUG_DIAGNOSTICS
    const int preac = hurted;
#endif
    hurted = mons->apply_ac(mons->beam_resists(pbeam, hurted, false));
    dprf("preac: %d, postac: %d", preac, hurted);

    pbeam.draw(beam.target);
    scaled_delay(200);
    pbeam.glyph = 0; // FIXME: a hack to avoid "appears out of thin air"

    
    mprf("The air twists around and %sstrikes %s%s%s",
         mons->airborne() ? "violently " : "",
         mons->name(DESC_THE).c_str(),
         hurted ? "" : " but does no damage",
         attack_strength_punctuation(hurted).c_str());
    _player_hurt_monster(*mons, hurted, pbeam.flavour);

    return spret::success;
}

// Here begin the actual spells:
static int _shatter_mon_dice(const monster *mon)
{
    const int DEFAULT_DICE = 3;
    if (!mon)
        return DEFAULT_DICE;

    // Removed a lot of silly monsters down here... people, just because
    // it says ice, rock, or iron in the name doesn't mean it's actually
    // made out of the substance. - bwr
    switch (mon->type)
    {
    // Double damage to stone, metal and crystal.
    case MONS_EARTH_ELEMENTAL:
    case MONS_ROCKSLIME:
    case MONS_USHABTI:
    case MONS_STATUE:
    case MONS_GARGOYLE:
    case MONS_IRON_ELEMENTAL:
    case MONS_IRON_GOLEM:
    case MONS_PEACEKEEPER:
    case MONS_WAR_GARGOYLE:
    case MONS_SALTLING:
    case MONS_CRYSTAL_GUARDIAN:
    case MONS_OBSIDIAN_STATUE:
    case MONS_ORANGE_STATUE:
    case MONS_ROXANNE:
        return DEFAULT_DICE * 2;

    default:
        if (mon->is_insubstantial())
            return 1;
        if (mon->petrifying() || mon->petrified())
            return DEFAULT_DICE * 2;
        // reduced later by petrification's damage reduction
        else if (mon->is_skeletal() || mon->is_icy())
            return DEFAULT_DICE * 2;
        else if (mon->airborne() || mons_is_slime(*mon))
            return 1;
        // Normal damage to everything else.
        else
            return DEFAULT_DICE;
    }
}

dice_def shatter_damage(int pow, monster* mon)
{
    return dice_def(_shatter_mon_dice(mon), 5 + pow / 3);
}

static int _shatter_monsters(coord_def where, int pow, actor *agent)
{
    monster* mon = monster_at(where);

    if (!mon || !mon->alive() || mon == agent)
        return 0;

    const dice_def dam_dice = shatter_damage(pow, mon);
    int damage = max(0, dam_dice.roll() - random2(mon->armour_class()));

    if (agent->is_player())
        _player_hurt_monster(*mon, damage, BEAM_MMISSILE);
    else if (damage)
        mon->hurt(agent, damage);

    return damage;
}

static int _shatter_walls(coord_def where, int /*pow*/, actor *agent)
{
    int chance = 0;

    // if not in-bounds then we can't really shatter it -- bwr
    if (!in_bounds(where))
        return 0;

    if (env.markers.property_at(where, MAT_ANY, "veto_shatter") == "veto")
        return 0;

    const dungeon_feature_type grid = grd(where);

    switch (grid)
    {
    case DNGN_CLOSED_DOOR:
    case DNGN_CLOSED_CLEAR_DOOR:
    case DNGN_RUNED_DOOR:
    case DNGN_RUNED_CLEAR_DOOR:
    case DNGN_OPEN_DOOR:
    case DNGN_OPEN_CLEAR_DOOR:
    case DNGN_SEALED_DOOR:
    case DNGN_SEALED_CLEAR_DOOR:
        if (you.see_cell(where))
            mpr("A door shatters!");
        chance = 100;
        break;

    case DNGN_GRATE:
        if (you.see_cell(where))
            mpr("An iron grate is ripped into pieces!");
        chance = 100;
        break;

    case DNGN_ORCISH_IDOL:
    case DNGN_GRANITE_STATUE:
        chance = 100;
        break;

    case DNGN_METAL_WALL:
        chance = 15;
        break;

    case DNGN_CLEAR_STONE_WALL:
    case DNGN_STONE_WALL:
        chance = 25;
        break;

    case DNGN_CLEAR_ROCK_WALL:
    case DNGN_ROCK_WALL:
    case DNGN_SLIMY_WALL:
    case DNGN_CRYSTAL_WALL:
    case DNGN_TREE:
        chance = 33;
        break;

    default:
        break;
    }

    if (agent->deity() == GOD_FEDHAS && feat_is_tree(grid))
        return 0;

    if (x_chance_in_y(chance, 100))
    {
        noisy(spell_effect_noise(SPELL_SHATTER), where);

        destroy_wall(where);

        return 1;
    }

    return 0;
}

static int _shatter_player_dice()
{
    if (you.is_insubstantial())
        return 1;
    if (you.petrified() || you.petrifying())
        return 6; // reduced later by petrification's damage reduction
    else if (you.form == transformation::statue
             || you.form == transformation::ice_beast
             || you.form == transformation::golem
             || you.species == SP_GARGOYLE)
        return 6;
    else if (you.airborne())
        return 1;
    else
        return 3;
}

/**
 * Is this a valid target for shatter?
 *
 * @param act     The actor being considered
 * @return        Whether the actor will take damage from shatter.
 */
static bool _shatterable(const actor *act)
{
    if (act->is_player())
        return _shatter_player_dice();
    return _shatter_mon_dice(act->as_monster());
}

spret cast_shatter(int pow, bool fail)
{
    targeter_radius hitfunc(&you, LOS_ARENA);
    auto vulnerable = [](const actor *act) -> bool
    {
        return !act->is_player()
               && !god_protects(act->as_monster())
               && _shatterable(act);
    };
    if (!you.is_auto_spell() && stop_attack_prompt(hitfunc, "attack", vulnerable))
        return spret::abort;

    fail_check();
    const bool silence = silenced(you.pos());

    if (silence)
        mpr("The dungeon shakes!");
    else
    {
        noisy(spell_effect_noise(SPELL_SHATTER), you.pos());
        mprf(MSGCH_SOUND, "The dungeon rumbles!");
    }

    run_animation(ANIMATION_SHAKE_VIEWPORT, UA_PLAYER);

    int dest = 0;
    for (distance_iterator di(you.pos(), true, true, LOS_RADIUS); di; ++di)
    {
        // goes from the center out, so newly dug walls recurse
        if (!cell_see_cell(you.pos(), *di, LOS_SOLID))
            continue;

        _shatter_monsters(*di, pow, &you);
        dest += _shatter_walls(*di, pow, &you);
    }

    if (dest && !silence)
        mprf(MSGCH_SOUND, "Ka-crash!");

    return spret::success;
}

static int _shatter_player(int pow, actor *wielder, bool devastator = false)
{
    if (wielder->is_player())
        return 0;

    dice_def dam_dice(_shatter_player_dice(), 5 + pow / 3);

    int damage = max(0, dam_dice.roll() - random2(you.armour_class()));

    if (damage > 0)
    {
        mprf(damage > 15 ? "You shudder from the earth-shattering force%s"
                        : "You shudder%s",
             attack_strength_punctuation(damage).c_str());
        if (devastator)
            ouch(damage, KILLED_BY_MONSTER, wielder->mid);
        else
            ouch(damage, KILLED_BY_BEAM, wielder->mid, "by Shatter");
    }

    return damage;
}

bool mons_shatter(monster* caster, bool actual)
{
    const bool silence = silenced(caster->pos());
    int foes = 0;

    if (actual)
    {
        if (silence)
        {
            mprf("The dungeon shakes around %s!",
                 caster->name(DESC_THE).c_str());
        }
        else
        {
            noisy(spell_effect_noise(SPELL_SHATTER), caster->pos(), caster->mid);
            mprf(MSGCH_SOUND, "The dungeon rumbles around %s!",
                 caster->name(DESC_THE).c_str());
        }
    }

    int pow = 5 + div_rand_round(caster->get_hit_dice() * 9, 2);

    int dest = 0;
    for (distance_iterator di(caster->pos(), true, true, LOS_RADIUS); di; ++di)
    {
        // goes from the center out, so newly dug walls recurse
        if (!cell_see_cell(caster->pos(), *di, LOS_SOLID))
            continue;

        if (actual)
        {
            _shatter_monsters(*di, pow, caster);
            if (*di == you.pos())
                _shatter_player(pow, caster);
            dest += _shatter_walls(*di, pow, caster);
        }
        else
        {
            if (you.pos() == *di)
                foes -= _shatter_player_dice();
            if (const monster *victim = monster_at(*di))
            {
                dprf("[%s]", victim->name(DESC_PLAIN, true).c_str());
                foes += _shatter_mon_dice(victim)
                     * (victim->wont_attack() ? -1 : 1);
            }
        }
    }

    if (dest && !silence)
        mprf(MSGCH_SOUND, "Ka-crash!");

    if (actual)
        run_animation(ANIMATION_SHAKE_VIEWPORT, UA_MONSTER);

    if (!caster->wont_attack())
        foes *= -1;

    if (!actual)
        dprf("Shatter foe HD: %d", foes);

    return foes > 0; // doesn't matter if actual
}

void shillelagh(actor *wielder, coord_def where, int pow)
{
    bolt beam;
    beam.name = "shillelagh";
    beam.flavour = BEAM_VISUAL;
    beam.set_agent(wielder);
    beam.colour = BROWN;
    beam.glyph = dchar_glyph(DCHAR_EXPLOSION);
    beam.range = 1;
    beam.ex_size = 1;
    beam.is_explosion = true;
    beam.source = wielder->pos();
    beam.target = where;
    beam.hit = AUTOMATIC_HIT;
    beam.loudness = 7;
    beam.explode();

    counted_monster_list affected_monsters;
    for (adjacent_iterator ai(where, false); ai; ++ai)
    {
        monster *mon = monster_at(*ai);
        if (!mon || !mon->alive() || mon->submerged()
            || mon->is_insubstantial() || !you.can_see(*mon)
            || mon == wielder)
        {
            continue;
        }
        affected_monsters.add(mon);
    }
    if (!affected_monsters.empty())
    {
        const string message =
            make_stringf("%s shudder%s.",
                         affected_monsters.describe().c_str(),
                         affected_monsters.count() == 1? "s" : "");
        if (strwidth(message) < get_number_of_cols() - 2)
            mpr(message);
        else
            mpr("There is a shattering impact!");
    }

    // need to do this again to do the actual damage
    for (adjacent_iterator ai(where, false); ai; ++ai)
        _shatter_monsters(*ai, pow * 3 / 2, wielder);

    if ((you.pos() - wielder->pos()).rdist() <= 1 && in_bounds(you.pos()))
        _shatter_player(pow, wielder, true);
}

dice_def irradiate_damage(int pow, bool random)
{
    const int dice = 6;
    const int max_dam = 30 + random ? div_rand_round(pow, 2) : pow / 2;
    return calc_dice(dice, max_dam);
}

/**
 * Irradiate the given cell. (Per the spell.)
 *
 * @param where     The cell in question.
 * @param pow       The power with which the spell is being cast.
 * @param agent     The agent (player or monster) doing the irradiating.
 */
static int _irradiate_cell(coord_def where, int pow, actor *agent)
{
    monster *mons = monster_at(where);
    if (!mons || !mons->alive())
        return 0; // XXX: handle damaging the player for mons casts...?

    const dice_def dam_dice = irradiate_damage(pow);
    const int dam = dam_dice.roll();
    mprf("%s is blasted with magical radiation%s",
         mons->name(DESC_THE).c_str(),
         attack_strength_punctuation(dam).c_str());
    dprf("irr for %d (%d pow, %dd%d)", dam, pow, dam_dice.num, dam_dice.size);

    if (god_protects(mons, false))
        return 0;

    if (agent->is_player())
        _player_hurt_monster(*mons, dam, BEAM_MMISSILE);
    else if (dam)
        mons->hurt(agent, dam, BEAM_MMISSILE);

    if (mons->alive())
        mons->malmutate("");

    return dam;
}

/**
 * Attempt to cast the spell "Irradiate", damaging & deforming enemies around
 * the player.
 *
 * @param pow   The power at which the spell is being cast.
 * @param who   The actor doing the irradiating.
 * @param fail  Whether the player has failed to cast the spell.
 * @return      spret::abort if the player changed their mind about casting after
 *              realizing they would hit an ally; spret::fail if they failed the
 *              cast chance; spret::success otherwise.
 */
spret cast_irradiate(int powc, actor* who, bool fail)
{
    targeter_radius hitfunc(who, LOS_NO_TRANS, 1, 0, 1);
    auto vulnerable = [who](const actor *act) -> bool
    {
        return !act->is_player()
            && !god_protects(who, act->as_monster());
    };

    if (!you.is_auto_spell() && stop_attack_prompt(hitfunc, "irradiate", vulnerable))
        return spret::abort;

    fail_check();

    ASSERT(who);
    if (who->is_player())
        mpr("You erupt in a fountain of uncontrolled magic!");
    else
    {
        simple_monster_message(*who->as_monster(),
                               " erupts in a fountain of uncontrolled magic!");
    }

    bolt beam;
    beam.name = "irradiate";
    beam.flavour = BEAM_VISUAL;
    beam.set_agent(&you);
    beam.colour = ETC_MUTAGENIC;
    beam.glyph = dchar_glyph(DCHAR_EXPLOSION);
    beam.range = 1;
    beam.ex_size = 1;
    beam.is_explosion = true;
    beam.explode_delay = beam.explode_delay * 3 / 2;
    beam.source = you.pos();
    beam.target = you.pos();
    beam.hit = AUTOMATIC_HIT;
    beam.loudness = 0;
    beam.explode(true, true);

    apply_random_around_square([powc, who] (coord_def where) {
        return _irradiate_cell(where, powc, who);
    }, who->pos(), true, 8);

    if (who->is_player())
        contaminate_player(1000 + random2(500));
    return spret::success;
}

// How much work can we consider we'll have done by igniting a cloud here?
// Considers a cloud under a susceptible ally bad, a cloud under a a susceptible
// enemy good, and other clouds relatively unimportant.
static int _ignite_tracer_cloud_value(coord_def where, actor *agent)
{
    actor* act = actor_at(where);
    if (act)
    {
        const int dam = actor_cloud_immune(*act, CLOUD_FIRE)
                        ? 0
                        : resist_adjust_damage(act, BEAM_FIRE, 40);

        if (god_protects(agent, act->as_monster()))
            return 0;

        return mons_aligned(act, agent) ? -dam : dam;
    }
    // We've done something, but its value is indeterminate
    else
        return 1;
}
/**
 * Place flame clouds over toxic bogs, by the power of Ignite Poison.
 *
 * @param where     The tile in question.
 * @param pow       The power with which Ignite Poison is being cast.
 *                  If -1, this indicates the spell is a test-run 'tracer'.
 * @param agent     The caster of Ignite Poison.
 * @return          If we're just running a tracer, return the expected 'value'
 *                  of creating fire clouds in the given location (could be
 *                  negative if there are allies there).
 *                  If it's not a tracer, return 1 if a flame cloud is created
 *                  and 0 otherwise.
 */
static int _ignite_poison_bog(coord_def where, int pow, actor *agent)
{
    const bool tracer = (pow == -1);  // Only testing damage, not dealing it

    if (grd(where) != DNGN_TOXIC_BOG)
        return false;

    if (tracer)
    {
        const int value = _ignite_tracer_cloud_value(where, agent);
        // Player doesn't care about magnitude.
        return agent && agent->is_player() ? sgn(value) : value;
    }

    place_cloud(CLOUD_FIRE, where,
                30 + random2(20 + pow), agent);
    return true;
}
/**
 * Turn poisonous clouds in the given tile into flame clouds, by the power of
 * Ignite Poison.
 *
 * @param where     The tile in question.
 * @param pow       The power with which Ignite Poison is being cast.
 *                  If -1, this indicates the spell is a test-run 'tracer'.
 * @param agent     The caster of Ignite Poison.
 * @return          If we're just running a tracer, return the expected 'value'
 *                  of creating fire clouds in the given location (could be
 *                  negative if there are allies there).
 *                  If it's not a tracer, return 1 if a flame cloud is created
 *                  and 0 otherwise.
 */
static int _ignite_poison_clouds(coord_def where, int pow, actor *agent)
{
    const bool tracer = (pow == -1);  // Only testing damage, not dealing it

    cloud_struct* cloud = cloud_at(where);
    if (!cloud)
        return false;

    if (cloud->type != CLOUD_MEPHITIC && cloud->type != CLOUD_POISON)
        return false;

    if (tracer)
    {
        const int value = _ignite_tracer_cloud_value(where, agent);
        // Player doesn't care about magnitude.
        return agent && agent->is_player() ? sgn(value) : value;
    }

    cloud->type = CLOUD_FIRE;
    cloud->decay = 30 + random2(20 + pow); // from 3-5 turns to 3-15 turns
    cloud->whose = agent->kill_alignment();
    cloud->killer = agent->is_player() ? KILL_YOU_MISSILE : KILL_MON_MISSILE;
    cloud->source = agent->mid;
    return true;
}

/**
 * Burn poisoned monsters in the given tile, removing their poison state &
 * damaging them.
 *
 * @param where     The tile in question.
 * @param pow       The power with which Ignite Poison is being cast.
 *                  If -1, this indicates the spell is a test-run 'tracer'.
 * @param agent     The caster of Ignite Poison.
 * @return          If we're just running a tracer, return the expected damage
 *                  of burning the monster in the given location (could be
 *                  negative if there are allies there).
 *                  If it's not a tracer, return 1 if damage is caused & 0
 *                  otherwise.
 */
static int _ignite_poison_monsters(coord_def where, int pow, actor *agent)
{
    bolt beam;
    beam.flavour = BEAM_FIRE;   // This is dumb, only used for adjust!

    const bool tracer = (pow == -1);  // Only testing damage, not dealing it
    if (tracer)                       // Give some fake damage to test resists
        pow = 100;

    // If a monster casts Ignite Poison, it can't hit itself.
    // This doesn't apply to the other functions: it can ignite
    // clouds where it's standing!

    monster* mon = monster_at(where);
    if (invalid_monster(mon) || mon == agent)
        return 0;

    // how poisoned is the victim?
    const mon_enchant ench = mon->get_ench(ENCH_POISON);
    const int pois_str = ench.ench == ENCH_NONE ? 0 : ench.degree;

    // poison currently does roughly 6 damage per degree (over its duration)
    // do roughly 2x to 3x that much, scaling with spellpower
    const dice_def dam_dice(pois_str * 2, 12 + div_rand_round(pow * 6, 100));

    const int base_dam = dam_dice.roll();
    const int damage = mons_adjust_flavoured(mon, beam, base_dam, false);
    if (damage <= 0)
        return 0;

    mon->expose_to_element(BEAM_FIRE, damage);

    if (tracer)
    {
        // players don't care about magnitude, just care if enemies exist
        if (agent && agent->is_player())
            return mons_aligned(mon, agent) ? -1 : 1;
        return mons_aligned(mon, agent) ? -1 * damage : damage;
    }

    if (you.see_cell(mon->pos()))
    {
        mprf("%s seems to burn from within%s",
             mon->name(DESC_THE).c_str(),
             attack_strength_punctuation(damage).c_str());
    }

    dprf("Dice: %dd%d; Damage: %d", dam_dice.num, dam_dice.size, damage);

    mon->hurt(agent, damage);

    if (mon->alive())
    {
        behaviour_event(mon, ME_WHACK, agent);

        // Monster survived, remove any poison.
        mon->del_ench(ENCH_POISON, true); // suppress spam
        print_wounds(*mon);
    }

    return 1;
}

/**
 * Burn poisoned players in the given tile, removing their poison state &
 * damaging them.
 *
 * @param where     The tile in question.
 * @param pow       The power with which Ignite Poison is being cast.
 *                  If -1, this indicates the spell is a test-run 'tracer'.
 * @param agent     The caster of Ignite Poison.
 * @return          If we're just running a tracer, return the expected damage
 *                  of burning the player in the given location (could be
 *                  negative if the player is an ally).
 *                  If it's not a tracer, return 1 if damage is caused & 0
 *                  otherwise.
 */

static int _ignite_poison_player(coord_def where, int pow, actor *agent)
{
    if (agent->is_player() || where != you.pos())
        return 0;

    const bool tracer = (pow == -1);  // Only testing damage, not dealing it
    if (tracer)                       // Give some fake damage to test resists
        pow = 100;

    // Step down heavily beyond light poisoning (or we could easily one-shot a heavily poisoned character)
    const int pois_str = stepdown((double)you.duration[DUR_POISONING] / 5000,
                                  2.25);
    if (!pois_str)
        return 0;

    const int base_dam = roll_dice(pois_str, 5 + pow/7);
    const int damage = resist_adjust_damage(&you, BEAM_FIRE, base_dam);

    if (tracer)
        return mons_aligned(&you, agent) ? -1 * damage : damage;

    const int resist = player_res_fire();
    if (resist > 0)
        mpr("You feel like your blood is boiling!");
    else if (resist < 0)
        mpr("The poison in your system burns terribly!");
    else
        mpr("The poison in your system burns!");

    ouch(damage, KILLED_BY_BEAM, agent->mid,
         "by burning poison", you.can_see(*agent),
         agent->as_monster()->name(DESC_A, true).c_str());
    if (damage > 0)
        you.expose_to_element(BEAM_FIRE, 2);

    mprf(MSGCH_RECOVERY, "You are no longer poisoned.");
    you.duration[DUR_POISONING] = 0;

    return damage ? 1 : 0;
}

/**
 * Would casting Ignite Poison possibly harm one of the player's allies in the
 * given cell?
 *
 * @param  where    The cell in question.
 * @return          1 if there's potential harm, 0 otherwise.
 */
static int _ignite_ally_harm(const coord_def &where)
{
    if (where == you.pos())
        return 0; // you're not your own ally!
    // (prevents issues with duplicate prompts when standing in an igniteable
    // cloud)

    return (_ignite_poison_clouds(where, -1, &you) < 0)   ? 1 :
           (_ignite_poison_monsters(where, -1, &you) < 0) ? 1 :
            (_ignite_poison_bog(where, -1, &you) < 0)      ? 1 :
            0;
}

/**
 * Let the player choose to abort a casting of ignite poison, if it seems
 * like a bad idea. (If they'd ignite themself.)
 *
 * @return      Whether the player chose to abort the casting.
 */
static bool maybe_abort_ignite()
{
    string prompt = "You are standing ";

    // XXX XXX XXX major code duplication (ChrisOelmueller)
    if (const cloud_struct* cloud = cloud_at(you.pos()))
    {
        if ((cloud->type == CLOUD_MEPHITIC || cloud->type == CLOUD_POISON)
            && !actor_cloud_immune(you, CLOUD_FIRE))
        {
            prompt += "in a cloud of ";
            prompt += cloud->cloud_name(true);
            prompt += "! Ignite poison anyway?";
            if (!yesno(prompt.c_str(), false, 'n'))
                return true;
        }
    }

    if (apply_area_visible(_ignite_ally_harm, you.pos()) > 0)
    {
        return !yesno("You might harm nearby allies! Ignite poison anyway?",
                      false, 'n');
    }

    return false;
}

/**
 * Does Ignite Poison affect the given creature?
 *
 * @param act       The creature in question.
 * @return          Whether Ignite Poison can directly damage the given
 *                  creature (not counting clouds).
 */
bool ignite_poison_affects(const actor* act)
{
    if (act->is_player())
        return you.duration[DUR_POISONING];
    return act->as_monster()->has_ench(ENCH_POISON);
}

/**
 * Cast the spell Ignite Poison, burning poisoned creatures and poisonous
 * clouds in LOS.
 *
 * @param agent         The spell's caster.
 * @param pow           The power with which the spell is being cast.
 * @param fail          If it's a player spell, whether the spell fail chance
 *                      was hit (whether the spell will fail as soon as the
 *                      player chooses not to abort the casting)
 * @param mon_tracer    Whether the 'casting' is just a tracer (a check to see
 *                      if it's worth actually casting)
 * @return              If it's a tracer, spret::success if the spell should
 *                      be cast & spret::abort otherwise.
 *                      If it's a real spell, spret::abort if the player chose
 *                      to abort the spell, spret::fail if they failed the cast
 *                      chance, and spret::success otherwise.
 */
spret cast_ignite_poison(actor* agent, int pow, bool fail, bool tracer)
{
    if (tracer)
    {
        // Estimate how much useful effect we'd get if we cast the spell now
        const int work = apply_area_visible([agent] (coord_def where) {
            return _ignite_poison_clouds(where, -1, agent)
                 + _ignite_poison_monsters(where, -1, agent)
                 + _ignite_poison_player(where, -1, agent)
                 + _ignite_poison_bog(where, -1, agent);
        }, agent->pos());

        return work > 0 ? spret::success : spret::abort;
    }

    if (agent->is_player())
    {
        if (!you.is_auto_spell() && maybe_abort_ignite())
        {
            canned_msg(MSG_OK);
            return spret::abort;
        }
        fail_check();
    }

    targeter_radius hitfunc(agent, LOS_NO_TRANS);
    flash_view_delay(
        agent->is_player()
            ? UA_PLAYER
            : UA_MONSTER,
        RED, 100, &hitfunc);

    mprf("%s %s the poison in %s surroundings!", agent->name(DESC_THE).c_str(),
         agent->conj_verb("ignite").c_str(),
         agent->pronoun(PRONOUN_POSSESSIVE).c_str());

    // this could conceivably cause crashes if the player dies midway through
    // maybe split it up...?
    apply_area_visible([pow, agent] (coord_def where) {
        _ignite_poison_clouds(where, pow, agent);
        _ignite_poison_monsters(where, pow, agent);
        _ignite_poison_bog(where, pow, agent);
        // Only relevant if a monster is casting this spell
        // (never hurts the caster)
        _ignite_poison_player(where, pow, agent);
        return 0; // ignored
    }, agent->pos());

    return spret::success;
}

static int _convert_poison_clouds(coord_def where, int pow, actor* agent);
static int _convert_poison_bog(coord_def where, int pow, actor* agent);

/**
 * Cast the spell Convert Poison, replace poisonous clouds in LOS
 * into healing clouds.
 *
 * @param agent         The spell's caster.
 * @param pow           The power with which the spell is being cast.
 * @param fail          If it's a player spell, whether the spell fail chance
 *                      was hit (whether the spell will fail as soon as the
 *                      player chooses not to abort the casting)
 * @param mon_tracer    Whether the 'casting' is just a tracer (a check to see
 *                      if it's worth actually casting)
 * @return              If it's a tracer, spret::success if the spell should
 *                      be cast & spret::abort otherwise.
 *                      If it's a real spell, spret::abort if the player chose
 *                      to abort the spell, spret::fail if they failed the cast
 *                      chance, and spret::success otherwise.
 */
spret cast_convert_poison(actor* agent, int pow, bool /*fail*/, bool tracer)
{
    if (tracer)
    {
        // Estimate how much useful effect we'd get if we cast the spell now
        const int work = apply_area_visible([agent] (coord_def where) {
            return _convert_poison_clouds(where, -1, agent)
                 + _convert_poison_bog(where, -1, agent);
        }, agent->pos());

        return work > 0 ? spret::success : spret::abort;
    }

    targeter_radius hitfunc(agent, LOS_NO_TRANS);
    flash_view_delay(
        agent->is_player()
            ? UA_PLAYER
            : UA_MONSTER,
        LIGHTGREEN, 100, &hitfunc);

    mprf("%s %s the poisonous cloud in %s surroundings!", agent->name(DESC_THE).c_str(),
         agent->conj_verb("convert").c_str(),
         agent->pronoun(PRONOUN_POSSESSIVE).c_str());

    // this could conceivably cause crashes if the player dies midway through
    // maybe split it up...?
    apply_area_visible([pow, agent] (coord_def where) {
        _convert_poison_clouds(where, pow, agent);
        _convert_poison_bog(where, pow, agent);
        return 0; // ignored
    }, agent->pos());

    return spret::success;
}

/**
 * Place healing clouds over toxic bogs, by the power of Convert Poison.
 *
 * @param where     The tile in question.
 * @param pow       The power with which Convert Poison is being cast.
 *                  If -1, this indicates the spell is a test-run 'tracer'.
 * @param agent     The caster of Convert Poison.
 * @return          If we're just running a tracer, return the expected 'value'
 *                  of creating fire clouds in the given location (could be
 *                  negative if there are allies there).
 *                  If it's not a tracer, return 1 if a flame cloud is created
 *                  and 0 otherwise.
 */
static int _convert_poison_bog(coord_def where, int pow, actor *agent)
{
    const bool tracer = (pow == -1);  // Only testing damage, not dealing it

    if (grd(where) != DNGN_TOXIC_BOG)
        return false;

    if (tracer)
    {
        //FIXME) create _convert_tracer_cloud_value function
        const int value = 1;// _convert_tracer_cloud_value(where, agent);
        // Player doesn't care about magnitude.
        return agent && agent->is_player() ? sgn(value) : value;
    }

    place_cloud(CLOUD_HEAL, where,
                40 + (pow/40) + random2(10 + pow/2), agent);
    return true;
}
/**
 * Turn poisonous clouds in the given tile into healing clouds, by the power of
 * Convert Poison.
 *
 * @param where     The tile in question.
 * @param pow       The power with which Convert Poison is being cast.
 *                  If -1, this indicates the spell is a test-run 'tracer'.
 * @param agent     The caster of Convert Poison.
 * @return          If we're just running a tracer, return the expected 'value'
 *                  of creating fire clouds in the given location (could be
 *                  negative if there are allies there).
 *                  If it's not a tracer, return 1 if a flame cloud is created
 *                  and 0 otherwise.
 */
static int _convert_poison_clouds(coord_def where, int pow, actor *agent)
{
    const bool tracer = (pow == -1);  // Only testing damage, not dealing it

    cloud_struct* cloud = cloud_at(where);
    if (!cloud)
        return false;

    if (cloud->type != CLOUD_MEPHITIC && cloud->type != CLOUD_POISON
		&& cloud->type != CLOUD_MIASMA && cloud->type != CLOUD_MUTAGENIC)
        return false;

    if (tracer)
    {
        //FIXME) create _convert_tracer_cloud_value function
        const int value = 1;//_convert_tracer_cloud_value(where, agent);
        // Player doesn't care about magnitude.
        return agent && agent->is_player() ? sgn(value) : value;
    }

    cloud->type = CLOUD_HEAL;
    cloud->decay = 40 + (pow/40) + random2(10 + pow/2);
    cloud->whose = agent->kill_alignment();
    cloud->killer = agent->is_player() ? KILL_YOU_MISSILE : KILL_MON_MISSILE;
    cloud->source = agent->mid;
    return true;
}

static void _ignition_square(const actor */*agent*/, bolt beam, coord_def square, bool center)
{
    // HACK: bypass visual effect
    beam.target = square;
    beam.in_explosion_phase = true;
    beam.explosion_affect_cell(square);
    if (center)
        noisy(spell_effect_noise(SPELL_IGNITION),square);
}

spret cast_ignition(const actor *agent, int pow, bool fail)
{
    ASSERT(agent->is_player());

    fail_check();

    //targeter_radius hitfunc(agent, LOS_NO_TRANS);

    // Ignition affects squares that had hostile monsters on them at the time
    // of casting. This way nothing bad happens when monsters die halfway
    // through the spell.
    vector<coord_def> blast_sources;

    for (actor_near_iterator ai(agent->pos(), LOS_NO_TRANS);
         ai; ++ai)
    {
        if (ai->is_monster()
            && !ai->as_monster()->wont_attack()
            && !mons_is_firewood(*ai->as_monster())
            && !mons_is_tentacle_segment(ai->as_monster()->type))
        {
            blast_sources.push_back(ai->position);
        }
    }

    if (blast_sources.empty())
        canned_msg(MSG_NOTHING_HAPPENS);
    else
    {
        mpr("The air bursts into flame!");

        vector<coord_def> blast_adjacents;

        // Used to draw explosion cells
        bolt beam_visual;
        beam_visual.set_agent(agent);
        beam_visual.flavour       = BEAM_VISUAL;
        beam_visual.glyph         = dchar_glyph(DCHAR_FIRED_BURST);
        beam_visual.colour        = RED;
        beam_visual.ex_size       = 1;
        beam_visual.is_explosion  = true;

        // Used to deal damage; invisible
        bolt beam_actual;
        zappy(ZAP_IGNITION, pow, false, beam_actual);
        beam_actual.set_agent(agent);
        beam_actual.ex_size       = 0;
        beam_actual.origin_spell  = SPELL_IGNITION;
        beam_actual.apply_beam_conducts();

#ifdef DEBUG_DIAGNOSTICS
        dprf(DIAG_BEAM, "ignition dam=%dd%d",
             beam_actual.damage.num, beam_actual.damage.size);
#endif

        // Fake "shaped" radius 1 explosions (skipping squares with friends).
        for (coord_def pos : blast_sources)
        {
            for (adjacent_iterator ai(pos); ai; ++ai)
            {
                if (cell_is_solid(*ai)
                    && (!beam_actual.can_affect_wall(*ai)
                        || you_worship(GOD_FEDHAS)))
                {
                    continue;
                }

                actor *act = actor_at(*ai);

                // Friendly creature, don't blast this square.
                if (act && (act == agent
                            || (act->is_monster()
                                && act->as_monster()->wont_attack())))
                {
                    continue;
                }

                blast_adjacents.push_back(*ai);
                beam_visual.explosion_draw_cell(*ai);
            }
            beam_visual.explosion_draw_cell(pos);
        }
        update_screen();
        scaled_delay(50);

        // Real explosions on each individual square.
        for (coord_def pos : blast_sources)
            _ignition_square(agent, beam_actual, pos, true);
        for (coord_def pos : blast_adjacents)
            _ignition_square(agent, beam_actual, pos, false);
    }

    return spret::success;
}

static int _discharge_monsters(const coord_def &where, int pow,
                               const actor &agent)
{
    actor* victim = actor_at(where);

    if (!victim || !victim->alive())
        return 0;

    int damage = (&agent == victim) ? 1 + random2(3 + pow / 15)
                                    : 3 + random2(5 + pow / 10
                                                  + (random2(pow) / 10));

    bolt beam;
    beam.flavour    = BEAM_ELECTRICITY; // used for mons_adjust_flavoured
    beam.glyph      = dchar_glyph(DCHAR_FIRED_ZAP);
    beam.colour     = LIGHTBLUE;
#ifdef USE_TILE
    beam.tile_beam  = -1;
#endif
    beam.draw_delay = 0;

    dprf("Static discharge on (%d,%d) pow: %d", where.x, where.y, pow);
    if (victim->is_player() || victim->res_elec() <= 0)
        beam.draw(where);

    if (victim->is_player())
    {
        damage = 1 + random2(3 + pow / 15);
        dprf("You: static discharge damage: %d", damage);
        damage = check_your_resists(damage, BEAM_ELECTRICITY,
                                    "static discharge");
        mprf("You are struck by an arc of lightning%s",
             attack_strength_punctuation(damage).c_str());
        ouch(damage, KILLED_BY_BEAM, agent.mid, "by static electricity", true,
             agent.is_player() ? "you" : agent.name(DESC_A).c_str());
        if (damage > 0)
            victim->expose_to_element(BEAM_ELECTRICITY, 2);
    }
    // rEelec monsters don't allow arcs to continue.
    else if (victim->res_elec() > 0)
        return 0;
    else if (god_protects(&agent, victim->as_monster(), false))
        return 0;
    else
    {
        monster* mons = victim->as_monster();

        // We need to initialize these before the monster has died.
        god_conduct_trigger conducts[3];
        if (agent.is_player())
            set_attack_conducts(conducts, *mons, you.can_see(*mons));

        dprf("%s: static discharge damage: %d",
             mons->name(DESC_PLAIN, true).c_str(), damage);
        damage = mons_adjust_flavoured(mons, beam, damage);
        mprf("%s is struck by an arc of lightning%s",
                mons->name(DESC_THE).c_str(),
                attack_strength_punctuation(damage).c_str());

        if (agent.is_player())
            _player_hurt_monster(*mons, damage, beam.flavour, false);
        else if (damage)
            mons->hurt(agent.as_monster(), damage);
    }

    // Recursion to give us chain-lightning -- bwr
    // Low power slight chance added for low power characters -- bwr
    if ((pow >= 10 && !one_chance_in(4)) || (pow >= 3 && one_chance_in(10)))
    {
        pow /= random_range(2, 3);
        damage += apply_random_around_square([pow, &agent] (coord_def where2) {
            return _discharge_monsters(where2, pow, agent);
        }, where, true, 1);
    }
    else if (damage > 0)
    {
        // Only printed if we did damage, so that the messages in
        // cast_discharge() are clean. -- bwr
        mpr("The lightning grounds out.");
    }

    return damage;
}

bool safe_discharge(coord_def where, vector<const actor *> &exclude)
{
    for (adjacent_iterator ai(where); ai; ++ai)
    {
        const actor *act = actor_at(*ai);
        if (!act)
            continue;

        if (find(exclude.begin(), exclude.end(), act) == exclude.end())
        {
            if (act->is_monster())
            {
                // Harmless to these monsters, so don't prompt about them.
                if (act->res_elec() > 0
                    || god_protects(act->as_monster()))
                {
                    continue;
                }

                if (!you.is_auto_spell() && stop_attack_prompt(act->as_monster(), false, where))
                    return false;
            }
            // Don't prompt for the player, but always continue arcing.

            exclude.push_back(act);
            if (!safe_discharge(act->pos(), exclude))
                return false;
        }
    }

    return true;
}

spret cast_discharge(int pow, const actor &agent, bool fail, bool prompt)
{
    vector<const actor *> exclude;
    if (agent.is_player() && prompt && !safe_discharge(you.pos(), exclude))
        return spret::abort;

    fail_check();

    const int num_targs = 1 + random2(random_range(1, 3) + pow / 20);
    const int dam =
        apply_random_around_square([pow, &agent] (coord_def target) {
            return _discharge_monsters(target, pow, agent);
        }, agent.pos(), true, num_targs);

    dprf("Arcs: %d Damage: %d", num_targs, dam);

    if (dam > 0)
        scaled_delay(100);
    else
    {
        if (coinflip())
            mpr("The air crackles with electrical energy.");
        else
        {
            const bool plural = coinflip();
            mprf("%s blue arc%s ground%s harmlessly.",
                 plural ? "Some" : "A",
                 plural ? "s" : "",
                 plural ? " themselves" : "s itself");
        }
    }
    return spret::success;
}

dice_def base_fragmentation_damage(int pow)
{
    return dice_def(3, 5 + pow / 5);
}

bool setup_fragmentation_beam(bolt &beam, int pow, const actor *caster,
                              const coord_def target, bool quiet,
                              const char **what, bool &should_destroy_wall, bool &hole)
{
    beam.flavour     = BEAM_FRAG;
    beam.glyph       = dchar_glyph(DCHAR_FIRED_BURST);
    beam.source_id   = caster->mid;
    beam.thrower     = caster->is_player() ? KILL_YOU : KILL_MON;
    beam.ex_size     = 1;
    beam.source      = you.pos();
    beam.hit         = AUTOMATIC_HIT;

    beam.source_name = caster->name(DESC_PLAIN, true);
    beam.aux_source = "by Lee's Rapid Deconstruction"; // for direct attack

    beam.target = target;

    // Number of dice vary from 2-4.
    beam.damage = base_fragmentation_damage(pow);

    monster* mon = monster_at(target);
    const dungeon_feature_type grid = grd(target);

    if (target == you.pos())
    {
        const bool petrified = (you.petrified() || you.petrifying());

        if (you.form == transformation::statue || you.species == SP_GARGOYLE)
        {
            beam.name       = "blast of rock fragments";
            beam.colour     = BROWN;
            if (you.species == SP_GARGOYLE)
                beam.damage.num = 2;
            return true;
        }
        else if (petrified)
        {
            beam.name       = "blast of petrified fragments";
            beam.colour     = mons_class_colour(player_mons(true));
            return true;
        }
        else if (you.form == transformation::ice_beast) // blast of ice
        {
            beam.name       = "icy blast";
            beam.colour     = WHITE;
            beam.flavour    = BEAM_ICE;
            return true;
        }
        else if (you.form == transformation::golem)
        {
            beam.name       = "blast of armour fragments";
            beam.colour     = BROWN;
            return true;
        }
    }
    else if (mon && (caster->is_monster() || (you.can_see(*mon))))
    {
        switch (mon->type)
        {
        case MONS_TOENAIL_GOLEM:
            beam.name       = "blast of toenail fragments";
            beam.colour     = RED;
            break;

        case MONS_IRON_ELEMENTAL:
        case MONS_IRON_GOLEM:
        case MONS_PEACEKEEPER:
        case MONS_WAR_GARGOYLE:
            beam.name       = "blast of metal fragments";
            beam.colour     = CYAN;
            beam.damage.num = 4;
            break;

        case MONS_EARTH_ELEMENTAL:
        case MONS_ROCKSLIME:
        case MONS_USHABTI:
        case MONS_STATUE:
        case MONS_GARGOYLE:
            beam.name       = "blast of rock fragments";
            beam.colour     = BROWN;
            break;

        case MONS_SALTLING:
            beam.name       = "blast of salt crystal fragments";
            beam.colour     = WHITE;
            break;

        case MONS_OBSIDIAN_STATUE:
        case MONS_ORANGE_STATUE:
        case MONS_CRYSTAL_GUARDIAN:
        case MONS_ROXANNE:
            beam.ex_size    = 2;
            beam.damage.num = 4;
            if (mon->type == MONS_OBSIDIAN_STATUE)
            {
                beam.name       = "blast of obsidian shards";
                beam.colour     = DARKGREY;
            }
            else if (mon->type == MONS_ORANGE_STATUE)
            {
                beam.name       = "blast of orange crystal shards";
                beam.colour     = LIGHTRED;
            }
            else if (mon->type == MONS_CRYSTAL_GUARDIAN)
            {
                beam.name       = "blast of crystal shards";
                beam.colour     = GREEN;
            }
            else
            {
                beam.name       = "blast of sapphire shards";
                beam.colour     = BLUE;
            }
            break;

        default:
            const bool petrified = (mon->petrified() || mon->petrifying());

            // Petrifying or petrified monsters can be exploded.
            if (petrified)
            {
                monster_info minfo(mon);
                beam.name       = "blast of petrified fragments";
                beam.colour     = minfo.colour();
                break;
            }
            else if (mon->is_icy()) // blast of ice
            {
                beam.name       = "icy blast";
                beam.colour     = WHITE;
                beam.flavour    = BEAM_ICE;
                break;
            }
            else if (mon->is_skeletal()) // blast of bone
            {
                beam.name   = "blast of bone shards";
                beam.colour = LIGHTGREY;
                break;
            }
            // Targeted monster not shatterable, try the terrain instead.
            goto do_terrain;
        }

        beam.aux_source = beam.name;

        // Got a target, let's blow it up.
        return true;
    }

  do_terrain:
    switch (grid)
    {
    // Stone and rock terrain
    case DNGN_ORCISH_IDOL:
        if (what && *what == nullptr)
            *what = "stone idol";
        // fall-through
    case DNGN_ROCK_WALL:
    case DNGN_SLIMY_WALL:
    case DNGN_STONE_WALL:
    case DNGN_CLEAR_ROCK_WALL:
    case DNGN_CLEAR_STONE_WALL:
        if (what && *what == nullptr)
            *what = "wall";
        // fall-through
    case DNGN_GRANITE_STATUE:
        if (what && *what == nullptr)
            *what = "statue";

        beam.name       = "blast of rock fragments";
        beam.damage.num = 3;

        if (grid == DNGN_ORCISH_IDOL
            || grid == DNGN_GRANITE_STATUE
            || pow >= 35 && (grid == DNGN_ROCK_WALL
                || grid == DNGN_SLIMY_WALL
                || grid == DNGN_CLEAR_ROCK_WALL)
            && one_chance_in(3)
            || pow >= 50 && (grid == DNGN_STONE_WALL
                || grid == DNGN_CLEAR_STONE_WALL)
            && one_chance_in(10))
        {
            should_destroy_wall = true;
        }
        break;

    // Metal -- small but nasty explosion
    case DNGN_METAL_WALL:
        if (what)
            *what = "metal wall";
        // fall-through
    case DNGN_GRATE:
        if (what && *what == nullptr)
            *what = "iron grate";
        beam.name       = "blast of metal fragments";
        beam.damage.num = 4;
        if (pow >= 75 && one_chance_in(20)
            || grid == DNGN_GRATE)
        {
            should_destroy_wall = true;
        }
        break;

    // Crystal
    case DNGN_CRYSTAL_WALL:       // crystal -- large & nasty explosion
        if (what)
            *what = "crystal wall";
        beam.ex_size    = 2;
        beam.name       = "blast of crystal shards";
        beam.damage.num = 4;
        if (one_chance_in(3))
            should_destroy_wall = true;
        break;

    // Stone arches and doors
    case DNGN_OPEN_DOOR:
    case DNGN_OPEN_CLEAR_DOOR:
    case DNGN_CLOSED_DOOR:
    case DNGN_CLOSED_CLEAR_DOOR:
    case DNGN_RUNED_DOOR:
    case DNGN_RUNED_CLEAR_DOOR:
    case DNGN_SEALED_DOOR:
    case DNGN_SEALED_CLEAR_DOOR:
        if (what)
            *what = "stone door frame";
        should_destroy_wall = true;
        // fall-through
    case DNGN_STONE_ARCH:
        if (what && *what == nullptr)
            *what = "stone arch";
        hole            = false;  // to hit monsters standing on doors
        beam.name       = "blast of rock fragments";
        beam.damage.num = 3;


        break;

    default:
        // Couldn't find a monster or wall to shatter - abort casting!
        if (caster->is_player() && !quiet)
            mpr("You can't deconstruct that!");
        return false;
    }

    // If it was recoloured, use that colour instead.
    if (env.grid_colours(target))
        beam.colour = env.grid_colours(target);
    else
    {
        beam.colour = element_colour(get_feature_def(grid).colour(),
                                     false, target);
    }

    beam.aux_source = beam.name;

    return true;
}

spret cast_fragmentation(int pow, const actor *caster,
                              const coord_def target, bool fail)
{
    bool should_destroy_wall = false;
    bool hole                = true;
    const char *what         = nullptr;

    bolt beam;

    // should_destroy_wall is an output argument.
    if (!setup_fragmentation_beam(beam, pow, caster, target, false, &what,
        should_destroy_wall, hole))
    {
        return spret::abort;
    }

    if (caster->is_player())
    {
        bolt tempbeam;
        bool temp;
        setup_fragmentation_beam(tempbeam, pow, caster, target, true, nullptr,
                                 temp, temp);
        tempbeam.is_tracer = true;
        tempbeam.explode(false);
        if (tempbeam.beam_cancelled)
        {
            canned_msg(MSG_OK);
            return spret::abort;
        }
    }

    fail_check();

    if (what != nullptr) // Terrain explodes.
    {
        if (you.see_cell(target))
            mprf("The %s shatters!", what);
        if (should_destroy_wall)
            destroy_wall(target);
    }
    else if (target == you.pos()) // You explode.
    {
        const int dam = beam.damage.roll();
        mprf("You shatter%s", attack_strength_punctuation(dam).c_str());

        ouch(dam, KILLED_BY_BEAM, caster->mid,
             "by Lee's Rapid Deconstruction", true,
             caster->is_player() ? "you"
                                 : caster->name(DESC_A).c_str());
    }
    else // Monster explodes.
    {
        // Checks by setup_fragmentation_beam() must guarantee that we have a
        // monster.
        monster* mon = monster_at(target);
        ASSERT(mon);

        const int dam = beam.damage.roll();
        if (you.see_cell(target))
        {
            mprf("%s shatters%s", mon->name(DESC_THE).c_str(),
                 attack_strength_punctuation(dam).c_str());
        }

        if (caster->is_player())
            _player_hurt_monster(*mon, dam, BEAM_DISINTEGRATION);
        else if (dam)
            mon->hurt(caster, dam, BEAM_DISINTEGRATION);
    }

    beam.explode(true, hole);

    return spret::success;
}

spret cast_sandblast(int pow, bolt &beam, bool fail)
{
    item_def *stone = nullptr;
    int num_stones = 0;
    for (item_def& i : you.inv)
    {
        if (i.is_type(OBJ_MISSILES, MI_STONE)
            && check_warning_inscriptions(i, OPER_DESTROY))
        {
            num_stones += i.quantity;
            stone = &i;
        }
    }

    if (num_stones == 0)
    {
        mpr("You don't have any stones to cast with.");
        return spret::abort;
    }

    zap_type zap = ZAP_SANDBLAST;
    const spret ret = zapping(zap, pow, beam, true, nullptr, fail);

    if (ret == spret::success)
    {
        if (dec_inv_item_quantity(letter_to_index(stone->slot), 1))
            mpr("You now have no stones remaining.");
        else
            mprf_nocap("%s", stone->name(DESC_INVENTORY).c_str());
    }

    return ret;
}

static bool _elec_not_immune(const actor *act)
{
    return act->res_elec() < 3 && !god_protects(act->as_monster());
}

spret cast_thunderbolt(actor *caster, int pow, coord_def aim, bool fail)
{
    coord_def prev;

    int &charges = caster->props[THUNDERBOLT_CHARGES_KEY].get_int();
    ASSERT(charges <= LIGHTNING_MAX_CHARGE);

    int &last_turn = caster->props[THUNDERBOLT_LAST_KEY].get_int();
    coord_def &last_aim = caster->props[THUNDERBOLT_AIM_KEY].get_coord();


    if (last_turn && last_turn + 1 == you.num_turns)
        prev = last_aim;
    else
        charges = 0;

    targeter_thunderbolt hitfunc(caster, spell_range(SPELL_THUNDERBOLT, pow),
                                 prev);
    hitfunc.set_aim(aim);

    if (!you.is_auto_spell()
        && caster->is_player()
        && stop_attack_prompt(hitfunc, "zap", _elec_not_immune))
    {
        return spret::abort;
    }

    fail_check();

    const int juice
        = (spell_mana(SPELL_THUNDERBOLT, false) + charges) * ROD_CHARGE_MULT;

    dprf("juice: %d", juice);

    bolt beam;
    beam.name              = "thunderbolt";
    beam.aux_source        = "lightning rod";
    beam.flavour           = BEAM_ELECTRICITY;
    beam.glyph             = dchar_glyph(DCHAR_FIRED_BURST);
    beam.colour            = LIGHTCYAN;
    beam.range             = 1;
    beam.hit               = AUTOMATIC_HIT;
    beam.ac_rule           = ac_type::proportional;
    beam.set_agent(caster);
#ifdef USE_TILE
    beam.tile_beam = -1;
#endif
    beam.draw_delay = 0;

    for (const auto &entry : hitfunc.zapped)
    {
        if (entry.second <= 0)
            continue;

        beam.draw(entry.first);
    }

    scaled_delay(200);

    beam.glyph = 0; // FIXME: a hack to avoid "appears out of thin air"

    for (const auto &entry : hitfunc.zapped)
    {
        if (entry.second <= 0)
            continue;

        // beams are incredibly spammy in debug mode
        if (!actor_at(entry.first))
            continue;

        int arc = hitfunc.arc_length[entry.first.distance_from(hitfunc.origin)];
        ASSERT(arc > 0);
        dprf("at distance %d, arc length is %d",
             entry.first.distance_from(hitfunc.origin), arc);
        beam.source = beam.target = entry.first;
        beam.source.x -= sgn(beam.source.x - hitfunc.origin.x);
        beam.source.y -= sgn(beam.source.y - hitfunc.origin.y);
        beam.damage = dice_def(div_rand_round(juice, ROD_CHARGE_MULT),
                               div_rand_round(30 + pow / 6, arc + 2));
        beam.fire();
    }

    last_turn = you.num_turns;
    last_aim = aim;
    if (charges < LIGHTNING_MAX_CHARGE)
        charges++;

    return spret::success;
}

// Find an enemy who would suffer from Awaken Forest.
actor* forest_near_enemy(const actor *mon)
{
    const coord_def pos = mon->pos();

    for (radius_iterator ri(pos, LOS_NO_TRANS); ri; ++ri)
    {
        actor* foe = actor_at(*ri);
        if (!foe || mons_aligned(foe, mon))
            continue;

        for (adjacent_iterator ai(*ri); ai; ++ai)
            if (feat_is_tree(grd(*ai)) && cell_see_cell(pos, *ai, LOS_DEFAULT))
                return foe;
    }

    return nullptr;
}

// Print a message only if you can see any affected trees.
void forest_message(const coord_def pos, const string &msg, msg_channel_type ch)
{
    for (radius_iterator ri(pos, LOS_DEFAULT); ri; ++ri)
        if (feat_is_tree(grd(*ri))
            && cell_see_cell(you.pos(), *ri, LOS_DEFAULT))
        {
            mprf(ch, "%s", msg.c_str());
            return;
        }
}

void forest_damage(const actor *mon)
{
    const coord_def pos = mon->pos();
    const int hd = mon->get_hit_dice();

    if (one_chance_in(4))
    {
        forest_message(pos, random_choose(
            "The trees move their gnarly branches around.",
            "You feel roots moving beneath the ground.",
            "Branches wave dangerously above you.",
            "Trunks creak and shift.",
            "Tree limbs sway around you."), MSGCH_TALK_VISUAL);
    }

    for (radius_iterator ri(pos, LOS_NO_TRANS); ri; ++ri)
    {
        actor* foe = actor_at(*ri);
        if (!foe || mons_aligned(foe, mon))
            continue;

        if (is_sanctuary(foe->pos()))
            continue;

        for (adjacent_iterator ai(*ri); ai; ++ai)
            if (feat_is_tree(grd(*ai)) && cell_see_cell(pos, *ai, LOS_NO_TRANS))
            {
                int dmg = 0;
                string msg;

                if (!apply_chunked_AC(1, foe->evasion(ev_ignore::none, mon)))
                {
                    msg = random_choose(
                            "@foe@ @is@ waved at by a branch",
                            "A tree reaches out but misses @foe@",
                            "A root lunges up near @foe@");
                }
                else if (!(dmg = foe->apply_ac(hd + random2(hd), hd * 2 - 1,
                                               ac_type::proportional)))
                {
                    msg = random_choose(
                            "@foe@ @is@ scraped by a branch",
                            "A tree reaches out and scrapes @foe@",
                            "A root barely touches @foe@ from below");
                    if (foe->is_monster())
                        behaviour_event(foe->as_monster(), ME_WHACK);
                }
                else
                {
                    msg = random_choose(
                        "@foe@ @is@ hit by a branch",
                        "A tree reaches out and hits @foe@",
                        "A root smacks @foe@ from below");
                    if (foe->is_monster())
                        behaviour_event(foe->as_monster(), ME_WHACK);
                }

                msg = replace_all(replace_all(msg,
                    "@foe@", foe->name(DESC_THE)),
                    "@is@", foe->conj_verb("be"))
                    + attack_strength_punctuation(dmg);
                if (you.see_cell(foe->pos()))
                    mpr(msg);

                if (dmg <= 0)
                    break;

                foe->hurt(mon, dmg, BEAM_MISSILE, KILLED_BY_BEAM, "",
                          "by angry trees");

                break;
            }
    }
}

vector<bolt> get_spray_rays(const actor *caster, coord_def aim, int range,
                            int max_rays, int max_spacing)
{
    coord_def aim_dir = (caster->pos() - aim).sgn();

    int num_targets = 0;
    vector<bolt> beams;

    bolt base_beam;

    base_beam.set_agent(caster);
    base_beam.attitude = caster->is_player() ? ATT_FRIENDLY
                                             : caster->as_monster()->attitude;
    base_beam.is_tracer = true;
    base_beam.is_targeting = true;
    base_beam.dont_stop_player = true;
    base_beam.friend_info.dont_stop = true;
    base_beam.foe_info.dont_stop = true;
    base_beam.range = range;
    base_beam.source = caster->pos();
    base_beam.target = aim;
    base_beam.is_spread = true;

    bolt center_beam = base_beam;
    center_beam.hit = AUTOMATIC_HIT;
    center_beam.fire();
    center_beam.target = center_beam.path_taken.back();
    center_beam.hit = 1;
    center_beam.fire();
    center_beam.is_tracer = false;
    center_beam.dont_stop_player = false;
    center_beam.foe_info.dont_stop = false;
    center_beam.friend_info.dont_stop = false;
    // Prevent self-hits, specifically when you aim at an adjacent wall.
    if (center_beam.path_taken.back() != caster->pos())
        beams.push_back(center_beam);

    for (distance_iterator di(aim, false, false, max_spacing); di; ++di)
    {
        if (monster_at(*di))
        {
            coord_def delta = caster->pos() - *di;

            //Don't aim secondary rays at friendlies
            if (mons_aligned(caster, monster_at(*di)))
                continue;

            if (!caster->can_see(*monster_at(*di)))
                continue;

            //Don't try to aim at a target if it's out of range
            if (delta.rdist() > range)
                continue;

            //Don't try to aim at targets in the opposite direction of main aim
            if (abs(aim_dir.x - delta.sgn().x) + abs(aim_dir.y - delta.sgn().y) >= 2)
                continue;

            //Test if this beam stops at a location used by any prior beam
            bolt testbeam = base_beam;
            testbeam.target = *di;
            testbeam.hit = AUTOMATIC_HIT;
            testbeam.fire();
            bool duplicate = false;

            for (const bolt &beam : beams)
            {
                if (testbeam.path_taken.back() == beam.target)
                {
                    duplicate = true;
                    continue;
                }
            }
            if (!duplicate)
            {
                bolt tempbeam = base_beam;
                tempbeam.target = testbeam.path_taken.back();
                tempbeam.fire();
                tempbeam.is_tracer = false;
                tempbeam.is_targeting = false;
                tempbeam.dont_stop_player = false;
                tempbeam.foe_info.dont_stop = false;
                tempbeam.friend_info.dont_stop = false;
                beams.push_back(tempbeam);
                num_targets++;
            }

            if (num_targets == max_rays - 1)
              break;
        }
    }

    return beams;
}

static bool _dazzle_can_hit(const actor *act)
{
    if (act->is_monster())
    {
        const monster* mons = act->as_monster();
        bolt testbeam;
        testbeam.thrower = KILL_YOU;
        zappy(ZAP_DAZZLING_SPRAY, 100, false, testbeam);

        return !testbeam.ignores_monster(mons);
    }
    else
        return false;
}

spret cast_dazzling_spray(int pow, coord_def aim, bool fail)
{
    int range = spell_range(SPELL_DAZZLING_SPRAY, pow);

    targeter_spray hitfunc(&you, range, ZAP_DAZZLING_SPRAY);
    hitfunc.set_aim(aim);
    if (!you.is_auto_spell() && stop_attack_prompt(hitfunc, "fire towards", _dazzle_can_hit))
        return spret::abort;

    fail_check();

    if (hitfunc.beams.size() == 0)
    {
        mpr("You can't see any targets in that direction!");
        return spret::abort;
    }

    for (bolt &beam : hitfunc.beams)
    {
        zappy(ZAP_DAZZLING_SPRAY, pow, false, beam);
        beam.fire();
    }

    return spret::success;
}

bool toxic_can_affect(const actor *act)
{
    if (act->is_monster() && act->as_monster()->submerged())
        return false;

    // currently monsters are still immune at rPois 1
    return act->res_poison() < (act->is_player() ? 3 : 1);
}

spret cast_toxic_radiance(actor *agent, int pow, bool fail, bool mon_tracer)
{
    if (agent->is_player())
    {
        targeter_radius hitfunc(&you, LOS_NO_TRANS);
        {
            if (!you.is_auto_spell() && stop_attack_prompt(hitfunc, "poison", toxic_can_affect))
                return spret::abort;
        }
        fail_check();

        if (!you.duration[DUR_TOXIC_RADIANCE])
            mpr("You begin to radiate toxic energy.");
        else
            mpr("Your toxic radiance grows in intensity.");

        you.increase_duration(DUR_TOXIC_RADIANCE, 2 + random2(pow/20), 15);
        toxic_radiance_effect(&you, 10, true);

        flash_view_delay(UA_PLAYER, GREEN, 300, &hitfunc);

        return spret::success;
    }
    else if (mon_tracer)
    {
        for (actor_near_iterator ai(agent->pos(), LOS_NO_TRANS); ai; ++ai)
        {
            if (!toxic_can_affect(*ai) || mons_aligned(agent, *ai))
                continue;
            else
                return spret::success;
        }

        // Didn't find any susceptible targets
        return spret::abort;
    }
    else
    {
        monster* mon_agent = agent->as_monster();
        simple_monster_message(*mon_agent,
                               " begins to radiate toxic energy.");

        mon_agent->add_ench(mon_enchant(ENCH_TOXIC_RADIANCE, 1, mon_agent,
                                        (4 + random2avg(pow/15, 2)) * BASELINE_DELAY));
        toxic_radiance_effect(agent, 10);

        targeter_radius hitfunc(mon_agent, LOS_NO_TRANS);
        flash_view_delay(UA_MONSTER, GREEN, 300, &hitfunc);

        return spret::success;
    }
}

/*
 * Attempt to poison all monsters in line of sight of the caster.
 *
 * @param agent   The caster.
 * @param mult    A number to multiply the damage by.
 *                This is the time taken for the player's action in auts,
 *                or 10 if the spell was cast this turn.
 * @param on_cast Whether the spell was cast this turn. This only matters
 *                if the player cast the spell. If true, we trigger conducts
 *                if the player hurts allies; if false, we don't, to avoid
 *                the player being accidentally put under penance.
 *                Defaults to false.
 */
void toxic_radiance_effect(actor* agent, int mult, bool on_cast)
{
    int pow;
    if (agent->is_player())
        pow = calc_spell_power(SPELL_OLGREBS_TOXIC_RADIANCE, true);
    else
        pow = agent->as_monster()->get_hit_dice() * 8;

    bool break_sanctuary = (agent->is_player() && is_sanctuary(you.pos()));

    for (actor_near_iterator ai(agent->pos(), LOS_NO_TRANS); ai; ++ai)
    {
        if (!toxic_can_affect(*ai))
            continue;

        // Monsters can skip hurting friendlies
        if (agent->is_monster() && mons_aligned(agent, *ai))
            continue;

        int dam = roll_dice(1, 1 + pow / 20) * div_rand_round(mult, BASELINE_DELAY);
        dam = resist_adjust_damage(*ai, BEAM_POISON, dam);

        if (ai->is_player())
        {
            // We're affected only if we're not the agent.
            if (!agent->is_player())
            {
                ouch(dam, KILLED_BY_BEAM, agent->mid,
                    "by Olgreb's Toxic Radiance", true,
                    agent->as_monster()->name(DESC_A).c_str());

                poison_player(roll_dice(2, 3), agent->name(DESC_A),
                              "toxic radiance", false);
            }
        }
        else
        {
            // We need to deal with conducts before damaging the monster,
            // because otherwise friendly monsters that are one-shot won't
            // trigger conducts. Only trigger conducts on the turn the player
            // casts the spell (see PR #999).
            if (on_cast && agent->is_player())
            {
                god_conduct_trigger conducts[3];
                set_attack_conducts(conducts, *ai->as_monster());
                if (is_sanctuary(ai->pos()))
                    break_sanctuary = true;
            }

            ai->hurt(agent, dam, BEAM_POISON);

            if (ai->alive())
            {
                behaviour_event(ai->as_monster(), ME_ANNOY, agent,
                                agent->pos());
                int q = mult / BASELINE_DELAY;
                int levels = roll_dice(q, 2) - q + (roll_dice(1, 20) <= (mult % BASELINE_DELAY));
                if (!ai->as_monster()->has_ench(ENCH_POISON)) // Always apply poison to an unpoisoned enemy
                    levels = max(levels, 1);
                poison_monster(ai->as_monster(), agent, levels);
            }
        }
    }

    if (break_sanctuary)
        remove_sanctuary(true);
}

spret cast_searing_ray(int pow, bolt &beam, bool fail)
{
    const spret ret = zapping(ZAP_SEARING_RAY_I, pow, beam, true, nullptr,
                                   fail);

    if (ret == spret::success)
    {
        // Special value, used to avoid terminating ray immediately, since we
        // took a non-wait action on this turn (ie: casting it)
        you.attribute[ATTR_SEARING_RAY] = -1;
        you.props["searing_ray_target"].get_coord() = beam.target;
        you.props["searing_ray_aimed_at_spot"].get_bool() = beam.aimed_at_spot;
        string msg = "(Press <w>%</w> to maintain the ray.)";
        insert_commands(msg, { CMD_WAIT });
        mpr(msg);
    }

    return ret;
}

void handle_searing_ray()
{
    if (you.attribute[ATTR_SEARING_RAY] == 0)
        return;

    // Convert prepping value into stage one value (so it can fire next turn)
    if (you.attribute[ATTR_SEARING_RAY] == -1)
    {
        you.attribute[ATTR_SEARING_RAY] = 1;
        return;
    }

    if (crawl_state.prev_cmd != CMD_WAIT)
        end_searing_ray();

    ASSERT_RANGE(you.attribute[ATTR_SEARING_RAY], 1, 4);

    // All of these effects interrupt a channeled ray
    if (you.confused() || you.berserk())
    {
        end_searing_ray();
        return;
    }

    if (!enough_mp(1, true))
    {
        mpr("Without enough magic to sustain it, your searing ray dissipates.");
        end_searing_ray();
        return;
    }

    const zap_type zap = zap_type(ZAP_SEARING_RAY_I + (you.attribute[ATTR_SEARING_RAY]-1));
    const int pow = calc_spell_power(SPELL_SEARING_RAY, true);

    bolt beam;
    beam.thrower = KILL_YOU_MISSILE;
    beam.range   = calc_spell_range(SPELL_SEARING_RAY, pow);
    beam.source  = you.pos();
    beam.target  = you.props["searing_ray_target"].get_coord();
    beam.aimed_at_spot = you.props["searing_ray_aimed_at_spot"].get_bool();

    // If friendlies have moved into the beam path, give a chance to abort
    if (!player_tracer(zap, pow, beam))
    {
        mpr("You stop channeling your searing ray.");
        end_searing_ray();
        return;
    }

    zappy(zap, pow, false, beam);

    aim_battlesphere(&you, SPELL_SEARING_RAY, pow, beam);
    aim_battlesphere(&you, SPELL_SEARING_RAY, pow, beam, true);
    beam.fire();
    trigger_battlesphere(&you, beam);
    trigger_battlesphere(&you, beam, true);

    dec_mp(1);

    if (++you.attribute[ATTR_SEARING_RAY] > 3)
    {
        mpr("You finish channeling your searing ray.");
        end_searing_ray();
    }
}

void end_searing_ray()
{
    you.attribute[ATTR_SEARING_RAY] = 0;
    you.props.erase("searing_ray_target");
    you.props.erase("searing_ray_aimed_at_spot");
}

void end_wall_invisible()
{
    if (you.props[WALL_INVISIBLE_KEY].get_bool()) {
        mpr("The assimilate with the wall has been temporarily fading.");
        you.props.erase(WALL_INVISIBLE_KEY);
    }
}

/**
 * Can a casting of Glaciate by the player injure the given creature?
 *
 * @param victim        The potential victim.
 * @return              Whether Glaciate can harm that victim.
 *                      (False for IOODs or friendly battlespheres.)
 */
static bool _player_glaciate_affects(const actor *victim)
{
    // TODO: deduplicate this with beam::ignores
    if (!victim)
        return false;

    const monster* mon = victim->as_monster();
    if (!mon) // player
        return true;

    return !mons_is_projectile(*mon)
            && (!mons_is_avatar(mon->type) || !mons_aligned(&you, mon));
}

dice_def glaciate_damage(int pow, int eff_range)
{
    // At or within range 3, this is equivalent to the old Ice Storm damage.
    return calc_dice(10, (54 + 3 * pow / 2) / eff_range);
}

spret cast_glaciate(actor *caster, int pow, coord_def aim, bool fail)
{
    const int range = spell_range(SPELL_GLACIATE, pow);
    targeter_cone hitfunc(caster, range);
    hitfunc.set_aim(aim);

    if (caster->is_player()
        && !you.is_auto_spell()
        && stop_attack_prompt(hitfunc, "glaciate", _player_glaciate_affects))
    {
        return spret::abort;
    }

    fail_check();

    bolt beam;
    beam.name              = "great icy blast";
    beam.aux_source        = "great icy blast";
    beam.flavour           = BEAM_ICE;
    beam.glyph             = dchar_glyph(DCHAR_EXPLOSION);
    beam.colour            = WHITE;
    beam.range             = 1;
    beam.hit               = AUTOMATIC_HIT;
    beam.source_id         = caster->mid;
    beam.hit_verb          = "engulfs";
    beam.origin_spell      = SPELL_GLACIATE;
    beam.set_agent(caster);
#ifdef USE_TILE
    beam.tile_beam = -1;
#endif
    beam.draw_delay = 0;

    for (int i = 1; i <= range; i++)
    {
        for (const auto &entry : hitfunc.sweep[i])
        {
            if (entry.second <= 0)
                continue;

            beam.draw(entry.first);
        }
        scaled_delay(25);
    }

    scaled_delay(100);

    if (you.can_see(*caster) || caster->is_player())
    {
        mprf("%s %s a mighty blast of ice!",
             caster->name(DESC_THE).c_str(),
             caster->conj_verb("conjure").c_str());
    }

    beam.glyph = 0;

    for (int i = 1; i <= range; i++)
    {
        for (const auto &entry : hitfunc.sweep[i])
        {
            if (entry.second <= 0)
                continue;

            const int eff_range = max(3, (6 * i / LOS_DEFAULT_RANGE));

            beam.damage = glaciate_damage(pow, eff_range);

            if (actor_at(entry.first))
            {
                beam.source = beam.target = entry.first;
                beam.source.x -= sgn(beam.source.x - hitfunc.origin.x);
                beam.source.y -= sgn(beam.source.y - hitfunc.origin.y);
                beam.fire();
            }
            place_cloud(CLOUD_COLD, entry.first,
                        (18 + random2avg(45,2)) / eff_range, caster);
        }
    }

    noisy(spell_effect_noise(SPELL_GLACIATE), hitfunc.origin);

    return spret::success;
}

spret cast_random_bolt(int pow, bolt& beam, bool fail)
{
    // Need to use a 'generic' tracer regardless of the actual beam type,
    // to account for the possibility of both bouncing and irresistible damage
    // (even though only one of these two ever occurs on the same bolt type).
    bolt tracer = beam;
    if (!player_tracer(ZAP_RANDOM_BOLT_TRACER, 200, tracer))
        return spret::abort;

    fail_check();

    zap_type zap = random_choose(ZAP_BOLT_OF_FIRE,
                                 ZAP_BOLT_OF_COLD,
                                 ZAP_VENOM_BOLT,
                                 ZAP_BOLT_OF_DRAINING,
                                 ZAP_QUICKSILVER_BOLT,
                                 ZAP_CRYSTAL_BOLT,
                                 ZAP_LIGHTNING_BOLT,
                                 ZAP_CORROSIVE_BOLT);
    beam.origin_spell = SPELL_NO_SPELL; // let zapping reset this
    zapping(zap, pow * 7 / 6 + 15, beam, false);

    return spret::success;
}

size_t shotgun_beam_count(int pow)
{
    return 1 + stepdown((pow - 5) / 3, 5, ROUND_CLOSE);
}

spret cast_scattershot(const actor *caster, int pow, const coord_def &pos,
                            bool fail)
{
    const size_t range = spell_range(SPELL_SCATTERSHOT, pow);
    const size_t beam_count = shotgun_beam_count(pow);

    targeter_shotgun hitfunc(caster, beam_count, range);

    hitfunc.set_aim(pos);

    if (caster->is_player())
    {
        if (!you.is_auto_spell() && stop_attack_prompt(hitfunc, "scattershot"))
            return spret::abort;
    }

    fail_check();

    bolt beam;
    beam.thrower = (caster && caster->is_player()) ? KILL_YOU :
                   (caster)                        ? KILL_MON
                                                   : KILL_MISC;
    beam.range       = range;
    beam.source      = caster->pos();
    beam.source_id   = caster->mid;
    beam.source_name = caster->name(DESC_PLAIN, true);
    zappy(ZAP_SCATTERSHOT, pow, false, beam);
    beam.aux_source  = beam.name;

    if (!caster->is_player())
        beam.damage   = dice_def(3, 4 + (pow / 18));

    // Choose a random number of 'pellets' to fire for each beam in the spread.
    // total pellets has O(beam_count^2)
    vector<size_t> pellets;
    pellets.resize(beam_count);
    for (size_t i = 0; i < beam_count; i++)
        pellets[random2avg(beam_count, 3)]++;

    map<mid_t, int> hit_count;

    // for each beam of pellets...
    for (size_t i = 0; i < beam_count; i++)
    {
        // find the beam's path.
        ray_def ray = hitfunc.rays[i];
        for (size_t j = 0; j < range; j++)
            ray.advance();

        // fire the beam once per pellet.
        for (size_t j = 0; j < pellets[i]; j++)
        {
            bolt tempbeam = beam;
            tempbeam.draw_delay = 0;
            tempbeam.target = ray.pos();
            tempbeam.fire();
            scaled_delay(5);
            for (auto it : tempbeam.hit_count)
               hit_count[it.first] += it.second;
        }
    }

    for (auto it : hit_count)
    {
        if (it.first == MID_PLAYER)
            continue;

        monster* mons = monster_by_mid(it.first);
        if (!mons || !mons->alive() || !you.can_see(*mons))
            continue;

        print_wounds(*mons);
    }

    return spret::success;
}

static void _setup_borgnjors_vile_clutch(bolt &beam, int pow)
{
    beam.name         = "vile clutch";
    beam.aux_source   = "vile_clutch";
    beam.flavour      = BEAM_VILE_CLUTCH;
    beam.glyph        = dchar_glyph(DCHAR_FIRED_BURST);
    beam.colour       = GREEN;
    beam.source_id    = MID_PLAYER;
    beam.thrower      = KILL_YOU;
    beam.is_explosion = true;
    beam.ex_size      = 1;
    beam.ench_power   = pow;
    beam.origin_spell = SPELL_BORGNJORS_VILE_CLUTCH;
}

spret cast_borgnjors_vile_clutch(int pow, bolt &beam, bool fail)
{
    if (cell_is_solid(beam.target))
    {
        canned_msg(MSG_SOMETHING_IN_WAY);
        return spret::abort;
    }

    fail_check();

    _setup_borgnjors_vile_clutch(beam, pow);
    mpr("Decaying hands burst forth from the earth!");
    beam.explode();

    return spret::success;
}



spret cast_eringyas_rootspike(int splpow, const dist& beam, bool fail)
{
    if (cell_is_solid(beam.target))
    {
        canned_msg(MSG_UNTHINKING_ACT);
        return spret::abort;
    }

    monster* mons = monster_at(beam.target);
    if (!mons || mons->submerged())
    {
        fail_check();
        canned_msg(MSG_SPELL_FIZZLES);
        return spret::success; // still losing a turn
    }

    if (!you.is_auto_spell()
        && !god_protects(mons)
        && stop_attack_prompt(mons, false, you.pos()))
    {
        return spret::abort;
    }
    fail_check();

    god_conduct_trigger conducts[3];
    set_attack_conducts(conducts, *mons, you.can_see(*mons));

    noisy(spell_effect_noise(SPELL_ERINGYAS_ROOTSPIKE), beam.target);


    bolt pbeam;
    zappy(ZAP_ERINYA_ROOT_SPIKE, splpow, false, pbeam);

    int damage = pbeam.damage.roll();
    // old: 9 + random2(3 + div_rand_round(splpow, 4));
    // new: 2d(10+sp/8)
#ifdef USE_TILE
    pbeam.tile_beam = -1;
#endif
    pbeam.draw_delay = 0;
    damage = mons_adjust_flavoured(mons, pbeam, damage);

    if (you.can_see(*mons))
    {
        mprf("Poisonous roots encircled %s%s%s",
            mons->name(DESC_THE).c_str(),
            damage ? "" : " but does no damage",
            attack_strength_punctuation(damage).c_str());
    }
    
    pbeam.draw(beam.target);
    scaled_delay(200);
    pbeam.glyph = 0; // FIXME: a hack to avoid "appears out of thin air"
    mons->hurt(&you, damage, BEAM_POISON_ERINYA);

    if (you.can_constrict(mons, false)) {
        const int dur = (4 + random2avg(div_rand_round(splpow, 10), 2))
            * BASELINE_DELAY;
        mons->add_ench(mon_enchant(ENCH_ERINGYAS_ROOTSPIKE, 0, &you, dur));
    }

    return spret::success;
}
spret cast_olgrebs_last_mercy(int pow, const dist& dist, bool fail)
{
    monster* mon = monster_at(dist.target);
    if (!mon || !mon->alive())
        return spret::abort;

    fail_check();

    const mon_enchant ench = mon->get_ench(ENCH_POISON);
    const int pois_str = ench.ench == ENCH_NONE ? 0 : ench.degree;

    if (pois_str == 0) {
        canned_msg(MSG_SPELL_FIZZLES);
        return spret::success;
    }

    bolt mbeam;
    zappy(ZAP_OLGREB_LAST_MERCY, pow, false, mbeam);
#ifdef USE_TILE
    mbeam.tile_beam = -1;
#endif
    mbeam.draw_delay = 0;
    int base_dam = 0;
    for (int i = 0; i < pois_str; i++) {
        base_dam += mbeam.damage.roll();
    }
    const int damage = mons_adjust_flavoured(mon, mbeam, base_dam, false);

    const int max_hp = mon->max_hit_points;
    mbeam.draw(dist.target);
    scaled_delay(200);
    mbeam.glyph = 0; // FIXME: a hack to avoid "appears out of thin air"

    mon->hurt(&you, damage);

    if (you.can_see(*mon))
    {
        mprf("Poison explode in the %s's body%s%s",
            mon->name(DESC_THE).c_str(),
            damage ? "" : " but does no damage",
            attack_strength_punctuation(damage).c_str());
    }

    if (mon->alive())
    {


        behaviour_event(mon, ME_WHACK, &you);

        // Monster survived, remove any poison.
        mon->del_ench(ENCH_POISON, true); // suppress spam
        print_wounds(*mon);
    }
    else {
        bolt beam;
        beam.name = "burst of toxic";
        beam.flavour = BEAM_MMISSILE;
        beam.set_agent(&you);
        beam.colour = LIGHTGREEN;
        beam.glyph = dchar_glyph(DCHAR_EXPLOSION);
        beam.range = 1;
        beam.ex_size = 1;
        beam.is_explosion = true;
        beam.damage = calc_dice(4, max_hp * 2 + 6 + div_rand_round(pow, 40));
        //beam.explode_delay = beam.explode_delay * 3 / 2;
        beam.source = dist.target;
        beam.target = dist.target;
        beam.hit = AUTOMATIC_HIT;
        beam.loudness = spell_effect_noise(SPELL_OLGREBS_LAST_MERCY);
        beam.explode(true);
    }

    return spret::success;

}

spret cast_pakellas_bolt(int powc, bolt& beam, bool fail)
{
    if (you.religion != GOD_PAKELLAS) {
        mprf("You cannot use it if you do not believe pakellas.");
        return spret::success;
    }
    // Need to use a 'generic' tracer regardless of the actual beam type,
    // to account for the possibility of both bouncing and irresistible damage
    // (even though only one of these two ever occurs on the same bolt type).
    bolt tracer = beam;
    if (!player_tracer(is_blueprint_exist(BLUEPRINT_BOME) ? ZAP_EXPLOSION_TRACER : ZAP_MAGIC_DART, 200, tracer))
        return spret::abort;



    fail_check();
    
    float multiple = 1.0f;



    bolt pbolt = beam;
    pbolt.name = "magic bolt";
    pbolt.thrower = KILL_YOU_MISSILE;
    pbolt.flavour = BEAM_MMISSILE;
    pbolt.real_flavour = BEAM_MMISSILE;
    pbolt.colour = LIGHTMAGENTA;
    pbolt.glyph = dchar_glyph(DCHAR_FIRED_ZAP);

    if (is_blueprint_exist(BLUEPRINT_ELEMENTAL_FIRE)) {
        pbolt.name = "fire bolt";
        pbolt.flavour = BEAM_ROD_FIRE;
        pbolt.real_flavour = BEAM_ROD_FIRE;
        pbolt.colour = RED;
        pbolt.glyph = dchar_glyph(DCHAR_FIRED_ZAP);
        multiple *= 1.2f;
    }
    else if (is_blueprint_exist(BLUEPRINT_ELEMENTAL_COLD)) {
        pbolt.name = "cold bolt";
        pbolt.flavour = BEAM_ROD_COLD;
        pbolt.real_flavour = BEAM_ROD_COLD;
        pbolt.colour = BLUE; 
        pbolt.glyph = dchar_glyph(DCHAR_FIRED_ZAP);
        multiple *= 1.2f;
    }
    else if (is_blueprint_exist(BLUEPRINT_ELEMENTAL_ELEC)) {
        pbolt.name = "electricity bolt";
        pbolt.flavour = BEAM_ROD_ELEC;
        pbolt.real_flavour = BEAM_ROD_ELEC;
        pbolt.colour = YELLOW;
        pbolt.glyph = dchar_glyph(DCHAR_FIRED_ZAP);
        multiple *= 1.2f;
    }
    else if (is_blueprint_exist(BLUEPRINT_ELEMENTAL_EARTH)) {
        pbolt.name = "stone bolt";
        pbolt.colour = BROWN;
        pbolt.glyph = dchar_glyph(DCHAR_EXPLOSION);
        multiple *= 1.2f;
    }
    else if (is_blueprint_exist(BLUEPRINT_ELEMENTAL_POISON)) {
        pbolt.name = "poison bolt";
        pbolt.flavour = BEAM_ROD_POISON;
        pbolt.real_flavour = BEAM_ROD_POISON;
        pbolt.colour = GREEN;
        pbolt.glyph = dchar_glyph(DCHAR_FIRED_ZAP);
        multiple *= 1.2f;
    }
    else if (is_blueprint_exist(BLUEPRINT_CHAOS)) {
        pbolt.name = "choas bolt";
        pbolt.flavour = BEAM_CHAOS;
        pbolt.real_flavour = BEAM_CHAOS;
        pbolt.colour = BLUE;
        pbolt.glyph = dchar_glyph(DCHAR_FIRED_BOLT);
        multiple *= 1.3f;
    }


    pbolt.hit_func = [&powc, &pbolt, &multiple](monster* mon, bool in_explosion) {
        if (mon == nullptr)
            return;

        if (is_blueprint_exist(BLUEPRINT_BOME) && !in_explosion) {
            return;
        }

        bool resist = 0;
        if (mon->alive() && is_blueprint_exist(BLUEPRINT_DEBUF_SLOW)) {
            bolt beam_;
            beam_.flavour = BEAM_SLOW;
            beam_.ench_power = powc;
            int unused; // res_margin
            beam_.try_enchant_monster(mon, unused);
            if (unused > 0 && (resist == 0 || unused < resist))
                resist = unused;
            else
                resist = -1;
        }

        if (mon->alive() && is_blueprint_exist(BLUEPRINT_STICKY_FLAME)) {
            mon->add_ench(mon_enchant(ENCH_STICKY_FLAME,
                min(4, 1 + random2(mon->get_hit_dice()) / 2),
                &you));
        }

        if (mon->alive() && is_blueprint_exist(BLUEPRINT_CHAIN_LIGHTNING)) {
            coord_def source, target;
            source = mon->pos();
            int min_dist = LOS_DEFAULT_RANGE - 1;

            int dist;
            int count = 0;

            target.x = -1;
            target.y = -1;

            for (monster_iterator mi; mi; ++mi)
            {
                if (invalid_monster(*mi))
                    continue;

                // Don't arc to things we cannot hit.
                if (pbolt.ignores_monster(*mi))
                    continue;

                dist = grid_distance(source, mi->pos());

                // check for the source of this arc
                if (!dist)
                    continue;

                // randomise distance (arcs don't care about a couple of feet)
                dist += (random2(3) - 1);

                // always ignore targets further than current one
                if (dist > min_dist)
                    continue;

                if (!cell_see_cell(source, mi->pos(), LOS_SOLID)
                    || !cell_see_cell(you.pos(), mi->pos(), LOS_SOLID_SEE))
                {
                    continue;
                }

                // check for actors along the arc path
                ray_def ray;
                if (!find_ray(source, mi->pos(), ray, opc_solid))
                    continue;

                while (ray.advance())
                    if (actor_at(ray.pos()))
                        break;

                if (ray.pos() != mi->pos())
                    continue;

                count++;

                if (dist < min_dist)
                {
                    // switch to looking for closer targets (but not always)
                    if (!one_chance_in(10))
                    {
                        min_dist = dist;
                        target = mi->pos();
                        count = 0;
                    }
                }
                else if (target.x == -1 || one_chance_in(count))
                {
                    // either first target, or new selected target at
                    // min_dist == dist.
                    target = mi->pos();
                }
            }
            if (target.x == -1)
            {
                return;
            }

            bolt beam_;
            beam_.name = "lightning arc";
            beam_.aux_source = "chain lightning";


            beam_.glyph = dchar_glyph(DCHAR_FIRED_ZAP);
            beam_.flavour = BEAM_ELECTRICITY;
            beam_.source_id = you.mid;
            beam_.thrower = KILL_YOU_MISSILE;
            beam_.range = 8;
            beam_.hit = AUTOMATIC_HIT;
            beam_.obvious_effect = true;
            beam_.pierce = false;       // since we want to stop at our target
            beam_.is_explosion = false;
            beam_.is_tracer = false;
            beam_.origin_spell = SPELL_CHAIN_LIGHTNING;
            beam_.source = source;
            beam_.target = target;
            beam_.colour = LIGHTBLUE;
            beam_.damage = calc_dice(5, ((6 + powc * 3 / 4)*8/10) * multiple); //80%

            // Be kinder to the caster.
            if (target == you.pos())
            {
                // Reduce damage when the spell arcs to the caster.
                beam_.damage.num = max(1, beam_.damage.num / 2);
                beam_.damage.size = max(3, beam_.damage.size / 2);
            }
            beam_.fire();
        }


        if (mon->alive() && is_blueprint_exist(BLUEPRINT_FROZEN)) {
            mon->add_ench(mon_enchant(ENCH_FROZEN, 0, &you, 6 + random2(16) * BASELINE_DELAY));
        }

        if (mon->alive() && is_blueprint_exist(BLUEPRINT_DEBUF_BLIND)) {
            if (mons_can_be_dazzled(mon->type)) {
                if (x_chance_in_y(95 - mon->get_hit_dice() * 5, 100))
                {
                    simple_monster_message(*mon, " is dazzled.");
                    mon->add_ench(mon_enchant(ENCH_BLIND, 1, &you,
                        random_range(4, 8) * BASELINE_DELAY));
                }
            }
        }

        if (mon->alive() && is_blueprint_exist(BLUEPRINT_DEFORM)) {
            mon->malmutate("");
        }

        if (mon->alive() && resist > 0) {
            simple_monster_message(*mon, mon->resist_margin_phrase(resist).c_str());
        }
    };
    

    pbolt.obvious_effect = true;
    pbolt.pierce = is_blueprint_exist(BLUEPRINT_PENTAN) >= 1;
    pbolt.is_explosion = is_blueprint_exist(BLUEPRINT_BOME) >= 1;
    if (is_blueprint_exist(BLUEPRINT_BOME)) {
        pbolt.ex_size = is_blueprint_exist(BLUEPRINT_BOME);
    }
    int range = spell_range(SPELL_PAKELLAS_ROD, powc);
    pbolt.range = range;
    //pbolt.ench_power = zap_ench_power(z_type, power, is_monster);
    
    //pbolt.hit = AUTOMATIC_HIT;
    pbolt.hit = is_blueprint_exist(BLUEPRINT_PERFECT_SHOT) >= 1 ?
        AUTOMATIC_HIT : 
        (10 + powc * 1 / 25);
    pbolt.hit = max(0, pbolt.hit - 5 * you.inaccuracy());

    pbolt.damage = calc_dice(6, (6 + powc * 3 / 4) * multiple);

    pbolt.origin_spell = SPELL_PAKELLAS_ROD;

    pbolt.loudness = 5 + is_blueprint_exist(BLUEPRINT_BOME);

    if (is_blueprint_exist(BLUEPRINT_SPREAD)) {
        //targeter_cone hitfunc(&you, range, PI / 6);
       // hitfunc.set_aim(pbolt.target);
        targeter_shotgun hitfunc(&you, 11, range);

        hitfunc.set_aim(pbolt.target);

        bolt sbeam = pbolt;
        sbeam.range = 1;
#ifdef USE_TILE
        sbeam.tile_beam = -1;
#endif
        sbeam.draw_delay = 0;

        if (stop_attack_prompt(hitfunc, "glaciate", _player_glaciate_affects))
        {
            return spret::abort;
        }

        for (const auto& entry : hitfunc.zapped)
        {
            if (entry.second <= 0)
                continue;

            sbeam.source = entry.first;
            sbeam.target = entry.first;
            sbeam.fire();

            sbeam.draw(entry.first);
        }
        scaled_delay(25);
    }
    else {
        pbolt.fire();
    }
    return spret::success;
}

void setup_miasma_breath(const actor *source, int pow, bolt &beam)
{
    beam.source_id= source->mid;
    beam.name     = "foul vapour";
    beam.damage   = dice_def(3, 5 + pow / 24);
    beam.colour   = DARKGREY;
    beam.flavour  = BEAM_MIASMA;
    beam.hit      = 17 + pow / 20;
    beam.pierce   = true;
    beam.origin_spell = SPELL_MIASMA_BREATH;
    beam.loudness = 0;
}

spret cast_miasma_breath(int pow, bolt &beam)
{
    if (grid_distance(beam.target, beam.source) > beam.range)
    {
        mpr("That is beyond the maximum range.");
        return spret::abort;
    }

    if (cell_is_solid(beam.target))
    {
        const char *feat = feat_type_name(grd(beam.target));
        mprf("You can't place the cloud on %s.", article_a(feat).c_str());
        return spret::abort;
    }

    setup_miasma_breath(&you, pow, beam);

    bolt tempbeam = beam;
    tempbeam.is_tracer = false;

    tempbeam.explode(false);
    if (tempbeam.beam_cancelled)
        return spret::abort;

    beam.apply_beam_conducts();
    beam.refine_for_explosion();
    beam.explode(false);
    
    viewwindow();
    return spret::success;
}


void actor_apply_toxic_bog(actor * act)
{
    if (grd(act->pos()) != DNGN_TOXIC_BOG)
        return;

    if (!act->ground_level())
        return;

    const bool player = act->is_player();
    monster *mons = !player ? act->as_monster() : nullptr;

    actor *oppressor = nullptr;

    for (map_marker *marker : env.markers.get_markers_at(act->pos()))
    {
        if (marker->get_type() == MAT_TERRAIN_CHANGE)
        {
            map_terrain_change_marker* tmarker =
                    dynamic_cast<map_terrain_change_marker*>(marker);
            if (tmarker->change_type == TERRAIN_CHANGE_BOG)
                oppressor = actor_by_mid(tmarker->mon_num);
        }
    }

    const int base_damage = dice_def(4, 6).roll();
    const int damage = resist_adjust_damage(act, BEAM_POISON_ARROW, base_damage);
    const int resist = base_damage - damage;

    const int final_damage = timescale_damage(act, damage);

    if (player && final_damage > 0)
    {
        mprf("You fester in the toxic bog%s",
                attack_strength_punctuation(final_damage).c_str());
    }
    else if (final_damage > 0)
    {
        behaviour_event(mons, ME_DISTURB, 0, act->pos());
        mprf("%s festers in the toxic bog%s",
                mons->name(DESC_THE).c_str(),
                attack_strength_punctuation(final_damage).c_str());
    }

    if (final_damage > 0 && resist > 0)
    {
        if (player)
            canned_msg(MSG_YOU_PARTIALLY_RESIST);

        act->poison(oppressor, 7, true);
    }
    else if (final_damage > 0)
        act->poison(oppressor, 21, true);

    if (final_damage)
    {

        const string oppr_name =
            oppressor ? " "+apostrophise(oppressor->name(DESC_THE))
                      : "";
        dprf("%s %s %d damage from%s toxic bog.",
             act->name(DESC_THE).c_str(),
             act->conj_verb("take").c_str(),
             final_damage,
             oppr_name.c_str());

        act->hurt(oppressor, final_damage, BEAM_MISSILE,
                  KILLED_BY_POISON, "", "toxic bog");
    }
}

/**
 * Cast Frozen Ramparts
 *
 * @param caster The caster.
 * @param pow    The spell power.
 * @param fail   Did this spell miscast? If true, abort the cast.
 * @return       spret::fail if one could be found but we miscast, and
 *               spret::success if the spell was successfully cast.
*/
spret cast_frozen_ramparts(int pow, bool fail)
{
    vector<coord_def> wall_locs;
    for (radius_iterator ri(you.pos(),
        spell_range(SPELL_FROZEN_RAMPARTS, -1, false), C_SQUARE,
        LOS_NO_TRANS, true); ri; ++ri)
    {
        const auto feat = grd(*ri);
        if (feat_is_wall(feat))
            wall_locs.push_back(*ri);
    }

    if (wall_locs.empty())
    {
        mpr("There are no walls around you to affect.");
        return spret::abort;
    }

    fail_check();

    for (auto pos : wall_locs)
    {
        if (in_bounds(pos))
            noisy(spell_effect_noise(SPELL_FROZEN_RAMPARTS), pos);
        env.pgrid(pos) |= FPROP_ICY;
    }

    env.level_state |= LSTATE_ICY_WALL;
    you.props[FROZEN_RAMPARTS_KEY] = you.pos();

    mpr("The walls around you are covered in ice.");
    you.duration[DUR_FROZEN_RAMPARTS] = random_range(40 + pow,
        80 + pow * 3 / 2);
    return spret::success;
}

dice_def ramparts_damage(int pow, bool random)
{
    int size = 2 + pow / 5;
    if (random)
        size = 2 + div_rand_round(pow, 5);
    return dice_def(1, size);
}

static bool _act_worth_targeting(const actor& caster, const actor& a)
{
    if (!caster.see_cell_no_trans(a.pos()))
        return false;
    if (a.is_player())
        return true;
    const monster& m = *a.as_monster();
    return !mons_is_firewood(m)
        && !mons_is_conjured(m.type)
        && (!caster.is_player() || !god_protects(&you, &m, true));
}


static bool _maxwells_target_check(monster& m)
{
    return _act_worth_targeting(you, m)
        && !m.wont_attack();
}

bool wait_spell_active(spell_type spell)
{
    // XX deduplicate code somehow
    return spell == SPELL_SEARING_RAY
        && you.attribute[ATTR_SEARING_RAY] != 0
        || spell == SPELL_MAXWELLS_COUPLING
        && you.props.exists(COUPLING_TIME_KEY);
}
// returns the closest target to the player, choosing randomly if there are more
// than one (see `fair` argument to distance_iterator).
static monster* _find_maxwells_target(bool tracer)
{
    for (distance_iterator di(you.pos(), !tracer, true, LOS_RADIUS); di; ++di)
    {
        monster* mon = monster_at(*di);
        if (mon && _maxwells_target_check(*mon)
            && (!tracer || you.can_see(*mon)))
        {
            return mon;
        }
    }

    return nullptr;
}

// find all possible targets at the closest distance; used for targeting
vector<monster*> find_maxwells_possibles()
{
    vector<monster*> result;
    monster* seed = _find_maxwells_target(true);
    if (seed)
    {
        const int distance = max(abs(you.pos().x - seed->pos().x),
            abs(you.pos().y - seed->pos().y));
        for (distance_iterator di(you.pos(), true, true, distance); di; ++di)
        {
            monster* mon = monster_at(*di);
            if (mon && _maxwells_target_check(*mon) && you.can_see(*mon))
                result.push_back(mon);
        }
    }
    return result;
}

spret cast_maxwells_coupling(int pow, bool fail, bool tracer)
{
    monster* const mon = _find_maxwells_target(tracer);

    if (tracer)
    {
        if (!mon || !you.can_see(*mon))
            return spret::abort;
        else
            return spret::success;
    }

    fail_check();

    mpr("You begin accumulating electric charge.");
    string msg = "(Press <w>%</w> to continue charging.)";
    insert_commands(msg, { CMD_WAIT });
    mpr(msg);

    you.props[COUPLING_TIME_KEY] =
        -(30 + div_rand_round(random2((200 - pow) * 40), 200));
    return spret::success;
}

static void _discharge_maxwells_coupling()
{
    monster* const mon = _find_maxwells_target(false);

    if (!mon)
    {
        mpr("Your charge dissipates without a target.");
        return;
    }

    targeter_radius hitfunc(&you, LOS_NO_TRANS);
    flash_view_delay(UA_PLAYER, LIGHTCYAN, 100, &hitfunc);

    god_conduct_trigger conducts[3];
    set_attack_conducts(conducts, *mon, you.can_see(*mon));


    if (mon->type == MONS_ROYAL_JELLY && !mon->is_summoned())
    {
        // need to do this here, because react_to_damage is never called
        mprf("A cloud of jellies burst out of %s as the current"
            " ripples through it!", mon->name(DESC_THE).c_str());
        trj_spawn_fineff::schedule(&you, mon, mon->pos(), mon->hit_points);
    }
    else
    {
        mprf("The electricity discharges through %s!", mon->name(DESC_THE).c_str());
    }



    const bool goldify = have_passive(passive_t::goldify_corpses);

    if (goldify)
        simple_monster_message(*mon, " vapourizes and condenses as gold!");
    else
        simple_monster_message(*mon, " vapourizes in an electric haze!");

    const coord_def pos = mon->pos();
    item_def* corpse = monster_die(*mon, KILL_YOU,
        actor_to_death_source(&you));
    if (corpse && !goldify)
        destroy_item(corpse->index());

    noisy(spell_effect_noise(SPELL_MAXWELLS_COUPLING), pos, you.mid);
}

void handle_maxwells_coupling()
{
    if (!you.props.exists(COUPLING_TIME_KEY))
        return;

    int charging_auts_remaining = you.props[COUPLING_TIME_KEY].get_int();

    if (charging_auts_remaining < 0)
    {
        mpr("You feel charge building up...");
        you.props[COUPLING_TIME_KEY] = -(charging_auts_remaining
            + you.time_taken);
        return;
    }

    if (crawl_state.prev_cmd != CMD_WAIT)
    {
        end_maxwells_coupling();
        return;
    }

    if (charging_auts_remaining <= you.time_taken)
    {
        you.time_taken = charging_auts_remaining;
        you.props.erase(COUPLING_TIME_KEY);
        _discharge_maxwells_coupling();
        return;
    }

    you.props[COUPLING_TIME_KEY] = charging_auts_remaining
        - you.time_taken;
    mpr("You feel charge building up...");
}

void end_maxwells_coupling()
{
    if (you.props.exists(COUPLING_TIME_KEY))
    {
        mpr("The insufficient charge disappates harmlessly.");
        you.props.erase(COUPLING_TIME_KEY);
    }
}

/**
 * Hailstorm the given cell. (Per the spell.)
 *
 * @param where     The cell in question.
 * @param pow       The power with which the spell is being cast.
 * @param agent     The agent (player or monster) doing the hailstorming.
 */
static void _hailstorm_cell(coord_def where, int pow, actor* agent)
{
    bolt beam;
    zappy(ZAP_HAILSTORM, pow, agent->is_monster(), beam);
    beam.thrower = agent->is_player() ? KILL_YOU : KILL_MON;
    beam.source_id = agent->mid;
    beam.attitude = agent->temp_attitude();
#ifdef USE_TILE
    beam.tile_beam = -1;
#endif
    beam.draw_delay = 10;
    beam.source = where;
    beam.target = where;
    beam.hit_verb = "pelts";

    monster* mons = monster_at(where);
    if (mons && mons->is_icy())
    {
        string msg;
        one_chance_in(20) ? msg = "%s dances in the hail." :
            msg = "%s is unaffected.";
        if (you.can_see(*mons))
            mprf(msg.c_str(), mons->name(DESC_THE).c_str());
        else
            mprf(msg.c_str(), "Something");

        beam.draw(where);
        return;
    }

    beam.fire();
}

spret cast_hailstorm(int pow, bool fail, bool tracer)
{
    targeter_radius hitfunc(&you, LOS_NO_TRANS, 3, 0, 2);
    bool (*vulnerable) (const actor*) = [](const actor* act) -> bool
    {
        // actor guaranteed to be monster from usage,
        // but we'll verify it as a matter of good hygiene.
        const monster* mon = act->as_monster();
        return mon && !mons_is_firewood(*mon)
            && !god_protects(mon)
            && !mons_is_projectile(*mon)
            && !(mons_is_avatar(mon->type) && mons_aligned(&you, mon))
            && !testbits(mon->flags, MF_DEMONIC_GUARDIAN);
    };

    if (tracer)
    {
        for (radius_iterator ri(you.pos(), 3, C_SQUARE, LOS_NO_TRANS, true); ri; ++ri)
        {
            if (grid_distance(you.pos(), *ri) == 1 || !in_bounds(*ri))
                continue;

            const monster* mon = monster_at(*ri);

            if (!mon || !you.can_see(*mon))
                continue;

            if (!mon->friendly() && (*vulnerable)(mon))
                return spret::success;
        }

        return spret::abort;
    }

    if (!you.is_auto_spell() && stop_attack_prompt(hitfunc, "hailstorm", vulnerable))
        return spret::abort;

    fail_check();

    mpr("A cannonade of hail descends around you!");

    for (radius_iterator ri(you.pos(), 3, C_SQUARE, LOS_NO_TRANS, true); ri; ++ri)
    {
        if (grid_distance(you.pos(), *ri) == 1 || !in_bounds(*ri))
            continue;

        _hailstorm_cell(*ri, pow, &you);
    }

    return spret::success;
}

spret cast_starburst(int pow, bool fail, bool tracer)
{
    int range = spell_range(SPELL_STARBURST, pow);

    vector<coord_def> offsets = { coord_def(range, 0),
                                coord_def(range, range),
                                coord_def(0, range),
                                coord_def(-range, range),
                                coord_def(-range, 0),
                                coord_def(-range, -range),
                                coord_def(0, -range),
                                coord_def(range, -range) };

    bolt beam;
    beam.range        = range;
    beam.source       = you.pos();
    beam.source_id    = MID_PLAYER;
    beam.is_tracer    = tracer;
    beam.is_targeting = tracer;
    beam.dont_stop_player = true;
    beam.friend_info.dont_stop = true;
    beam.foe_info.dont_stop = true;
    beam.attitude = ATT_FRIENDLY;
    beam.thrower      = KILL_YOU;
    beam.origin_spell = SPELL_STARBURST;
    beam.draw_delay   = 5;
    zappy(ZAP_BOLT_OF_FIRE, pow, false, beam);

    for (const coord_def & offset : offsets)
    {
        beam.target = you.pos() + offset;
        if (!tracer && !player_tracer(ZAP_BOLT_OF_FIRE, pow, beam))
            return spret::abort;

        if (tracer)
        {
            beam.fire();
            // something to hit
            if (beam.foe_info.count > 0)
                return spret::success;
        }
    }

    if (tracer)
        return spret::abort;

    fail_check();

    // Randomize for nice animations
    shuffle_array(offsets);
    for (auto & offset : offsets)
    {
        beam.target = you.pos() + offset;
        beam.fire();
    }

    return spret::success;
}

spret cast_flame_strike_shot(const actor* caster, const actor* defender, int damage, int hit, bool fail)
{
    const size_t range = 3;

    bolt beam;
    beam.range = range;
    beam.source = caster->pos();
    beam.source_id = MID_PLAYER;
    beam.target = defender->pos();
    beam.attitude = ATT_FRIENDLY;
    beam.thrower = KILL_YOU;
    beam.origin_spell = SPELL_FLAME_STRIKE;

    targeter_shotgun hitfunc(caster, 15, range);

    hitfunc.set_aim(defender->pos());

    fail_check();

    bolt pbolt = beam;
    pbolt.name = "flame strike";
    pbolt.thrower = KILL_YOU_MISSILE;
    pbolt.flavour = BEAM_FIRE;
    pbolt.real_flavour = BEAM_FIRE;
    pbolt.colour = RED;
    pbolt.glyph = dchar_glyph(DCHAR_EXPLOSION);
    pbolt.damage = calc_dice(1, damage);
    pbolt.hit = hit;

    pbolt.range = 1;
#ifdef USE_TILE
    pbolt.tile_beam = -1;
#endif
    pbolt.draw_delay = 0;

    hitfunc.set_aim(pbolt.target);
    noisy(explosion_noise(1), pbolt.target);

    for (const auto& entry : hitfunc.zapped)
    {
        if (entry.second <= 0)
            continue;

        pbolt.source = entry.first;
        pbolt.target = entry.first;
        pbolt.fire();

        pbolt.draw(entry.first);
    }
    scaled_delay(25);
    return spret::success;
}

vector<coord_def> find_bog_locations(const coord_def& center, int pow)
{
    vector<coord_def> bog_locs;
    const int radius = spell_range(SPELL_NOXIOUS_BOG, pow, false);

    for (radius_iterator ri(center, radius, C_SQUARE, LOS_NO_TRANS, true); ri;
        ri++)
    {
        if (!feat_has_solid_floor(env.grid(*ri)))
            continue;

        // If a candidate cell is next to a solid feature, we can't bog it.
        // Additionally, if it's next to a cell we can't currently see, we
        // can't bog it, regardless of what the cell contains. Don't want to
        // leak information about out-of-los cells.
        bool valid = true;
        for (adjacent_iterator ai(*ri); ai; ai++)
        {
            if (!you.see_cell(*ai) || feat_is_solid(env.grid(*ai)))
            {
                valid = false;
                break;
            }
        }
        if (valid)
            bog_locs.push_back(*ri);
    }

    return bog_locs;
}
spret cast_noxious_bog(int pow, bool fail)
{
    vector <coord_def> bog_locs = find_bog_locations(you.pos(), pow);
    if (bog_locs.empty())
    {
        mpr("There are no places for you to create a bog.");
        return spret::abort;
    }

    fail_check();

    const int turns = 5 + random2(pow / 10);
    you.increase_duration(DUR_NOXIOUS_BOG, turns);

    for (auto pos : bog_locs)
    {
        temp_change_terrain(pos, DNGN_TOXIC_BOG, turns * BASELINE_DELAY,
            TERRAIN_CHANGE_BOG, you.as_monster());
    }

    flash_view_delay(UA_PLAYER, LIGHTGREEN, 100);
    mpr("You spew toxic sludge!");

    return spret::success;
}