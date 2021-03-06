/**
 * @file
 * @brief Damage-dealing spells not already handled elsewhere.
 *           Other targeted spells are covered in spl-zap.cc.
**/

#include "AppHdr.h"

#include "spl-damage.h"

#include <cmath>

#include "act-iter.h"
#include "areas.h"
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
#include "godabil.h"
#include "godconduct.h"
#include "invent.h"
#include "itemname.h"
#include "items.h"
#include "losglobal.h"
#include "macro.h"
#include "mapmark.h"
#include "message.h"
#include "mgen_data.h"
#include "misc.h"
#include "mon-behv.h"
#include "mon-death.h"
#include "mon-place.h"
#include "mon-project.h"
#include "mon-tentacle.h"
#include "mutation.h"
#include "ouch.h"
#include "prompt.h"
#include "random.h"
#include "shout.h"
#include "spl-goditem.h" // monster_is_debuffable
#include "spl-summoning.h"
#include "spl-util.h"
#include "stepdown.h"
#include "stringutil.h"
#include "target.h"
#include "terrain.h"
#include "transform.h"
#include "unicode.h"
#include "viewchar.h"
#include "view.h"

#if TAG_MAJOR_VERSION == 34
// This spell has two main advantages over Fireball:
//
// (1) The release is instantaneous, so monsters will not
//     get an action before the player... this allows the
//     player to hit monsters with a double fireball (this
//     is why we only allow one delayed fireball at a time,
//     if you want to allow for more, then the release should
//     take at least some amount of time).
//
//     The casting of this spell still costs a turn. So
//     casting Delayed Fireball and immediately releasing
//     the fireball is only slightly different from casting
//     a regular Fireball (monsters act in the middle instead
//     of at the end). This is why we allow for the spell
//     level discount so that Fireball is free with this spell
//     (so that it only costs 7 levels instead of 12 to have
//     both).
//
// (2) When the fireball is released, it is guaranteed to
//     go off... the spell only fails at this point. This can
//     be a large advantage for characters who have difficulty
//     casting Fireball in their standard equipment. However,
//     the power level for the actual fireball is determined at
//     release, so if you do swap out your enhancers you'll
//     get a less powerful ball when it's released. - bwr
//
spret_type cast_delayed_fireball(bool fail)
{
    fail_check();
    // Okay, this message is weak but functional. - bwr
    mpr("You feel magically charged.");
    you.attribute[ATTR_DELAYED_FIREBALL] = 1;
    return SPRET_SUCCESS;
}
#endif

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

spret_type cast_fire_storm(int pow, bolt &beam, bool fail)
{
    if (grid_distance(beam.target, beam.source) > beam.range)
    {
        mpr("That is beyond the maximum range.");
        return SPRET_ABORT;
    }

    if (cell_is_solid(beam.target))
    {
        const char *feat = feat_type_name(grd(beam.target));
        mprf("You can't place the storm on %s.", article_a(feat).c_str());
        return SPRET_ABORT;
    }

    fail_check();
    setup_fire_storm(&you, pow, beam);

    bolt tempbeam = beam;
    tempbeam.ex_size = (pow > 76) ? 3 : 2;
    tempbeam.is_tracer = true;

    tempbeam.explode(false);
    if (tempbeam.beam_cancelled)
        return SPRET_ABORT;

    beam.refine_for_explosion();
    beam.explode(false);

    viewwindow();
    return SPRET_SUCCESS;
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

// XXX no friendly check
spret_type cast_chain_spell(spell_type spell_cast, int pow,
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
    
    int max_arcs = 1 + div_rand_round(pow, 20);
	
    source = target = caster->pos();
    do
    {
        source = target;
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

            count++;

            if (dist < min_dist)
            {
                min_dist = dist;
                target = mi->pos();
                count = 0;
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
                    ? calc_dice(5, 10 + div_rand_round(pow * 3,5))
                    : calc_dice(5, 46 + div_rand_round(pow,6));
                break;
            case SPELL_CHAIN_OF_CHAOS:
                beam.colour       = ETC_RANDOM;
                beam.ench_power   = pow;
                beam.damage       = calc_dice(3, 5 + div_rand_round(pow,2));
                beam.real_flavour = BEAM_CHAOS;
                beam.flavour      = BEAM_CHAOS;
            default:
                break;
        }

        // Be kinder to the caster.
        if (target == caster->pos())
        {
            if (!(beam.damage.num /= 2))
                beam.damage.num = 1;
            if ((beam.damage.size /= 2) < 3)
                beam.damage.size = 3;
        }
        beam.fire();
		
        max_arcs--;
    }
    while (max_arcs > 0);

    return SPRET_SUCCESS;
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

static bool _drain_lifeable_hitfunc(const actor* act)
{
    return _drain_lifeable(&you, act);
}

static int _drain_player(const actor* agent, int pow, int avg, bool actual)
{
    const int hurted = resist_adjust_damage(&you, BEAM_NEG, avg);
	
    if (actual)
    {
		mprf("That hurt (%d)!", hurted);
        const monster* mons = agent ? agent->as_monster() : 0;
        ouch(hurted, KILLED_BY_BEAM, mons ? mons->mid : MID_NOBODY,
             "by drain life");
    }

    return hurted;
}

static int _drain_monster(const actor* agent, monster* target, int pow,
                          int avg, bool actual)
{
    ASSERT(target); // XXX: change to monster &target
    int hurted = resist_adjust_damage(target, BEAM_NEG, avg);
    if (actual)
    {
        if (hurted)
        {
            if (agent && agent->is_player())
            {
                mprf("You draw life from %s (%d).",
                     target->name(DESC_THE).c_str(), hurted);
            }
            target->hurt(agent, hurted);
        }

        if (target->alive())
        {
            behaviour_event(target, ME_ANNOY, agent,
                            agent ? agent->pos() : coord_def(0, 0));
        }

        if (target->alive() && you.can_see(*target))
            print_wounds(*target);
    }

    if (!target->is_summoned())
        return hurted;

    return 0;
}

static spret_type _cast_los_attack_spell(spell_type spell, int pow, const
                                         actor* agent, bool actual, bool fail,
                                         int* damage_done)
{
    const monster* mons = agent ? agent->as_monster() : nullptr;

    colour_t flash_colour = BLACK;
    const char *player_msg = nullptr, *global_msg = nullptr,
               *mons_vis_msg = nullptr, *mons_invis_msg = nullptr;
    bool (*vulnerable)(const actor *, const actor *) = nullptr;
    bool (*vul_hitfunc)(const actor *) = nullptr;
    int (*damage_player)(const actor *, int, int, bool) = nullptr;
    int (*damage_monster)(const actor *, monster *, int, int, bool)
        = nullptr;
    void (*pre_hook)(const actor*, bool, vector<monster *>) = nullptr;
    int fake_damage = -1;
    if (!damage_done)
        damage_done = &fake_damage;

    int hurted = 0;
    int this_damage = 0;
    int total_damage = 0;
    *damage_done = total_damage;

    bolt beam;
    beam.source_id = agent ? agent->mid : MID_NOBODY;
    beam.foe_ratio = 80;

    switch (spell)
    {
        case SPELL_DRAIN_LIFE:
            player_msg = "You draw life from your surroundings.";
            global_msg = "Something draws the life force from your"
                         " surroundings.";
            mons_vis_msg = " draws from the surrounding life force!";
            mons_invis_msg = "The surrounding life force dissipates!";
            flash_colour = DARKGREY;
            vulnerable = &_drain_lifeable;
            vul_hitfunc = &_drain_lifeable_hitfunc;
            damage_player = &_drain_player;
            damage_monster = &_drain_monster;
            hurted = 3 + random2(7) + random2(pow);
            break;

        default: return SPRET_ABORT;
    }

    if (agent && agent->is_player())
    {
        ASSERT(actual);
        targetter_los hitfunc(&you, LOS_NO_TRANS);
        {
            if (stop_attack_prompt(hitfunc, "harm", vul_hitfunc))
                return SPRET_ABORT;
        }
        fail_check();

        mpr(player_msg);
        flash_view(UA_PLAYER, flash_colour, &hitfunc);
        more();
        clear_messages();
        flash_view(UA_PLAYER, 0);
    }
    else if (actual)
    {
        if (!agent)
            mpr(global_msg);
        else if (you.can_see(*agent))
            simple_monster_message(*mons, mons_vis_msg);
        else if (you.see_cell(agent->pos()))
            mpr(mons_invis_msg);

        flash_view_delay(UA_MONSTER, flash_colour, 300);
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

    // XXX: This ordering is kind of broken; it's to preserve the message
    // order from the original behaviour in the case of refrigerate.
    if (affects_you)
    {
        total_damage = (*damage_player)(agent, pow, hurted, actual);
        if (!actual && mons)
        {
            if (mons->wont_attack())
            {
                beam.friend_info.count++;
                beam.friend_info.power +=
                    (you.get_experience_level() * total_damage / hurted);
            }
            else
            {
                beam.foe_info.count++;
                beam.foe_info.power +=
                    (you.get_experience_level() * total_damage / hurted);
            }
        }
    }

    if (actual && pre_hook)
        (*pre_hook)(agent, affects_you, affected_monsters);

    for (auto m : affected_monsters)
    {
        if (!m->alive())
            continue;

        this_damage = (*damage_monster)(agent, m, pow, hurted, actual);
        total_damage += this_damage;

        if (!actual && mons)
        {
            if (mons_atts_aligned(m->attitude, mons->attitude))
            {
                beam.friend_info.count++;
                beam.friend_info.power += (m->get_hit_dice() * this_damage / hurted);
            }
            else
            {
                beam.foe_info.count++;
                beam.foe_info.power += (m->get_hit_dice() * this_damage / hurted);
            }
        }
    }

    *damage_done = total_damage;
    if (actual)
        return SPRET_SUCCESS;
    return mons_should_fire(beam) ? SPRET_SUCCESS : SPRET_ABORT;
}

spret_type trace_los_attack_spell(spell_type spell, int pow, const actor* agent)
{
    return _cast_los_attack_spell(spell, pow, agent, false, false, nullptr);
}

spret_type fire_los_attack_spell(spell_type spell, int pow, const actor* agent,
                                 bool fail, int* damage_done)
{
    return _cast_los_attack_spell(spell, pow, agent, true, fail, damage_done);
}

// Screaming Sword
void sonic_damage(bool scream)
{
    if (is_sanctuary(you.pos()))
        return;

    // First build the message.
    counted_monster_list affected_monsters;

    for (monster_near_iterator mi(you.pos(), LOS_SOLID); mi; ++mi)
        if (!silenced(mi->pos()))
            affected_monsters.add(*mi);

    /* dpeg sez:
       * damage applied to everyone but the wielder (reasoning: the sword
         does not like competitors, so no allies)
       -- so sorry, Beoghites, Jiyvaites and the rest.
    */

    if (!affected_monsters.empty())
    {
        const string message =
            make_stringf("%s %s hurt by the noise.",
                         affected_monsters.describe().c_str(),
                         conjugate_verb("be", affected_monsters.count() > 1).c_str());
        if (strwidth(message) < get_number_of_cols() - 2)
            mpr(message);
        else
        {
            // Exclamation mark to suggest that a lot of creatures were
            // affected.
            mpr("The monsters around you reel from the noise!");
        }
    }

    // Now damage the creatures.
    for (monster_near_iterator mi(you.pos(), LOS_SOLID); mi; ++mi)
    {
        if (silenced(mi->pos()))
            continue;
        int hurt = (random2(2) + 1) * (random2(2) + 1) * (random2(3) + 1)
                 + (random2(3) + 1) + 1;
        if (scream)
            hurt = max(hurt * 2, 16);
        int cap = scream ? mi->max_hit_points / 2 : mi->max_hit_points * 3 / 10;
        hurt = min(hurt, max(cap, 1));
        // not so much damage if you're a n00b
        hurt = div_rand_round(hurt * you.experience_level, 27);
        /* per dpeg:
         * damage is universal (well, only to those who can hear, but not sure
           we can determine that in-game), i.e. smiting, no resists
        */
        dprf("damage done: %d", hurt);
        mi->hurt(&you, hurt);

        if (is_sanctuary(mi->pos()))
            remove_sanctuary(true);
    }
}

spret_type vampiric_drain(int pow, bool fail, bool tracer)
{
    if (!tracer && you.hp == you.hp_max)
    {
        mpr("Your health is already full!");
        return SPRET_ABORT;
    }
    
    targetter_radius hitfunc(&you, LOS_NO_TRANS, 1);
    bool (*vulnerable) (const actor *) = [](const actor * act) -> bool
    {
        return act->is_monster()
               && !act->wont_attack()
               && !act->is_summoned()
               && act->holiness() & MH_NATURAL;
    };
    
    if (!tracer && stop_attack_prompt(hitfunc, "drain", vulnerable))
        return SPRET_ABORT;
    
    monster *target = nullptr;
    
    for (distance_iterator di(you.pos(), true, true, 1); di; ++di)
    {
        monster *mon = monster_at(*di);
        if (mon
            && you.can_see(*mon)
            && you.see_cell_no_trans(mon->pos())
            && !mon->wont_attack()
            && !mons_is_firewood(*mon)
            &&  mon->holiness() & MH_NATURAL
            && !mon->is_summoned())
        { 
            target = mon;
        }
    }

    if (!target && tracer)
    {
        // nothing to drain
        return SPRET_ABORT;
    }
    else
    {
        if (tracer)
            return SPRET_SUCCESS;
        
        fail_check();
        
        if (!target)
        {
            canned_msg(MSG_NOTHING_HAPPENS);
            return SPRET_SUCCESS;
        }
        else
        {
            god_conduct_trigger conducts[3];
            disable_attack_conducts(conducts);
            set_attack_conducts(conducts, target);
            behaviour_event(target, ME_WHACK, &you, you.pos());
            enable_attack_conducts(conducts);
            
            int hp_gain = 2 + random2(7) + random2(1 + div_rand_round(pow, 5));

            hp_gain = min(target->hit_points, hp_gain);
            hp_gain = min(you.hp_max - you.hp, hp_gain);
            
            if (!hp_gain)
            {
                canned_msg(MSG_NOTHING_HAPPENS);
                return SPRET_SUCCESS;
            }
            
            mprf("You drain life from %s (%d)!", target->name(DESC_THE).c_str(), hp_gain);

            target->hurt(&you, hp_gain);
            
            if (target->alive())
                print_wounds(*target);
            
            hp_gain = div_rand_round(hp_gain, 2);
            
            if (hp_gain && !you.duration[DUR_DEATHS_DOOR])
            {
                mpr("You feel life coursing into your body.");
                inc_hp(hp_gain);
            }
        }
    }
    return SPRET_SUCCESS;
}

spret_type cast_freeze(int pow, bool fail, bool tracer, bool battlesphere)
{
    monster* avatar = find_battlesphere(&you);
    coord_def source = avatar && battlesphere ? avatar->pos() : you.pos();
    
    coord_def target = random_target_in_range(1, source);
    
    if(!in_bounds(target) && tracer)
    {
        //nothing to hit
        return SPRET_ABORT;
    }
    else
    {     
        if(tracer)
            return SPRET_SUCCESS;
        
        targetter_radius hitfunc(&you, LOS_NO_TRANS, 1);
        bool (*vulnerable) (const actor *) = [](const actor * act) -> bool
        {
            return act->is_monster()
                && !act->wont_attack();
        };
        if (!battlesphere && stop_attack_prompt(hitfunc, "freeze", vulnerable))
            return SPRET_ABORT;
        
        fail_check();
        
        if(!in_bounds(target))
        {
            if (!battlesphere)
                canned_msg(MSG_NOTHING_HAPPENS);
            
            return SPRET_SUCCESS;
        }
        
        monster* mons = monster_at(target);
        
        //this should not ever happen, but just in case...
        if (mons == nullptr || mons->submerged())
        {
            canned_msg(MSG_NOTHING_CLOSE_ENOUGH);
            return SPRET_SUCCESS;
        }

        god_conduct_trigger conducts[3];
        disable_attack_conducts(conducts);

        bolt beam;
        beam.flavour = BEAM_COLD;
        beam.thrower = KILL_YOU;
	
        const int orig_hurted = roll_dice(1, 3 + div_rand_round(pow, 3));
        int hurted = mons_adjust_flavoured(mons, beam, orig_hurted);
	
        if (!fail)
        {
            set_attack_conducts(conducts, mons);

            mprf("You%s freeze%s %s (%d).", battlesphere ? "r battlesphere" : "",
                    battlesphere ? "s" : "",
                    mons->name(DESC_THE).c_str(), hurted);

            behaviour_event(mons, ME_ANNOY, &you);
        }

        enable_attack_conducts(conducts);

        mons->hurt(&you, hurted);

        if (mons->alive())
        {
            mons->expose_to_element(BEAM_COLD, orig_hurted);
            print_wounds(*mons);
        }
    }
    
    return SPRET_SUCCESS;
}

/**
 * Hailstorm the given cell. (Per the spell.)
 *
 * @param where     The cell in question.
 * @param pow       The power with which the spell is being cast.
 * @param agent     The agent (player or monster) doing the hailstorming.
 */
static void _hailstorm_cell(coord_def where, int pow, actor *agent)
{
    bolt beam;
    beam.flavour    = BEAM_ICE;
    beam.thrower    = agent->is_player() ? KILL_YOU : KILL_MON;
    beam.source_id  = agent->mid;
    beam.attitude   = agent->temp_attitude();
    beam.glyph      = dchar_glyph(DCHAR_FIRED_BURST);
    beam.colour     = ETC_ICE;
#ifdef USE_TILE
    beam.tile_beam  = -1;
#endif
    beam.draw_delay = 10;
    beam.source     = where;
    beam.target     = where;
    beam.damage     = calc_dice(1, 10 + div_rand_round(pow, 2));
    beam.hit        = 18 + div_rand_round(pow, 6);
    beam.name       = "hail";
    beam.hit_verb   = "pelts";

    monster *mons = monster_at(where);
    if (mons && mons->is_icy())
    {
        string msg = "%s is unaffected.";
        if (you.can_see(*mons))
            mprf(msg.c_str(), mons->name(DESC_THE).c_str());
        else
            mprf(msg.c_str(), "Something");

        beam.draw(where);
        return;
    }

    beam.fire();
}

spret_type cast_hailstorm(int pow, bool fail, bool tracer, bool battlesphere)
{
    targetter_radius hitfunc(&you, LOS_NO_TRANS, 3, 0, 2);
    bool (*vulnerable) (const actor *) = [](const actor * act) -> bool
    {
        return !act->is_icy();
    };

    if (tracer && !battlesphere)
    {
        for (radius_iterator ri(you.pos(), 3, C_SQUARE, LOS_NO_TRANS, true); ri; ++ri)
        {
            if (grid_distance(you.pos(), *ri) == 1 || !in_bounds(*ri))
                continue;

            const monster* mon = monster_at(*ri);

            if (!mon || !you.can_see(*mon))
                continue;

            if (!mon->friendly() && (*vulnerable)(mon))
                return SPRET_SUCCESS;
        }

        return SPRET_ABORT;
    }

    if (!battlesphere && stop_attack_prompt(hitfunc, "hailstorm", vulnerable))
        return SPRET_ABORT;

    fail_check();
    
    monster* avatar = find_battlesphere(&you);
    if (battlesphere && !avatar)
        return SPRET_SUCCESS;

    mprf("A cannonade of hail descends around you%s!", battlesphere ? "r battlesphere" : "");
    coord_def pos = battlesphere ? avatar->pos() : you.pos();
    for (radius_iterator ri(pos, 3, C_SQUARE, LOS_NO_TRANS, true); ri; ++ri)
    {
        if (grid_distance(pos, *ri) == 1 || !in_bounds(*ri))
            continue;

        _hailstorm_cell(*ri, pow, &you);
    }

    return SPRET_SUCCESS;
}

//Handle Winter's Embrace at a given cell
static void _embrace_cell(coord_def where, int pow, actor *agent)
{
    bolt beam;
    beam.flavour    = BEAM_ICE;
    beam.thrower    = agent->is_player() ? KILL_YOU : KILL_MON;
    beam.source_id  = agent->mid;
    beam.attitude   = agent->temp_attitude();
    beam.glyph      = dchar_glyph(DCHAR_FIRED_BURST);
    beam.colour     = BLUE;
#ifdef USE_TILE
    beam.tile_beam  = -1;
#endif
    beam.draw_delay = 10;
    beam.source     = where;
    beam.target     = where;
    beam.damage     = calc_dice(1, 20 + div_rand_round(pow * 2, 3));
    beam.hit        = AUTOMATIC_HIT;
    beam.loudness   = 0;
    beam.name       = "cold";
    beam.hit_verb   = "envelops";

    actor *act = actor_at(where);
    if (act && act->is_monster() && act->as_monster()->is_icy())
    {
        string msg = "%s is unaffected.";
        mprf(msg.c_str(), act->name(DESC_THE).c_str());

        beam.draw(where);
        return;
    }

    beam.fire();
}

spret_type cast_winters_embrace(int pow, bool fail, bool tracer, bool battlesphere)
{
    monster* avatar = find_battlesphere(&you);
    
    if (!avatar && battlesphere)
        return SPRET_SUCCESS;
    
    targetter_radius hitfunc(&you, LOS_NO_TRANS, min(4,LOS_RADIUS));
    bool (*vulnerable) (const actor *) = [](const actor * act) -> bool
    {
        return !act->is_icy();
    };
    
    if (tracer)
    {
        for (radius_iterator ri(you.pos(), 4, C_SQUARE, LOS_NO_TRANS, true); ri; ++ri)
        {
            const monster* mon = monster_at(*ri);
            if(mon && you.can_see(*mon) && !mon->friendly() && (*vulnerable)(mon))
                return SPRET_SUCCESS;
        }
        return SPRET_ABORT;
    }
    
    if(!battlesphere)
    {
        if (stop_attack_prompt(hitfunc, "winter's embrace", vulnerable))
            return SPRET_ABORT;
    }
    
    fail_check();
    
    coord_def source = battlesphere ? avatar->pos() : you.pos();
    
    mprf("You%s drain%s the heat from %s surroundings.",
            battlesphere ? "r battlesphere" : "",
            battlesphere ? "s" : "",
            battlesphere ? "its" : "your");
    noisy(spell_effect_noise(SPELL_WINTERS_EMBRACE), source);
    
    for (radius_iterator ri(source, 4, C_SQUARE, LOS_NO_TRANS, true); ri; ++ri)
    {
        _embrace_cell(*ri, pow, &you);
        if (grid_distance(*ri, source) == 1)
        {
            actor* act = actor_at(*ri);
            if(act && act->check_res_magic(div_rand_round(pow * 2, 3)) < 0)
            {
                if(act->is_player())
                {
                    mpr("You fall asleep.");
                    you.put_to_sleep(&you, pow, true);
                }
                else
                {
                    mprf("%s falls asleep.", act->as_monster()->name(DESC_THE).c_str());
                    act->as_monster()->put_to_sleep(&you, pow, true);
                }
            }
        }
    }
   
   return SPRET_SUCCESS;
}

static coord_def _pyroclasm_target()
{
    
    coord_def target = you.pos();
    int num_hit = 0;
    int denom = 2;
    
    for (actor_near_iterator ai(you.pos(), LOS_NO_TRANS);
         ai; ++ai)
    {
        if (ai->is_monster()
            && !ai->as_monster()->wont_attack()
            && !mons_is_firewood(*ai->as_monster())
            && !mons_is_tentacle_segment(ai->as_monster()->type)
            && grid_distance(you.pos(), ai->pos()) < 6
            && grid_distance(you.pos(), ai->pos()) > 1)
        {
            int hit = 1;
            int rad = min(3, grid_distance(you.pos(), ai->pos()) - 1);
            
            for(radius_iterator ri(ai->pos(),rad, C_SQUARE, LOS_NO_TRANS, true); ri; ++ri)
            {
                const monster* mon = monster_at(*ri);
                if (mon && !mon->wont_attack() && !mons_is_firewood(*mon)
                    && !mons_is_tentacle_or_tentacle_segment(mon->type))
                {
                    hit++;
                }
            }
            
            if (hit > num_hit)
            {
                num_hit = hit;
                target = ai->pos();
                denom = 2;
            }
            else if (hit == num_hit)
            {
                if(one_chance_in(denom))
                    target = ai->pos();
                denom++;
            }
        }
    }
    
    return target;
}

spret_type cast_pyroclasm(int pow, bool fail, bool tracer)
{
    coord_def target = _pyroclasm_target();
    
    if (tracer)
    {
        if (target == you.pos())
            return SPRET_ABORT;
        else
            return SPRET_SUCCESS;
    }
    
    const actor* act = actor_at(target);
    
    int rad = min(3, grid_distance(you.pos(), target) - 1);
    
    targetter_radius hitfunc(act, LOS_NO_TRANS, rad);
    
    if (stop_attack_prompt(hitfunc, "pyroclasm", nullptr))
        return SPRET_ABORT;
    
    fail_check();
    
    if(target == you.pos())
        canned_msg(MSG_NOTHING_HAPPENS);
    else
    {
        bolt beam;
        beam.set_agent(&you);
        beam.flavour            = BEAM_LAVA;
        beam.real_flavour       = BEAM_LAVA;
        beam.origin_spell       = SPELL_PYROCLASM;
        beam.name               = "pyroclasm";
        beam.target             = target;
        beam.damage             = calc_dice(8, 5 + pow);
        beam.ench_power         = pow;
        beam.colour             = RED;
        beam.ex_size            = rad;
        beam.is_explosion       = true;
        beam.explode(false);
    }
    
    return SPRET_SUCCESS;
}

spret_type cast_airstrike(int pow, const dist &beam, bool fail)
{
    if (cell_is_solid(beam.target))
    {
        canned_msg(MSG_UNTHINKING_ACT);
        return SPRET_ABORT;
    }

    monster* mons = monster_at(beam.target);
    if (!mons || mons->submerged())
    {
        fail_check();
        canned_msg(MSG_SPELL_FIZZLES);
        return SPRET_SUCCESS; // still losing a turn
    }

    god_conduct_trigger conducts[3];
    disable_attack_conducts(conducts);

    if (stop_attack_prompt(mons, false, you.pos()))
        return SPRET_ABORT;

    fail_check();
    set_attack_conducts(conducts, mons);
	
	int hurted = 7 + random2(2+div_rand_round(pow,6));

    bolt pbeam;
    pbeam.flavour = BEAM_AIR;

#ifdef DEBUG_DIAGNOSTICS
    const int preac = hurted;
#endif
    hurted = mons->apply_ac(mons->beam_resists(pbeam, hurted, false));
    dprf("preac: %d, postac: %d", preac, hurted);	

    mprf("The air twists around and %sstrikes %s (%d)!",
         mons->airborne() ? "violently " : "",
         mons->name(DESC_THE).c_str(), hurted);
    noisy(spell_effect_noise(SPELL_AIRSTRIKE), beam.target);

    behaviour_event(mons, ME_ANNOY, &you);

    enable_attack_conducts(conducts);

    mons->hurt(&you, hurted);
    if (mons->alive())
        print_wounds(*mons);

    return SPRET_SUCCESS;
}

// Just to avoid typing this over and over.
// Returns true if monster died. -- bwr
static bool _player_hurt_monster(monster& m, int damage,
                                 beam_type flavour = BEAM_MISSILE)
{
    if (damage > 0)
    {
        m.hurt(&you, damage, flavour, KILLED_BY_BEAM, "", "", false);

        if (m.alive())
        {
            print_wounds(m);
            behaviour_event(&m, ME_WHACK, &you);
        }
        else
        {
            monster_die(&m, KILL_YOU, NON_MONSTER);
            return true;
        }
    }

    return false;
}

// Here begin the actual spells:
static int _shatter_mon_dice(const monster *mon)
{
    if (!mon)
        return 0;

    // Removed a lot of silly monsters down here... people, just because
    // it says ice, rock, or iron in the name doesn't mean it's actually
    // made out of the substance. - bwr
    switch (mon->type)
    {
    // Double damage to stone, metal and crystal.
    case MONS_EARTH_ELEMENTAL:
    case MONS_USHABTI:
    case MONS_STATUE:
    case MONS_GARGOYLE:
    case MONS_IRON_ELEMENTAL:
    case MONS_PEACEKEEPER:
    case MONS_WAR_GARGOYLE:
    case MONS_SALTLING:
    case MONS_CRYSTAL_GUARDIAN:
    case MONS_OBSIDIAN_STATUE:
    case MONS_ORANGE_STATUE:
    case MONS_ROXANNE:
        return 6;

    default:
        const bool petrifying = mon->petrifying();
        const bool petrified = mon->petrified();

		// Extra damage to petrifying/petrified things.
        if (petrifying || petrified)
            return 6;
        // reduced damage to insubstantials.
        else if (mon->is_insubstantial())
            return 1;
        // 1/3 damage to fliers and slimes.
        else if (mon->airborne() || mons_is_slime(*mon))
            return 1;
        // Double damage to bone.
        else if (mon->is_skeletal())
            return 6;
        // Normal damage to everything else.
        else
            return 3;
    }
}

static int _shatter_monsters(coord_def where, int pow, actor *agent)
{
    dice_def dam_dice(0, 5 + pow / 3); // Number of dice set below.
    monster* mon = monster_at(where);

    if (mon == nullptr || mon == agent)
        return 0;

    dam_dice.num = _shatter_mon_dice(mon);
    int damage = max(0, dam_dice.roll() - random2(mon->armour_class()));

    if (damage <= 0)
        return 0;

    if (agent->is_player())
    {
        mprf("%s shudders (%d).", mon->name(DESC_THE).c_str(), damage);
        _player_hurt_monster(*mon, damage);

        if (is_sanctuary(you.pos()) || is_sanctuary(mon->pos()))
            remove_sanctuary(true);
    }
    else
	{
        mprf("%s shudders (%d).", mon->name(DESC_THE).c_str(), damage);
        mon->hurt(agent, damage);
	}

    return damage;
}

static int _shatter_walls(coord_def where, int pow, actor *agent)
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
    case DNGN_RUNED_DOOR:
    case DNGN_OPEN_DOOR:
    case DNGN_SEALED_DOOR:
        if (you.see_cell(where))
            mpr("A door shatters!");
        chance = 100;
        break;

    case DNGN_GRATE:
        if (you.see_cell(where))
            mpr("An iron grate is ripped into pieces!");
        chance = 100;
        break;

    case DNGN_METAL_WALL:
        chance = pow / 10;
        break;

    case DNGN_ORCISH_IDOL:
    case DNGN_GRANITE_STATUE:
        chance = 50;
        break;

    case DNGN_CLEAR_STONE_WALL:
    case DNGN_STONE_WALL:
        chance = pow / 6;
        break;

    case DNGN_CLEAR_ROCK_WALL:
    case DNGN_ROCK_WALL:
    case DNGN_SLIMY_WALL:
        chance = pow / 4;
        break;

    case DNGN_CRYSTAL_WALL:
        chance = 50;
        break;

    case DNGN_TREE:
        chance = 33;
        break;

    default:
        break;
    }

    if (x_chance_in_y(chance, 100))
    {
        noisy(spell_effect_noise(SPELL_SHATTER), where);

        destroy_wall(where);

        if (agent->is_player() && feat_is_tree(grid))
            did_god_conduct(DID_KILL_PLANT, 1);

        return 1;
    }

    return 0;
}

static int _shatter_player_dice()
{
    if (you.is_insubstantial())
        return 1;
    else if (you.petrified() || you.petrifying())
        return 6; // reduced later
    else if (you.airborne())
        return 1;
    else if (you.form == TRAN_STATUE || you.species == SP_GARGOYLE)
        return 6;
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

spret_type cast_shatter(int pow, bool fail)
{
    {
        targetter_los hitfunc(&you, LOS_ARENA);
        if (stop_attack_prompt(hitfunc, "harm", _shatterable))
            return SPRET_ABORT;
    }

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

    return SPRET_SUCCESS;
}

static int _shatter_player(int pow, actor *wielder, bool devastator = false)
{
    if (wielder->is_player())
        return 0;

    dice_def dam_dice(_shatter_player_dice(), 5 + pow / 3);

    int damage = max(0, dam_dice.roll() - random2(you.armour_class()));
	
	
    if (damage > 0)
    {
		std::string d = std::to_string(damage);
		if(damage > 15)
			mprf("You shudder from the earth-shattering force (%s).",
						d.c_str());
        else
			mprf("You shudder (%s).", d.c_str());
		
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
        mpr("There is a shattering impact!");
    }

    // need to do this again to do the actual damage
    for (adjacent_iterator ai(where, false); ai; ++ai)
        _shatter_monsters(*ai, pow * 3 / 2, wielder);

    if ((you.pos() - wielder->pos()).rdist() <= 1 && in_bounds(you.pos()))
        _shatter_player(pow, wielder, true);
}

void detonation_brand(actor *wielder, coord_def where, int pow)
{

    // Used to draw explosion cells
    bolt beam_visual;
    beam_visual.set_agent(wielder);
    beam_visual.flavour       = BEAM_VISUAL;
    beam_visual.glyph         = dchar_glyph(DCHAR_EXPLOSION);
    beam_visual.colour        = RED;
    beam_visual.ex_size       = 1;
    beam_visual.is_explosion  = true;

    vector <monster* > affected_monsters;
    for (adjacent_iterator ai(where, false); ai; ++ai)
    {
        monster *mon = monster_at(*ai);
        if (!mon || !mon->alive() || mon->submerged()
            || mon == wielder)
        {
            continue;
        }
        if(mon && wielder->is_monster() && wielder->as_monster()->attitude == mon->attitude)
        {
			continue;
        }
        else if(mon && wielder->is_player() && mon->attitude != ATT_HOSTILE)
        {
			continue;
        }	
        affected_monsters.push_back(mon);
    }
    
    if (you.can_see(*wielder))
    {
        mpr("There is a fiery explosion!");
    }
	
    beam_visual.explosion_draw_cell(where);

    // do the actual damage
    for (auto mon : affected_monsters)
    {
        if(!mon || mon == nullptr || mon->type >= NUM_MONSTERS)
            continue;
        int dam = resist_adjust_damage(mon, BEAM_FIRE, 1 + random2(pow) / 2);
        if(you.can_see(*mon))
        {
            if(dam > 0)
                mprf("%s is burned (%d)!", mon->name(DESC_THE).c_str(), dam);
			
            beam_visual.explosion_draw_cell(mon->pos());
        }
        mon->expose_to_element(BEAM_FIRE, 1 + dam / 5);
        mon->hurt(wielder, dam);
    }
	
    if ((you.pos() - where).rdist() <= 1 && in_bounds(you.pos()) && wielder->is_monster()
            && wielder->as_monster()->attitude == ATT_HOSTILE)
    {
        int dam = resist_adjust_damage(&you, BEAM_FIRE, 1 + random2(pow) / 2);
		beam_visual.explosion_draw_cell(you.pos());
        mprf("you are burned (%d)!", dam);
        expose_player_to_element(BEAM_FIRE, 1 + dam / 5);
        ouch(dam, KILLED_BY_MONSTER, wielder->mid);
    }
	
    update_screen();
    scaled_delay(50);
}

/**
 * Is it OK for the player to cast Irradiate right now, or will they end up
 * injuring a monster they didn't mean to?
 *
 * @return  true if it's ok to go ahead with the spell; false if the player
 *          wants to abort.
 */
static bool _irradiate_is_safe()
{
    for (adjacent_iterator ai(you.pos()); ai; ++ai)
    {
        const monster *mon = monster_at(*ai);
        if (!mon)
            continue;

        if (stop_attack_prompt(mon, false, you.pos()))
            return false;
    }

    return true;
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
    if (!mons)
        return 0; // XXX: handle damaging the player for mons casts...?

    if (you.can_see(*mons))
    {
        mprf("%s is blasted with magical radiation!",
             mons->name(DESC_THE).c_str());
    }

    const int dice = 6;
    const int max_dam = 30 + div_rand_round(pow, 2);
    const dice_def dam_dice = calc_dice(dice, max_dam);
    const int dam = dam_dice.roll();
    dprf("irr for %d (%d pow, max %d)", dam, pow, max_dam);

    if (agent->is_player())
    {
        _player_hurt_monster(*mons, dam, BEAM_MMISSILE);

        // Why are you casting this spell at all while worshipping Zin?
        if (is_sanctuary(you.pos()) || is_sanctuary(mons->pos()))
            remove_sanctuary(true);
    }
    else
        mons->hurt(agent, dam, BEAM_MMISSILE);

    if (mons->alive())
        mons->malmutate("");

    return 1;
}

/**
 * Attempt to cast the spell "Irradiate", damaging & deforming enemies around
 * the player.
 *
 * @param pow   The power at which the spell is being cast.
 * @param who   The actor doing the irradiating.
 * @param fail  Whether the player has failed to cast the spell.
 * @return      SPRET_ABORT if the player changed their mind about casting after
 *              realizing they would hit an ally; SPRET_FAIL if they failed the
 *              cast chance; SPRET_SUCCESS otherwise.
 */
spret_type cast_irradiate(int powc, actor* who, bool fail)
{
    if (!_irradiate_is_safe())
        return SPRET_ABORT;

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
    {
        contaminate_player(1500 + random2(1500)); // on avg, a bit under 50% of
                                                  // yellow contam
                                                  // another cast might or
                                                  // might not push you over
    }
    return SPRET_SUCCESS;
}

// How much work can we consider we'll have done by igniting a cloud here?
// Considers a cloud under a susceptible ally bad, a cloud under a a susceptible
// enemy good, and other clouds relatively unimportant.
static int _ignite_tracer_cloud_value(coord_def where, actor *agent)
{
    actor* act = actor_at(where);
    if (act)
    {
        const int dam = resist_adjust_damage(act, BEAM_FIRE, 40);
        return mons_aligned(act, agent) ? -dam : dam;
    }
    // We've done something, but its value is indeterminate
    else
        return 1;
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
        // players just care if igniteable clouds exist
        if (agent && agent->is_player())
            return 1;
        return _ignite_tracer_cloud_value(where, agent);
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
    if (mon == nullptr || mon == agent)
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
            return mons_aligned(mon, agent) ? 0 : 1;
        return mons_aligned(mon, agent) ? -1 * damage : damage;
    }

    mprf("%s seems to burn from within (%d)!", mon->name(DESC_THE).c_str(), damage);

    dprf("Dice: %dd%d; Damage: %d", dam_dice.num, dam_dice.size, damage);

    mon->hurt(agent, damage);

    if (mon->alive())
    {
        behaviour_event(mon, ME_WHACK, agent);

        // Monster survived, remove any poison.
        mon->del_ench(ENCH_POISON, true); // suppress spam
        print_wounds(*mon);
    }
    else
    {
        monster_die(mon,
                    agent->is_player() ? KILL_YOU : KILL_MON,
                    agent->mindex());
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
        mprf("You feel like your blood is boiling (%d)!", damage);
    else if (resist < 0)
        mprf("The poison in your system burns terribly (%d)!", damage);
    else
        mprf("The poison in your system burns (%d)!", damage);

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
    // Fire cloud immunity.
    if (you.duration[DUR_FIRE_SHIELD] || you.has_mutation(MUT_IGNITE_BLOOD)
        || you.attribute[ATTR_FIRE_SHIELD])
        return false;

    string prompt = "You are standing ";

    // XXX XXX XXX major code duplication (ChrisOelmueller)

    if (const cloud_struct* cloud = cloud_at(you.pos()))
    {
        if (cloud->type == CLOUD_MEPHITIC || cloud->type == CLOUD_POISON)
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
 * @return              If it's a tracer, SPRET_SUCCESS if the spell should
 *                      be cast & SPRET_ABORT otherwise.
 *                      If it's a real spell, SPRET_ABORT if the player chose
 *                      to abort the spell, SPRET_FAIL if they failed the cast
 *                      chance, and SPRET_SUCCESS otherwise.
 */
spret_type cast_ignite_poison(actor* agent, int pow, bool fail, bool tracer)
{
    if (tracer)
    {
        // Estimate how much useful effect we'd get if we cast the spell now
        const int work = apply_area_visible([agent] (coord_def where) {
            return _ignite_poison_clouds(where, -1, agent)
                 + _ignite_poison_monsters(where, -1, agent)
                 + _ignite_poison_player(where, -1, agent);
        }, agent->pos());

        return work > 0 ? SPRET_SUCCESS : SPRET_ABORT;
    }

    if (agent->is_player())
    {
        if (maybe_abort_ignite())
        {
            canned_msg(MSG_OK);
            return SPRET_ABORT;
        }
        fail_check();
    }

    targetter_los hitfunc(agent, LOS_NO_TRANS);
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
        // Only relevant if a monster is casting this spell
        // (never hurts the caster)
        _ignite_poison_player(where, pow, agent);
        return 0; // ignored
    }, agent->pos());

    return SPRET_SUCCESS;
}

static void _ignition_square(const actor *agent, bolt beam, coord_def square, bool center)
{
    // HACK: bypass visual effect
    beam.target = square;
    beam.in_explosion_phase = true;
    beam.explosion_affect_cell(square);
    if (center)
        noisy(spell_effect_noise(SPELL_IGNITION),square);
}

coord_def random_target_in_range(int radius, coord_def center)
{
    if (!in_bounds(center))
        center = you.pos();
    
    vector<coord_def> targets;
    
    for (actor_near_iterator ai(center, LOS_NO_TRANS);
         ai; ++ai)
    {
        if (ai->is_monster()
            && !ai->as_monster()->wont_attack()
            && !mons_is_firewood(*ai->as_monster())
            && !mons_is_tentacle_segment(ai->as_monster()->type)
            && ai->pos().distance_from(center) <= radius)
        {
            targets.push_back(ai->position);
        }
    }
    
    if (targets.empty())
        return coord_def(0, 0);
    
    return targets[random2(targets.size())];
}

static monster* _closest_target_in_range(int radius, coord_def source = you.pos())
{
    
    for (distance_iterator di(source, true, true, radius); di; ++di)
    {
        monster *mon = monster_at(*di);
        if (mon
            && you.can_see(*mon)
            && you.see_cell_no_trans(mon->pos())
            && !mon->wont_attack()
            && !mons_is_firewood(*mon)
            && !mons_is_tentacle_segment(mon->type))
        { 
            return mon;
        }
    }
    
    return nullptr;
}

static vector<coord_def> _directional_bolt_targets(int radius, int max_targets = 3, bool battlesphere = false)
{
    vector<coord_def> targets;
    
    monster* avatar = find_battlesphere(&you);
    coord_def source = avatar && battlesphere ? avatar->pos() : you.pos();
    
    monster* first = _closest_target_in_range(radius, source);
    
    if(!first)
        return targets;
    
    targets.push_back(first->pos());
    max_targets--;
    
    coord_def current = first->pos();
    
    bool target_found = true;
    
    while(max_targets > 0 && target_found)
    {
        for (distance_iterator di(current, true, true, radius); di; ++di)
        {
            target_found = false;
            monster *mon = monster_at(*di);
            if(mon
                && !mons_is_firewood(*mon)
                && !mons_is_tentacle_or_tentacle_segment(mon->type)
                && cell_see_cell(you.pos(), mon->pos(), LOS_SOLID)
                && cell_see_cell(current, mon->pos(), LOS_SOLID_SEE)
                && mon->pos().distance_from(source) > current.distance_from(source)
                && mon->pos().distance_from(current) < mon->pos().distance_from(source))
            {
                targets.push_back(mon->pos());
                current = mon->pos();
                max_targets--;
                target_found = true;
                break;
            } 
        }
    }
    
    return targets;
}

spret_type cast_shock(int pow, bool fail, bool tracer, bool battlesphere)
{
    monster* avatar = find_battlesphere(&you);
    
    if (!avatar && battlesphere)
        return SPRET_SUCCESS;
    
    coord_def center = battlesphere ? avatar->pos() : you.pos();
    
    monster* closest = _closest_target_in_range(min(3, LOS_RADIUS), center);
    
    if (tracer)
    {
        if (!closest && !battlesphere)
            return SPRET_ABORT;
        else
            return SPRET_SUCCESS;
    }
    
    if(!battlesphere)
    {
        targetter_radius hitfunc(&you, LOS_NO_TRANS, LOS_RADIUS);
        if (stop_attack_prompt(hitfunc, "shock", nullptr))
            return SPRET_ABORT;
    }
    
    fail_check();

    if (!closest)
    {
        if(!battlesphere)
            canned_msg(MSG_NOTHING_HAPPENS);
    }
    else
    {
        coord_def close = closest->pos();
        
        bolt beam;
        beam.name = "shock";
        beam.thrower = KILL_YOU_MISSILE;
        beam.flavour = BEAM_ELECTRICITY;
        beam.pierce = true;
        beam.real_flavour = BEAM_ELECTRICITY;
        beam.colour = LIGHTCYAN;
        beam.is_explosion = false;
        beam.is_tracer = false;
        
        beam.hit = 8 + div_rand_round(pow, 7);
        beam.damage = calc_dice(1, 3 + div_rand_round(pow,4));
        beam.range = close.distance_from(you.pos());
        beam.source = you.pos();
        beam.target = close;
        beam.fire();
        
        coord_def random = random_target_in_range(LOS_RADIUS);
        
        //could have only had one monster onscreen and killed it with the first beam
        if(in_bounds(random))
        {
            //reset damage and to-hit as well as setting up the source and target
            beam.hit = 8 + div_rand_round(pow, 7);
            beam.damage = calc_dice(1, 3 + div_rand_round(pow,4));
            beam.range = random.distance_from(close);
            beam.source = close;
            beam.target = random;
            beam.fire();
        }
    }
    
    return SPRET_SUCCESS;
}

spret_type directional_lbolt(int pow, bool fail, bool tracer, bool battlesphere)
{
    monster* avatar = find_battlesphere(&you);
    
    if (!avatar && battlesphere)
        return SPRET_SUCCESS;
    
    vector<coord_def> targets = _directional_bolt_targets(3, 3 + div_rand_round(pow, 50), battlesphere);
    
    if (tracer)
    {
        if (targets.empty())
            return SPRET_ABORT;
        else
            return SPRET_SUCCESS;
    }
    
    if(!battlesphere)
    {
        targetter_radius hitfunc(&you, LOS_NO_TRANS, LOS_RADIUS);
        if(stop_attack_prompt(hitfunc, "electric surge", nullptr))
            return SPRET_ABORT;
    }
    
    fail_check();
    
    if (targets.empty())
    {
        if (!battlesphere)
            canned_msg(MSG_NOTHING_HAPPENS);
    }
    else
    {
        bolt beam;
        beam.name = "electric surge";
        beam.thrower = KILL_YOU_MISSILE;
        beam.flavour = BEAM_ELECTRICITY;
        beam.pierce = true;
        beam.real_flavour = BEAM_ELECTRICITY;
        beam.colour = LIGHTBLUE;
        beam.is_explosion = false;
        beam.is_tracer = false;
        
        mprf("You%s unleash%s a surge of electricity!", 
                battlesphere ? "r battlesphere" : "",
                battlesphere ? "es" : "");
        
        coord_def source = battlesphere ? avatar->pos() : you.pos();
        coord_def target;
        
        for (int i = 0; i < static_cast<int>(targets.size()); i++)
        {
            target = targets[i];
            beam.hit = 7 + div_rand_round(pow, 20);
            beam.damage = calc_dice(1, 11 + div_rand_round(pow * 3, 5));
            beam.range = target.distance_from(source);
            beam.source = source;
            beam.target = target;
            
            noisy(spell_effect_noise(SPELL_ELECTRIC_SURGE), beam.target);
            
            source = target;
            beam.fire();
        }      
    }
    
    return SPRET_SUCCESS;
}

spret_type cast_mephitic_cloud(int pow, bool fail, bool tracer)
{
    monster* target = _closest_target_in_range(min(4, LOS_RADIUS)); 
    
    if (tracer)
    {
        if (!target)
            return SPRET_ABORT;
        else
            return SPRET_SUCCESS;
    }
    
    targetter_radius hitfunc(&you, LOS_NO_TRANS, min(4,LOS_RADIUS));
    if (stop_attack_prompt(hitfunc, "mephitic cloud", nullptr))
        return SPRET_ABORT;
    
    fail_check();
    
    if(!target)
        canned_msg(MSG_NOTHING_HAPPENS);
    else
    {
        bolt beam_actual;
        beam_actual.set_agent(&you);
        beam_actual.flavour       = BEAM_MEPHITIC;
        beam_actual.real_flavour  = BEAM_MEPHITIC;
        beam_actual.damage        = calc_dice(1, 0);
        beam_actual.name          = "mephitic cloud";
        beam_actual.source        = target->pos();
        beam_actual.target        = target->pos();
        beam_actual.ench_power    = pow;
        beam_actual.colour        = GREEN;
        beam_actual.ex_size       = 1;
        beam_actual.is_explosion  = true;
        beam_actual.explode();
    }
    
    return SPRET_SUCCESS;
}

spret_type fcloud(int pow, bool fail, bool tracer)
{
    monster* closest = _closest_target_in_range(LOS_RADIUS);
    
    if (tracer)
    {
        if (!closest)
            return SPRET_ABORT;
        else
            return SPRET_SUCCESS;
    }
    
    fail_check();
    
    if(!closest)
        canned_msg(MSG_NOTHING_HAPPENS);
    else
    {
        coord_def target = closest->pos();
        int dist = target.distance_from(you.pos());
        
        for (radius_iterator ri(you.pos(), 3, C_SQUARE, LOS_NO_TRANS, true); ri; ++ri)
        {
            if ((grid_distance(target, *ri) > dist 
                    && grid_distance(you.pos(), *ri) <= grid_distance(target, *ri))
                    || !in_bounds(*ri))
                continue;
            
            if (!cell_is_solid(*ri) && !cloud_at(*ri))
            {
                place_cloud(CLOUD_COLD, *ri, 
                    1 + div_rand_round(pow, 20) + random2(div_rand_round(pow, 20)), &you);
            }
        }
    }
    
    return SPRET_SUCCESS;
    
}

spret_type violent_unravelling(int pow, bool fail, bool tracer, bool battlesphere)
{
    vector<coord_def> targets;
    
    monster* avatar = find_battlesphere(&you);
    coord_def center = avatar && battlesphere ? avatar->pos() : you.pos();
    
    for (actor_near_iterator ai(center, LOS_NO_TRANS);
         ai; ++ai)
    {
        if (ai->is_monster()
            && !ai->as_monster()->wont_attack()
            && monster_is_debuffable(*ai->as_monster())
            && !mons_is_firewood(*ai->as_monster())
            && !mons_is_tentacle_segment(ai->as_monster()->type))
        {
            targets.push_back(ai->position);
        }
    }
    
    coord_def target = coord_def(0,0);
    
    if (targets.empty())
    {
        if (tracer)
            return SPRET_ABORT;
    }
    else
    {
        if (tracer)
            return SPRET_SUCCESS;
        
        target = targets[random2(targets.size())];
        
        if (!battlesphere)
        {
            targetter_radius hitfunc(&you, LOS_NO_TRANS, LOS_RADIUS);
            if (stop_attack_prompt(hitfunc, "violent unravelling", nullptr))
                return SPRET_ABORT;
        }
    }
    
    fail_check();
    
    if (!in_bounds(target))
    {
        if (!battlesphere)
            canned_msg(MSG_NOTHING_HAPPENS);
    }
    else
    {
        bolt beam_actual;
        beam_actual.set_agent(&you);
        beam_actual.thrower       = KILL_YOU_MISSILE;
        beam_actual.flavour       = BEAM_UNRAVELLING;
        beam_actual.real_flavour  = BEAM_UNRAVELLING;
        beam_actual.hit           = AUTOMATIC_HIT;
        beam_actual.damage        = calc_dice(1, 1);
        beam_actual.ench_power    = pow;
        beam_actual.name          = "unravelling";
        beam_actual.source        = target;
        beam_actual.target        = target;
        beam_actual.colour        = ETC_MUTAGENIC;
        
        noisy(spell_effect_noise(SPELL_VIOLENT_UNRAVELLING), beam_actual.target);

        beam_actual.fire();
    }
    
    return SPRET_SUCCESS;
}

spret_type random_fireball(int pow, bool fail, bool tracer, bool battlesphere)
{

    monster* avatar = find_battlesphere(&you);
    if (battlesphere && !avatar)
        return SPRET_SUCCESS;

    coord_def source = battlesphere ? avatar->pos() : you.pos();
    monster* target = _closest_target_in_range(min(5,LOS_RADIUS), source);
    
    if (tracer && !battlesphere)
    {
        if (!target)
            return SPRET_ABORT;
        else
            return SPRET_SUCCESS;
    }
    
    if(!battlesphere)
    {
        targetter_radius hitfunc(&you, LOS_NO_TRANS, min(5,LOS_RADIUS));
        if (stop_attack_prompt(hitfunc, "detonate", nullptr))
            return SPRET_ABORT;
    }
    
    fail_check();

    if (!target)
    {
        if (!battlesphere)
            canned_msg(MSG_NOTHING_HAPPENS);
    }
    else
    {
        bolt beam_actual;
        beam_actual.set_agent(&you);
        beam_actual.flavour       = BEAM_FIRE;
        beam_actual.real_flavour  = BEAM_FIRE;
        beam_actual.damage        = calc_dice(1, 10 + div_rand_round(pow * 3, 5));
        beam_actual.name          = "fireball";
        beam_actual.target        = target->pos();
        beam_actual.colour        = RED;
        beam_actual.ex_size       = 1;
        beam_actual.is_explosion  = true;
        beam_actual.explode();
    }
    
    return SPRET_SUCCESS;
}

spret_type cast_absolute_zero(int pow, bool fail, bool tracer)
{
    monster* mon = _closest_target_in_range(3);
    
    if (tracer)
    {
        if (!mon)
            return SPRET_ABORT;
        else
            return SPRET_SUCCESS;
    }
    
    if (mon && you.can_see(*mon) && stop_attack_prompt(mon, false, mon->pos()))
        return SPRET_ABORT;
    
    fail_check();
    
    if (!mon)
        canned_msg(MSG_NOTHING_HAPPENS);
    else
    {
        targetter_radius hitfunc(&you, LOS_NO_TRANS);
        flash_view_delay(UA_PLAYER, LIGHTCYAN, 100, &hitfunc);

        god_conduct_trigger conducts[3];
        set_attack_conducts(conducts, mon, you.can_see(*mon));
        
        if (mon->type == MONS_ROYAL_JELLY && !mon->is_summoned())
        {
            // need to do this here, because react_to_damage is never called
            mprf("A cloud of jellies burst out of %s as it chills to"
                 " absolute zero!", mon->name(DESC_THE, false).c_str());
            trj_spawn_fineff::schedule(&you, mon, mon->pos(), mon->hit_points);
        }
        else
        {
            mprf("You chill %s to absolute zero!",
                 you.can_see(*mon) ? mon->name(DESC_THE).c_str() : "something");
        }

        const coord_def pos = mon->pos();
        glaciate_freeze(mon, KILL_YOU, actor_to_death_source(&you));
        // extremely loud at low power, zero noise at max power
        noisy(40 - div_rand_round(pow, 5), pos, you.mid);
    }
    
    return SPRET_SUCCESS;
}

spret_type cast_ignition(const actor *agent, int pow, bool fail)
{
    ASSERT(agent->is_player());

    fail_check();

    targetter_los hitfunc(agent, LOS_NO_TRANS);

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

    if (blast_sources.empty()) {
        canned_msg(MSG_NOTHING_HAPPENS);
    } else {
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
        beam_actual.set_agent(agent);
        beam_actual.flavour       = BEAM_FIRE;
        beam_actual.real_flavour  = BEAM_FIRE;
        beam_actual.glyph         = 0;
        beam_actual.damage        = calc_dice(3, 10 + pow/2); // same as fireball for now
        beam_actual.name          = "fireball";
        beam_actual.colour        = RED;
        beam_actual.ex_size       = 0;
        beam_actual.is_explosion  = true;
        beam_actual.loudness      = 0;

#ifdef DEBUG_DIAGNOSTICS
        dprf(DIAG_BEAM, "ignition dam=%dd%d",
             beam_actual.damage.num, beam_actual.damage.size);
#endif

        // Fake "shaped" radius 1 explosions (skipping squares with friends).
        for (coord_def pos : blast_sources) {
            for (adjacent_iterator ai(pos); ai; ++ai) {
                if (cell_is_solid(*ai))
                    continue;

                actor *act = actor_at(*ai);

                // Friendly creature, don't blast this square.
                if (act && (act == agent || (act->is_monster()
                    && act->as_monster()->wont_attack())))
                    continue;

                blast_adjacents.push_back(*ai);
                beam_visual.explosion_draw_cell(*ai);
            }
            beam_visual.explosion_draw_cell(pos);
        }
        update_screen();
        scaled_delay(50);

        // Real explosions on each individual square.
        for (coord_def pos : blast_sources) {
            _ignition_square(agent, beam_actual, pos, true);
        }
        for (coord_def pos : blast_adjacents) {
            _ignition_square(agent, beam_actual, pos, false);
        }
    }

    return SPRET_SUCCESS;
}

int discharge_monsters(coord_def where, int pow, actor *agent, int prior_arcs)
{
    actor* victim = actor_at(where);

    if (!victim)
        return 0;

    int damage = (agent == victim) ? 1 + random2(3 + div_rand_round(pow,15))
                                   : 3 + random2(5 + div_rand_round(pow,7));

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
		damage = 1 + random2(3 + div_rand_round(pow,15));
        dprf("You: static discharge damage: %d", damage);
        damage = check_your_resists(damage, BEAM_ELECTRICITY,
                                    "static discharge");
        mprf("You are struck by lightning (%d).", damage);
        
        ouch(damage, KILLED_BY_BEAM, agent->mid, "by static electricity", true,
             agent->is_player() ? "you" : agent->name(DESC_A).c_str());
        if (damage > 0)
            victim->expose_to_element(BEAM_ELECTRICITY, 2);
    }
    else if (victim->res_elec() > 0)
        return 0;
    else
    {
        monster* mons = victim->as_monster();
        ASSERT(mons);

        dprf("%s: static discharge damage: %d",
             mons->name(DESC_PLAIN, true).c_str(), damage);
        damage = mons_adjust_flavoured(mons, beam, damage);
        if (damage > 0)
            victim->expose_to_element(BEAM_ELECTRICITY, 2);

        if (damage)
        {
            mprf("%s is struck by lightning (%d).",
                 mons->name(DESC_THE).c_str(),
				 damage);
            if (agent->is_player())
            {
                _player_hurt_monster(*mons, damage);

                if (is_sanctuary(you.pos()) || is_sanctuary(mons->pos()))
                    remove_sanctuary(true);
            }
            else
                mons->hurt(agent->as_monster(), damage);
        }
    }

    // Recursion to give us chain-lightning -- bwr
    // Smooth out some of the low end nonsense -- me
    if (x_chance_in_y(3, 4 + (2 * prior_arcs)) && prior_arcs < div_rand_round(pow, 20))
    {
        mpr("The lightning arcs!");
        damage += apply_random_around_square([pow, agent, prior_arcs] (coord_def where2) {
            return discharge_monsters(where2, pow, agent, prior_arcs + 1);
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

static bool _safe_discharge(coord_def where, vector<const monster *> &exclude)
{
    for (adjacent_iterator ai(where); ai; ++ai)
    {
        const monster *mon = monster_at(*ai);
        if (!mon)
            continue;

        if (find(exclude.begin(), exclude.end(), mon) == exclude.end())
        {
            // Harmless to these monsters, so don't prompt about hitting them
            if (mon->res_elec() > 0)
                continue;

            if (stop_attack_prompt(mon, false, where))
                return false;

            exclude.push_back(mon);
            if (!_safe_discharge(mon->pos(), exclude))
                return false;
        }
    }

    return true;
}

spret_type cast_discharge(int pow, bool fail, bool battlesphere)
{
    fail_check();
    
    monster* avatar = find_battlesphere(&you);
    coord_def pos = avatar && battlesphere ? avatar->pos() : you.pos();

    vector<const monster *> exclude;
    if (!battlesphere && !_safe_discharge(you.pos(), exclude))
        return SPRET_ABORT;

    // this formula is fucking stupid but at least it uses div_rand_round now!
    const int num_targs = 1 + random2(random_range(1, 3) + div_rand_round(pow, 20));
    const int dam =
        apply_random_around_square([pow] (coord_def where) {
            return discharge_monsters(where, pow, &you);
        }, pos, true, num_targs);

    dprf("Arcs: %d Damage: %d", num_targs, dam);

    if (dam > 0)
        scaled_delay(100);
    else
    {
        mpr("The air crackles with electrical energy.");
    }
    return SPRET_SUCCESS;
}

bool setup_fragmentation_beam(bolt &beam, int pow, const actor *caster,
                              const coord_def target, bool quiet,
                              const char **what, bool &should_destroy_wall,
                              bool &hole)
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
    beam.damage = dice_def(0, 5 + pow / 5);

    monster* mon = monster_at(target);
    const dungeon_feature_type grid = grd(target);

    if (target == you.pos())
    {
        const bool petrified = (you.petrified() || you.petrifying());

        if (you.form == TRAN_STATUE || you.species == SP_GARGOYLE)
        {
            beam.name       = "blast of rock fragments";
            beam.colour     = BROWN;
            beam.damage.num = you.form == TRAN_STATUE ? 3 : 2;
            return true;
        }
        else if (petrified)
        {
            beam.name       = "blast of petrified fragments";
            beam.colour     = mons_class_colour(player_mons(true));
            beam.damage.num = 3;
            return true;
        }
        else if (you.form == TRAN_ICE_BEAST) // blast of ice
        {
            beam.name       = "icy blast";
            beam.colour     = WHITE;
            beam.damage.num = 3;
            beam.flavour    = BEAM_ICE;
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
            beam.damage.num = 3;
            break;

        case MONS_IRON_ELEMENTAL:
        case MONS_PEACEKEEPER:
        case MONS_WAR_GARGOYLE:
            beam.name       = "blast of metal fragments";
            beam.colour     = CYAN;
            beam.damage.num = 4;
            break;

        case MONS_EARTH_ELEMENTAL:
        case MONS_USHABTI:
        case MONS_STATUE:
        case MONS_GARGOYLE:
            beam.name       = "blast of rock fragments";
            beam.colour     = BROWN;
            beam.damage.num = 3;
            break;

        case MONS_SALTLING:
            beam.name       = "blast of salt crystal fragments";
            beam.colour     = WHITE;
            beam.damage.num = 3;
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
                beam.damage.num = 3;
                break;
            }
            else if (mon->is_icy()) // blast of ice
            {
                beam.name       = "icy blast";
                beam.colour     = WHITE;
                beam.damage.num = 3;
                beam.flavour    = BEAM_ICE;
                break;
            }
            else if (mon->is_skeletal()) // blast of bone
            {
                beam.name   = "blast of bone shards";
                beam.colour = LIGHTGREY;
                beam.damage.num = 3;
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

    if (env.markers.property_at(target, MAT_ANY,
                                "veto_fragmentation") == "veto")
    {
        if (caster->is_player() && !quiet)
        {
            mprf("%s seems to be unnaturally hard.",
                 feature_description_at(target, false, DESC_THE, false).c_str());
        }
        return false;
    }

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
        // fall through
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

    // Stone doors and arches
    case DNGN_OPEN_DOOR:
    case DNGN_CLOSED_DOOR:
    case DNGN_RUNED_DOOR:
    case DNGN_SEALED_DOOR:
        // Doors always blow up, stone arches never do (would cause problems).
        if (what)
            *what = "door";
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

spret_type cast_fragmentation(int pow, const actor *caster,
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
        return SPRET_ABORT;
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
            return SPRET_ABORT;
        }
    }

    monster* mon = monster_at(target);

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
        mpr("You shatter!");

        ouch(beam.damage.roll(), KILLED_BY_BEAM, caster->mid,
             "by Lee's Rapid Deconstruction", true,
             caster->is_player() ? "you"
                                 : caster->name(DESC_A).c_str());
    }
    else // Monster explodes.
    {
        if (you.see_cell(target))
            mprf("%s shatters!", mon->name(DESC_THE).c_str());

        if (caster->is_player())
            _player_hurt_monster(*mon, beam.damage.roll(), BEAM_DISINTEGRATION);
        else
            mon->hurt(caster, beam.damage.roll(), BEAM_DISINTEGRATION);
    }

    beam.explode(true, hole);

    return SPRET_SUCCESS;
}

spret_type cast_sandblast(int pow, bool fail, bool tracer, bool battlesphere)
{
    monster* avatar = find_battlesphere(&you);
    
    if(!avatar && battlesphere)
        return SPRET_SUCCESS;
    
    coord_def source = battlesphere ? avatar->pos() : you.pos();
    
    monster* target = _closest_target_in_range(min(3,LOS_RADIUS), source);
    
    if (tracer)
    {
        if (!target)
            return SPRET_ABORT;
        else
            return SPRET_SUCCESS;
    }
    
    fail_check();

    if (!target)
    {
        if (!battlesphere)
            canned_msg(MSG_NOTHING_HAPPENS);
    }
    else
    {
        bolt beam;
        beam.set_agent(&you);
        beam.flavour            = BEAM_FRAG;
        beam.real_flavour       = BEAM_FRAG;
        beam.hit                = 13 + div_rand_round(pow, 10);
        beam.damage             = calc_dice(1, 6 + div_rand_round(pow * 2, 3));
        beam.name               = "rocky blast";
        beam.origin_spell       = SPELL_SANDBLAST;
        beam.target             = target->pos();
        beam.source             = source;
        beam.range              = min(3,LOS_RADIUS);
        beam.colour             = BROWN;
        beam.glyph              = DCHAR_FIRED_BOLT;
        
        beam.fire();
    }
    
    return SPRET_SUCCESS;
}

static bool _elec_not_immune(const actor *act)
{
    return act->res_elec() < 3;
}

spret_type cast_thunderbolt(actor *caster, int pow, coord_def aim, bool fail)
{
    coord_def prev;
    if (caster->props.exists("thunderbolt_last")
        && caster->props["thunderbolt_last"].get_int() + 1 == you.num_turns)
    {
        prev = caster->props["thunderbolt_aim"].get_coord();
    }

    targetter_thunderbolt hitfunc(caster, spell_range(SPELL_THUNDERBOLT, pow),
                                  prev);
    hitfunc.set_aim(aim);

    if (caster->is_player())
    {
        if (stop_attack_prompt(hitfunc, "zap", _elec_not_immune))
            return SPRET_ABORT;
    }

    fail_check();

    int juice = prev.origin() ? 2 * LIGHTNING_CHARGE_MULT
                              : caster->props["thunderbolt_mana"].get_int();
    bolt beam;
    beam.name              = "lightning";
    beam.aux_source        = "rod of lightning";
    beam.flavour           = BEAM_ELECTRICITY;
    beam.glyph             = dchar_glyph(DCHAR_FIRED_BURST);
    beam.colour            = LIGHTCYAN;
    beam.range             = 1;
    // Dodging a horizontal arc is nearly impossible: you'd have to fall prone
    // or jump high.
    beam.hit               = prev.origin() ? 10 + pow / 20 : 1000;
    beam.ac_rule           = AC_PROPORTIONAL;
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
        beam.damage = dice_def(div_rand_round(juice, LIGHTNING_CHARGE_MULT),
                               div_rand_round(30 + pow / 6, arc + 2));
        beam.fire();
    }

    caster->props["thunderbolt_last"].get_int() = you.num_turns;
    caster->props["thunderbolt_aim"].get_coord() = aim;

    noisy(15 + div_rand_round(juice, LIGHTNING_CHARGE_MULT), hitfunc.origin);

    return SPRET_SUCCESS;
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

                if (!apply_chunked_AC(1, foe->evasion(EV_IGNORE_NONE, mon)))
                {
                    msg = "A tree reaches out but misses @foe@.";
                }
                else if (!(dmg = foe->apply_ac(hd + random2(hd), hd * 2 - 1,
                                               AC_PROPORTIONAL)))
                {
                    msg = "A tree reaches out and scrapes @foe@!";
                }
                else
                {
                    msg = "A tree reaches out and hits @foe@ (@dmg@)!";
                }

                std::string d = to_string(dmg);
                msg = replace_all(replace_all(msg,
                    "@foe@", foe->name(DESC_THE)),
                    "@dmg@", d);
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

            //Don't aim at fire immune enemies
            if(monster_at(*di)->res_fire() >= 3)
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

spret_type cast_dazzling_flash(int pow, bool fail, bool tracer)
{
    int range = spell_range(SPELL_DAZZLING_FLASH, pow);
    targetter_radius hitfunc(&you, LOS_SOLID_SEE, range);
    bool (*vulnerable) (const actor *) = [](const actor * act) -> bool
    {
        // No fedhas checks needed, plants can't be dazzled
        return act->is_monster()
               && mons_can_be_dazzled(act->as_monster()->type);
    };

    if (tracer)
    {
        for (radius_iterator ri(you.pos(), range, C_SQUARE, LOS_SOLID_SEE, true); ri; ++ri)
        {
            if (!in_bounds(*ri))
                continue;

            const monster* mon = monster_at(*ri);

            if (!mon || !you.can_see(*mon))
                continue;

            if (!mon->friendly() && (*vulnerable)(mon))
                return SPRET_SUCCESS;
        }

        return SPRET_ABORT;
    }


    // [eb] the simulationist in me wants to use LOS_DEFAULT
    // and let this blind through glass
    if (stop_attack_prompt(hitfunc, "dazzle", vulnerable))
        return SPRET_ABORT;

    fail_check();

    bolt beam;
    beam.name = "energy";
    beam.flavour = BEAM_VISUAL;
    beam.origin_spell = SPELL_DAZZLING_FLASH;
    beam.set_agent(&you);
    beam.colour = WHITE;
    beam.glyph = dchar_glyph(DCHAR_EXPLOSION);
    beam.range = range;
    beam.ex_size = range;
    beam.is_explosion = true;
    beam.source = you.pos();
    beam.target = you.pos();
    beam.hit = AUTOMATIC_HIT;
    beam.loudness = 0;
    beam.explode(true, true);

    for (radius_iterator ri(you.pos(), range, C_SQUARE, LOS_SOLID_SEE, true);
         ri; ++ri)
    {
        monster* mons = monster_at(*ri);

        if (!mons || !mons_can_be_dazzled(mons->type))
            continue;

        if (mons->check_res_magic(pow) <= 0)
        {
            simple_monster_message(*mons, " is dazzled.");
            mons->add_ench(mon_enchant(ENCH_BLIND, 1, &you,
                           random_range(4, 8) * BASELINE_DELAY));
        }
    }

    return SPRET_SUCCESS;
}

static bool _toxic_can_affect(const actor *act)
{
    if (act->is_monster() && act->as_monster()->submerged())
        return false;

    // currently monsters are still immune at rPois 1
    return act->res_poison() < (act->is_player() ? 3 : 1);
}

spret_type cast_toxic_radiance(actor *agent, int pow, bool fail, bool mon_tracer)
{
    if (agent->is_player())
    {
        targetter_los hitfunc(&you, LOS_NO_TRANS);
        {
            if (stop_attack_prompt(hitfunc, "poison", _toxic_can_affect))
                return SPRET_ABORT;
        }
        fail_check();

        if (!you.duration[DUR_TOXIC_RADIANCE])
            mpr("You begin to radiate toxic energy.");
        else
            mpr("Your toxic radiance grows in intensity.");

        you.increase_duration(DUR_TOXIC_RADIANCE, 3 + random2(pow/20), 15);

        flash_view_delay(UA_PLAYER, GREEN, 300, &hitfunc);

        return SPRET_SUCCESS;
    }
    else if (mon_tracer)
    {
        for (actor_near_iterator ai(agent->pos(), LOS_NO_TRANS); ai; ++ai)
        {
            if (!_toxic_can_affect(*ai) || mons_aligned(agent, *ai))
                continue;
            else
                return SPRET_SUCCESS;
        }

        // Didn't find any susceptible targets
        return SPRET_ABORT;
    }
    else
    {
        monster* mon_agent = agent->as_monster();
        simple_monster_message(*mon_agent,
                               " begins to radiate toxic energy.");

        mon_agent->add_ench(mon_enchant(ENCH_TOXIC_RADIANCE, 1, mon_agent,
                                        (5 + random2avg(pow/15, 2)) * BASELINE_DELAY));

        targetter_los hitfunc(mon_agent, LOS_NO_TRANS);
        flash_view_delay(UA_MONSTER, GREEN, 300, &hitfunc);

        return SPRET_SUCCESS;
    }
}

void toxic_radiance_effect(actor* agent, int mult)
{
    int pow;
    if (agent->is_player())
        pow = calc_spell_power(SPELL_OLGREBS_TOXIC_RADIANCE, true);
    else
        pow = agent->as_monster()->get_hit_dice() * 8;

    bool break_sanctuary = (agent->is_player() && is_sanctuary(you.pos()));

    for (actor_near_iterator ai(agent->pos(), LOS_NO_TRANS); ai; ++ai)
    {
        if (!_toxic_can_affect(*ai))
            continue;

        // Monsters can skip hurting friendlies
        if (agent->is_monster() && mons_aligned(agent, *ai))
            continue;

        int dam = roll_dice(1, 1 + pow / 20) * mult;
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
            ai->hurt(agent, dam, BEAM_POISON);
            if (ai->alive())
            {
                behaviour_event(ai->as_monster(), ME_ANNOY, agent,
                                agent->pos());
                if (coinflip() || !ai->as_monster()->has_ench(ENCH_POISON))
                    poison_monster(ai->as_monster(), agent, 1);
            }

            if (agent->is_player() && is_sanctuary(ai->pos()))
                break_sanctuary = true;
        }
    }

    if (break_sanctuary)
        remove_sanctuary(true);
}

spret_type cast_searing_ray(int pow, bolt &beam, bool fail)
{
    const spret_type ret = zapping(ZAP_SEARING_RAY_I, pow, beam, true, nullptr,
                                   fail);

    if (ret == SPRET_SUCCESS)
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
	beam.flavour = BEAM_FIRE; 
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

    beam.fire();

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

static int _quake_cell(coord_def where, int pow, int wall_count, bool knockback)
{
    monster *mons = monster_at(where);
    if (!mons)
        return 0; 
   
    bolt beam;
    beam.name = knockback ? "aftershock" : "quake";
    beam.flavour = BEAM_MMISSILE;
    beam.set_agent(&you);
    beam.range = LOS_RADIUS;
    beam.source = you.pos();
    beam.target = where;
    beam.ench_power = pow;
    beam.hit = AUTOMATIC_HIT;
    beam.damage = calc_dice(1, div_rand_round(8 + div_rand_round(pow,6) * 10, 10 + wall_count));
    beam.fire();

    if (is_sanctuary(you.pos()))
        remove_sanctuary(true);

    return 1;
}

spret_type cast_force_quake(int pow, bool fail)
{
    if (you.attribute[ATTR_FORCE_QUAKE] > 0)
    {
        mprf("You are already unleashing a force quake!");
        return SPRET_ABORT;
    }
    
    fail_check();
    
    you.attribute[ATTR_FORCE_QUAKE] = 1;
    
    return SPRET_SUCCESS;
}

void force_quake()
{
    ASSERT_RANGE(you.attribute[ATTR_FORCE_QUAKE], 1, 4);
    
    bool knockback = false;
    
    if(++you.attribute[ATTR_FORCE_QUAKE] > 3)
        knockback = true;
    
    int wall_count = 0;
    
    //iterate once to get the wall count
    for (adjacent_iterator ai(you.pos()); ai; ++ai)
    {
        if(cell_is_solid(*ai))
            wall_count++;
    }
    
    bolt beam;
    beam.name = "force quake";
    beam.flavour = BEAM_VISUAL;
    beam.origin_spell = SPELL_FORCE_QUAKE;
    beam.set_agent(&you);
    beam.colour = ETC_EARTH;
    beam.glyph = dchar_glyph(DCHAR_EXPLOSION);
    beam.range = 1;
    beam.ex_size = 1;
    beam.is_explosion = true;
    beam.explode_delay = beam.explode_delay * 1 / 2;
    beam.source = you.pos();
    beam.target = you.pos();
    beam.hit = AUTOMATIC_HIT;
    beam.loudness = 0;
    beam.explode(true, true);
    
    int pow = calc_spell_power(SPELL_FORCE_QUAKE, true);

    apply_random_around_square([pow, wall_count, knockback] (coord_def where) {
        return _quake_cell(where, pow, wall_count, knockback);
    }, you.pos(), true, 8);
    
    if (knockback)
        you.attribute[ATTR_FORCE_QUAKE] = 0;
}

spret_type cast_essence_spray(int pow, bool fail, bool tracer)
{
    targetter_radius hitfunc(&you, LOS_NO_TRANS, 1);
    bool (*vulnerable) (const actor *) = [](const actor * act) -> bool
    {
        return act->is_monster()
               && !act->wont_attack()
               && !act->res_torment()
               && act->res_magic() != MAG_IMMUNE;
    };
    
    if (stop_attack_prompt(hitfunc, "essence spray", vulnerable))
        return SPRET_ABORT;
    
    bool success = false;
    
    bolt beam;
    beam.name = "essence_spray";
    beam.flavour = BEAM_PAIN;
    beam.set_agent(&you);
    beam.colour = BLACK;
    beam.range = 1;
    beam.hit = AUTOMATIC_HIT;
    beam.source = you.pos();
    beam.ench_power = div_rand_round(pow * 7, 2);
    
    for (adjacent_iterator ai(you.pos()); ai; ++ai) 
    {
        if (cell_is_solid(*ai))
            continue;

        actor *act = actor_at(*ai);

        if (act
            && act->is_monster()
            && !act->wont_attack()
            && !act->res_torment()
            && act->res_magic() != MAG_IMMUNE)
        {
            // at least one valid target
            if (tracer)
                return SPRET_SUCCESS;
            else
            {
                beam.target = act->pos();
                beam.damage = calc_dice(1, 4 + div_rand_round(pow, 5));
                beam.fire();
                
                success = true;
            }
        }
    }
    // no valid targets
    if (tracer)
        return SPRET_ABORT;
    
    if (success)
        dec_hp(1, false);
    else
        canned_msg(MSG_NOTHING_HAPPENS);
    
    return SPRET_SUCCESS;
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

spret_type cast_glaciate(actor *caster, int pow, coord_def aim, bool fail)
{
    const int range = spell_range(SPELL_GLACIATE, pow);
    targetter_cone hitfunc(caster, range);
    hitfunc.set_aim(aim);

    if (caster->is_player()
        && stop_attack_prompt(hitfunc, "glaciate", _player_glaciate_affects))
    {
        return SPRET_ABORT;
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

            // At or within range 3, this is equivalent to the old Ice Storm
            // damage.
            beam.damage =
                caster->is_player()
                    ? calc_dice(7, (66 + 3 * pow) / eff_range)
                    : calc_dice(10, (54 + 3 * pow / 2) / eff_range);

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

    return SPRET_SUCCESS;
}

spret_type cast_random_bolt(int pow, bolt& beam, bool fail)
{
    // Need to use a 'generic' tracer regardless of the actual beam type,
    // to account for the possibility of both bouncing and irresistible damage
    // (even though only one of these two ever occurs on the same bolt type).
    bolt tracer = beam;
    if (!player_tracer(ZAP_RANDOM_BOLT_TRACER, 200, tracer))
        return SPRET_ABORT;

    fail_check();

    zap_type zap = random_choose(ZAP_BOLT_OF_FIRE,
                                 ZAP_BOLT_OF_COLD,
                                 ZAP_VENOM_BOLT,
                                 ZAP_BOLT_OF_DRAINING,
                                 ZAP_QUICKSILVER_BOLT,
                                 ZAP_CRYSTAL_BOLT,
                                 ZAP_LIGHTNING_BOLT,
                                 ZAP_CORROSIVE_BOLT);
    zapping(zap, pow * 7 / 6 + 15, beam, false);

    return SPRET_SUCCESS;
}

size_t shotgun_beam_count(int pow)
{
    return 1 + stepdown((pow - 5) / 3, 5, ROUND_CLOSE);
}

spret_type cast_scattershot(const actor *caster, int pow, const coord_def &pos,
                            bool fail)
{
    const size_t range = spell_range(SPELL_SCATTERSHOT, pow);
    const size_t beam_count = shotgun_beam_count(pow);

    targetter_shotgun hitfunc(caster, beam_count, range);

    hitfunc.set_aim(pos);

    if (caster->is_player())
    {
        if (stop_attack_prompt(hitfunc, "scattershot"))
            return SPRET_ABORT;
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

    return SPRET_SUCCESS;
}

spret_type cast_starburst(int pow, bool fail, bool tracer, bool battlesphere)
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

    monster* avatar = find_battlesphere(&you);
    if (battlesphere && !avatar)
        return SPRET_SUCCESS;

    bolt beam;
    beam.range        = range;
    beam.source       = battlesphere ? avatar->pos() : you.pos();
    beam.source_id    = MID_PLAYER;
    beam.is_tracer    = tracer && !battlesphere;
    beam.is_targeting = tracer && !battlesphere;
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
        if (!tracer && !battlesphere && !player_tracer(ZAP_BOLT_OF_FIRE, pow, beam))
            return SPRET_ABORT;

        if (tracer)
        {
            beam.fire();
            // something to hit
            if (beam.foe_info.count > 0)
                return SPRET_SUCCESS;
        }
    }

    if (tracer)
        return SPRET_ABORT;

    fail_check();

    // Randomize for nice animations
    shuffle_array(offsets);
    for (auto & offset : offsets)
    {
        beam.target = beam.source + offset;
        beam.fire();
    }

    return SPRET_SUCCESS;
}


void foxfire_attack(const monster *foxfire, const actor *target)
{
    actor * summoner = actor_by_mid(foxfire->summoner);

    // Don't allow foxfires that have wandered off to attack before dissapating
    if (summoner && !(summoner->can_see(*foxfire)
                      && summoner->see_cell(target->pos())))
    {
        return;
    }

    bolt beam;
    beam.thrower = (foxfire && foxfire->friendly()) ? KILL_YOU :
                   (foxfire)                       ? KILL_MON
                                                  : KILL_MISC;
    beam.range       = 1;
    beam.source      = foxfire->pos();
    beam.source_id   = foxfire->summoner;
    beam.source_name = summoner->name(DESC_PLAIN, true);
    zappy(ZAP_FOXFIRE, foxfire->get_hit_dice(), !foxfire->friendly(), beam);
    beam.aux_source  = beam.name;
    beam.target      = target->pos();
    beam.fire();
}

void actor_apply_quicksand(actor * act)
{
    if (grd(act->pos()) != DNGN_QUICKSAND)
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
            if (tmarker->change_type == TERRAIN_CHANGE_QUICKSAND)
                oppressor = actor_by_mid(tmarker->mon_num);
        }
    }

    const int damage = dice_def(1, 18).roll();

    const int final_damage = timescale_damage(act, damage);

    if (player && final_damage > 0)
    {
        mprf("You are sucked in by the quicksand (%d).",
                final_damage);
    }
    else if (final_damage > 0)
    {
        behaviour_event(mons, ME_DISTURB, 0, act->pos());
        mprf("%s is sucked in by the quicksand (%d).",
                mons->name(DESC_THE).c_str(),
                final_damage);
    }

    if (final_damage)
    {

        const string oppr_name =
            oppressor ? " "+apostrophise(oppressor->name(DESC_THE))
                      : "";

        act->hurt(oppressor, final_damage, BEAM_MISSILE,
                  KILLED_BY_POISON, "", "quicksand");
    }
}

spret_type untargeted_iood(int pow, bool fail, bool tracer)
{
    coord_def target = random_target_in_range(LOS_RADIUS);
    
    if (tracer)
    {
        if (!in_bounds(target))
            return SPRET_ABORT;
        
        return SPRET_SUCCESS;
    }

    fail_check();
    
    if (!in_bounds(target))
        canned_msg(MSG_NOTHING_HAPPENS);
    else
    {
        monster *mon = place_monster(mgen_data(MONS_ORB_OF_DESTRUCTION,
                BEH_FRIENDLY, coord_def(),
                monster_at(target) ? mgrd(target) :
                MHITNOT).set_summoned(&you, 0, SPELL_IOOD), true, true);
                
        if (!mon)
        {
            mprf(MSGCH_ERROR, "Failed to spawn projectile.");
            return SPRET_ABORT;
        }
        
        // approximate the angle between us and the target
        double xdist = static_cast<double>(target.x) - static_cast<double>(you.pos().x);
        double ydist = static_cast<double>(target.y) - static_cast<double>(you.pos().y);
        xdist += random_choose(-0.1, 0.1);
        ydist += random_choose(-0.1, 0.1);
        const double angle = atan2(ydist, xdist);
        
        mon->props[IOOD_X].get_float() = you.pos().x + random_choose(-0.1, 0.1);
        mon->props[IOOD_Y].get_float() = you.pos().y + random_choose(-0.1, 0.1);
        mon->props[IOOD_VX].get_float() = cos(angle);
        mon->props[IOOD_VY].get_float() = sin(angle);
        mon->props[IOOD_KC].get_byte() = KC_YOU;
        mon->props[IOOD_POW].get_short() = pow;
        mon->flags &= ~MF_JUST_SUMMONED;
        mon->props[IOOD_CASTER].get_string() = "you";
        mon->summoner = you.mid;
        mon->props[IOOD_FLAWED].get_byte() = true;
        
        // Move away from the caster's square.
        iood_act(*mon, true);

        // If the foe was adjacent to the caster, that might have destroyed it.
        if (mon->alive())
        {
            // We need to take at least one full move (for the above), but let's
            // randomize it and take more so players won't get guaranteed instant
            // damage.
            mon->lose_energy(EUT_MOVE, 2, random2(3)+2);
        }
    }

    return SPRET_SUCCESS;
}

static void _stone_shard_cell(coord_def target, int frags, int pow, int total_frags)
{
    bolt beam_visual;
    beam_visual.set_agent(&you);
    beam_visual.flavour       = BEAM_VISUAL;
    beam_visual.glyph         = dchar_glyph(DCHAR_FIRED_BURST);
    beam_visual.colour        = BROWN;
    beam_visual.ex_size       = 1;
    beam_visual.is_explosion  = true;
    
    beam_visual.explosion_draw_cell(target);
    
    update_screen();
    scaled_delay(5);
    
    bolt beam;
    beam.name = "stone shard";
    beam.flavour = BEAM_FRAG;
    beam.set_agent(&you);
    beam.colour = BROWN;
    beam.glyph = dchar_glyph(DCHAR_EXPLOSION);
    beam.range = 1;
    beam.source = target;
    beam.target = target;
    beam.hit = AUTOMATIC_HIT;
    beam.loudness = spell_effect_noise(SPELL_STONE_SHARDS);
    
    int adjusted_power = div_rand_round(pow * frags, total_frags);
    
    beam.damage = calc_dice(1, div_rand_round(adjusted_power * 5, 4));
    beam.fire();
    
    place_cloud(CLOUD_DUST_TRAIL, target, 2, &you);
}

static int _num_stone_shards(const actor* ai)
{
    int frags = 0;
    
    if (ai->petrified() || ai->petrifying())
            frags +=2;
        
    for(adjacent_iterator bi(ai->pos()); bi; ++bi)
    {
        if (feat_is_wall(env.grid(*bi)) || feat_is_statuelike(env.grid(*bi)))
            frags++;
        else
        {
            const actor* act = actor_at(*bi);
            if (act && (act->petrified() || act->petrifying()))
                    frags++;
        }
    }
    
    return frags;
}

spret_type stone_shards(int pow, bool fail, bool tracer, bool battlesphere)
{
    monster* avatar = find_battlesphere(&you);
    
    if(!avatar && battlesphere)
        return SPRET_SUCCESS;
    
    int range = spell_range(SPELL_STONE_SHARDS, pow);
    int total_frags = 0;
    vector<pair <coord_def, int> > targets;
    
    coord_def center = battlesphere ? avatar->pos() : you.pos();
    
    for (actor_near_iterator ai(center, LOS_NO_TRANS); ai; ++ai)
    {
        if (grid_distance(ai->pos(), center) > range)
            continue;
        
        // don't damage the source tile
        if (ai->pos() == center)
            continue;
        
        int frags = _num_stone_shards(*ai);
        
        if(frags > 0)
        {
            targets.emplace_back(make_pair(ai->pos(), frags));
            total_frags += frags;
        }
    }
    
    if (tracer)
    {
        if (total_frags == 0)
            return SPRET_ABORT;
        else
            return SPRET_SUCCESS;
    }
    
    if (total_frags > 0 && !battlesphere)
    {
        targetter_radius hitfunc(&you, LOS_SOLID_SEE, range);
        bool (*vulnerable) (const actor *) = [](const actor * act) -> bool
        {
        return act->is_monster()
               && _num_stone_shards(act) > 0;
        };
        
        if (stop_attack_prompt(hitfunc, "shard", vulnerable))
            return SPRET_ABORT;
    }
    
    fail_check();
    
    if (total_frags == 0)
    {
        if (!battlesphere)
            canned_msg(MSG_NOTHING_HAPPENS);
    }
    else
    {
        for (int i = 0; i < static_cast<int>(targets.size()); i++)
        {
            _stone_shard_cell(targets[i].first, targets[i].second, pow, total_frags);
        }
    }
    
    return SPRET_SUCCESS;
}

spret_type cast_affliction(int pow, bool fail, bool tracer)
{
    if (tracer)
    {
        for (adjacent_iterator ai(you.pos()); ai; ++ai) 
        {
            if (cell_is_solid(*ai))
                continue;

            actor *act = actor_at(*ai);

            if (act
                && act->is_monster()
                && !act->wont_attack()
                && !act->res_torment()
                && act->res_magic() != MAG_IMMUNE)
            {
                // at least one valid target
                return SPRET_SUCCESS;    
            }
        }
        return SPRET_ABORT;
    }
    
    targetter_radius hitfunc(&you, LOS_NO_TRANS, 1);
    bool (*vulnerable) (const actor *) = [](const actor * act) -> bool
    {
        return act->is_monster()
               && !act->wont_attack()
               && !act->res_torment()
               && act->res_magic() != MAG_IMMUNE;
    };
    
    if (stop_attack_prompt(hitfunc, "afflict", vulnerable))
        return SPRET_ABORT;
    
    
    mprf("You unleash a malevolent force.");
    
    for (fair_adjacent_iterator ai(you.pos()); ai; ++ai) 
    {
        if (cell_is_solid(*ai))
            continue;

        actor *act = actor_at(*ai);

        if (act && act->is_monster()
                && !act->as_monster()->wont_attack()
                && !act->res_torment()
                && act->check_res_magic(div_rand_round(pow * 4, 3)) < 0)
        {
            torment_cell(act->pos(), &you, TORMENT_AGONY);
            return SPRET_SUCCESS;
        }
    }
    
    mprf("The malevolent force dissipates harmlessly.");
    return SPRET_SUCCESS;
}