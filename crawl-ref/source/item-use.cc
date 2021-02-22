/**
 * @file
 * @brief Functions for making use of inventory items.
**/

#include "AppHdr.h"

#include "item-use.h"

#include "ability.h"
#include "acquire.h"
#include "act-iter.h"
#include "areas.h"
#include "artefact.h"
#include "art-enum.h"
#include "butcher.h"
#include "chardump.h"
#include "cloud.h"
#include "colour.h"
#include "coordit.h"
#include "database.h"
#include "delay.h"
#include "describe.h"
#include "directn.h"
#include "english.h"
#include "env.h"
#include "evoke.h"
#include "exercise.h"
#include "fight.h"
#include "food.h"
#include "god-abil.h"
#include "god-conduct.h"
#include "god-item.h"
#include "god-passive.h"
#include "hints.h"
#include "invent.h"
#include "item-prop.h"
#include "item-status-flag-type.h"
#include "items.h"
#include "level-state-type.h"
#include "libutil.h"
#include "macro.h"
#include "makeitem.h"
#include "message.h"
#include "misc.h"
#include "mon-behv.h"
#include "mutation.h"
#include "nearby-danger.h"
#include "orb.h"
#include "output.h"
#include "player-equip.h"
#include "player-stats.h"
#include "player.h"
#include "potion.h"
#include "prompt.h"
#include "religion.h"
#include "rot.h"
#include "shout.h"
#include "skills.h"
#include "sound.h"
#include "spl-book.h"
#include "spl-clouds.h"
#include "spl-goditem.h"
#include "spl-selfench.h"
#include "spl-summoning.h"
#include "spl-transloc.h"
#include "spl-wpnench.h"
#include "spl-zap.h"
#include "state.h"
#include "stringutil.h"
#include "target.h"
#include "terrain.h"
#include "throw.h"
#include "tiles-build-specific.h"
#include "transform.h"
#include "uncancel.h"
#include "unwind.h"
#include "view.h"
#include "xom.h"

// The menu class for using items from either inv or floor.
// Derivative of InvMenu

class UseItemMenu : public InvMenu
{
    void populate_list();
    void populate_menu();
    bool process_key(int key) override;
    void repopulate_menu();

public:
    bool display_all;
    bool is_inventory;
    int item_type_filter;

    vector<const item_def*> item_inv;
    vector<const item_def*> item_floor;

    // Constructor
    // Requires int for item filter.
    // Accepts:
    //      OBJ_POTIONS
    //      OBJ_SCROLLS
    //      OSEL_WIELD
    //      OBJ_ARMOUR
    //      OBJ_FOOD
    UseItemMenu(int selector, const char* prompt);

    void toggle_display_all();
    void toggle_inv_or_floor();
};

UseItemMenu::UseItemMenu(int item_type, const char* prompt)
    : InvMenu(MF_SINGLESELECT), display_all(false), is_inventory(true),
      item_type_filter(item_type)
{
    set_title(prompt);
    populate_list();
    populate_menu();
}

void UseItemMenu::populate_list()
{
    // Load inv items first
    for (const auto &item : you.inv)
    {
        if (item.defined())
            item_inv.push_back(&item);
    }
    // Load floor items...
    item_floor = item_list_on_square(you.visible_igrd(you.pos()));
    // ...only stuff that can go into your inventory though
    erase_if(item_floor, [=](const item_def* it)
    {
        // Did we get them all...?
        return !it->defined() || item_is_stationary(*it) || item_is_orb(*it)
            || item_is_spellbook(*it) || it->base_type == OBJ_GOLD
            || it->base_type == OBJ_RUNES;
    });

    // Filter by type
    if (!display_all)
    {
        erase_if(item_inv, [=](const item_def* item)
        {
            return !item_is_selected(*item, item_type_filter);
        });
        erase_if(item_floor, [=](const item_def* item)
        {
            return !item_is_selected(*item, item_type_filter);
        });
    }
}

void UseItemMenu::populate_menu()
{
    if (item_inv.empty())
        is_inventory = false;
    else if (item_floor.empty())
        is_inventory = true;

    // Entry for unarmed
    if (item_type_filter == OSEL_WIELD)
    {
        string hands_title = " -   unarmed";
        MenuEntry *hands = new MenuEntry (hands_title, MEL_ITEM);
        add_entry(hands);
    }

    if (!item_inv.empty())
    {
        // Only clarify that these are inventory items if there are also floor
        // items.
        if (!item_floor.empty())
        {
            string subtitle_text = "Inventory Items";
            if (!is_inventory)
                subtitle_text += " (',' to select)";
            auto subtitle = new MenuEntry(subtitle_text, MEL_TITLE);
            subtitle->colour = LIGHTGREY;
            add_entry(subtitle);
        }

        // nullptr means using the items' normal hotkeys
        if (is_inventory)
            load_items(item_inv);
        else
        {
            load_items(item_inv,
                        [&](MenuEntry* entry) -> MenuEntry*
                        {
                            entry->hotkeys.clear();
                            return entry;
                        });
        }
    }

    if (!item_floor.empty())
    {
#ifndef USE_TILE
        // vertical padding for console
        if (!item_inv.empty())
            add_entry(new MenuEntry("", MEL_TITLE));
#endif
        // Load floor items to menu
        string subtitle_text = "Floor Items";
        if (is_inventory)
            subtitle_text += " (',' to select)";
        auto subtitle = new MenuEntry(subtitle_text, MEL_TITLE);
        subtitle->colour = LIGHTGREY;
        add_entry(subtitle);

        // nullptr means using a-zA-Z
        if (is_inventory)
        {
            load_items(item_floor,
                        [&](MenuEntry* entry) -> MenuEntry*
                        {
                            entry->hotkeys.clear();
                            return entry;
                        });
        }
        else
            load_items(item_floor);
    }
}

void UseItemMenu::repopulate_menu()
{
    deleteAll(items);
    populate_menu();
}

void UseItemMenu::toggle_display_all()
{
    display_all = !display_all;
    item_inv.clear();
    item_floor.clear();
    populate_list();
    repopulate_menu();
}

void UseItemMenu::toggle_inv_or_floor()
{
    is_inventory = !is_inventory;
    repopulate_menu();
}

bool UseItemMenu::process_key(int key)
{
    if (isadigit(key) || key == '*' || key == '\\' || key == ','
        || key == '-' && item_type_filter == OSEL_WIELD)
    {
        lastch = key;
        return false;
    }
    return Menu::process_key(key);
}

static string _weird_smell()
{
    return getMiscString("smell_name");
}

static string _weird_sound()
{
    return getMiscString("sound_name");
}

static MenuEntry* bag_item_mangle(MenuEntry* me)
{
    unique_ptr<InvEntry> ie(dynamic_cast<InvEntry*>(me));
    BagEntry* newme = new BagEntry(ie.get());
    return newme;
}

static int _use_an_item_beg(item_def& bag, item_def*& target, int item_type, const char* prompt)
{
    bool choice_made = false;
    InvMenu bag_menu(MF_SINGLESELECT | MF_ALLOW_FILTER);
    {
        bag_menu.set_title(new MenuEntry("Choose the item you want to take", MEL_TITLE));
    }
    bag_menu.set_tag("bag");
    bag_menu.menu_action = Menu::ACT_EXECUTE;
    bag_menu.set_type(menu_type::invlist);
    bag_menu.menu_action = InvMenu::ACT_EXECUTE;

    if (bag.props.exists(BAG_PROPS_KEY))
    {
        //����� �κ��丮
        {
            vector<const item_def*> tobeshown;

            int itemnum_in_bag = 0;
            CrawlVector& bagVector = bag.props[BAG_PROPS_KEY].get_vector();
            for (const auto& item : bagVector)
            {
                if (!(item.get_flags() & SFLAG_UNSET) && item.get_item().defined())
                {
                    if (item.get_item().base_type == item_type) {
                        tobeshown.push_back(&(item.get_item()));
                        itemnum_in_bag++;
                    }
                }
            }

            bag_menu.load_items(tobeshown, bag_item_mangle);
            bag_menu.set_title(prompt);
        }
    }
    else {
        bag_menu.set_title(prompt);
    }

    bag_menu.set_type(menu_type::invlist);

    vector<MenuEntry*> sel = bag_menu.show(true);

    item_def* tmp_tgt = nullptr;
    if (!sel.empty())
    {
        ASSERT(sel.size() == 1);
        choice_made = true;
        auto ie = dynamic_cast<InvEntry*>(sel[0]);
        tmp_tgt = const_cast<item_def*>(ie->item);
    }
    else {
        return false;
    }
    if (choice_made)
        target = tmp_tgt;

    ASSERT(!choice_made || target || item_type == OSEL_WIELD);
    return choice_made;
}



/**
 * Prompt use of an item from either player inventory or the floor.
 *
 * This function generates a menu containing type_expect items based on the
 * object_class_type to be acted on by another function. First it will list
 * items in inventory, then items on the floor. If the prompt is cancelled,
 * false is returned. If something is successfully choosen, then true is
 * returned, and at function exit the parameter target points to the object the
 * player chose or to nullptr if the player chose to wield bare hands (this is
 * only possible if item_type is OSEL_WIELD).
 *
 * @param target A pointer by reference to indicate the object selected.
 * @param item_type The object_class_type or OSEL_* of items to list.
 * @param oper The operation being done to the selected item.
 * @param prompt The prompt on the menu title
 * @param allowcancel If the user tries to cancel out of the prompt, run this
 *                    function. If it returns false, continue the prompt rather
 *                    than returning null.
 *
 * @return boolean true if something was chosen, false if the process failed and
 *                 no choice was made
 */
bool use_an_item(item_def *&target, int item_type, operation_types oper,
                      const char* prompt, function<bool ()> allowcancel)
{
    // First bail if there's nothing appropriate to choose in inv or on floor
    // (if choosing weapons, then bare hands are always a possibility)
    if (item_type != OSEL_WIELD && !any_items_of_type(item_type, -1, true))
    {
        mprf(MSGCH_PROMPT, "%s",
             no_selectables_message(item_type).c_str());
        return false;
    }

    bool choice_made = false;
    item_def *tmp_tgt = nullptr; // We'll change target only if the player
                                 // actually chooses

    // Init the menu
    UseItemMenu menu(item_type, prompt);

    while (true)
    {
        vector<MenuEntry*> sel = menu.show(true);
        int keyin = menu.getkey();

        // Handle inscribed item keys
        if (isadigit(keyin))
        {
        // This allows you to select stuff by inscription that is not on the
        // screen, but only if you couldn't by default use it for that operation
        // anyway. It's a bit weird, but it does save a '*' keypress for
        // bread-swingers.
            tmp_tgt = digit_inscription_to_item(keyin, oper);
            if (tmp_tgt)
                choice_made = true;
        }
        else if (keyin == '*')
        {
            menu.toggle_display_all();
            continue;
        }
        else if (keyin == ',')
        {
            if (Options.easy_floor_use && menu.item_floor.size() == 1)
            {
                choice_made = true;
                tmp_tgt = const_cast<item_def*>(menu.item_floor[0]);
            }
            else
            {
                menu.toggle_inv_or_floor();
                continue;
            }
        }
        else if (keyin == '\\')
        {
            check_item_knowledge();
            continue;
        }
        else if (keyin == '-' && menu.item_type_filter == OSEL_WIELD)
        {
            choice_made = true;
            tmp_tgt = nullptr;
        }
        else if (!sel.empty())
        {
            ASSERT(sel.size() == 1);

            choice_made = true;
            auto ie = dynamic_cast<InvEntry *>(sel[0]);
            tmp_tgt = const_cast<item_def*>(ie->item);
        }

        redraw_screen();
        // drink and scroll can used in the bag
        if (tmp_tgt && (item_type == OBJ_POTIONS || item_type == OBJ_SCROLLS))
        {
            if (tmp_tgt->base_type == OBJ_MISCELLANY && tmp_tgt->sub_type == MISC_BAG) {
                if (_use_an_item_beg(*tmp_tgt, target, item_type, prompt)) {
                    return true;
                }
                else {
                    choice_made = false;
                }
            }
        }
        // For weapons, armour, and jewellery this is handled in wield_weapon,
        // wear_armour, and _puton_item after selection
        if (item_type != OSEL_WIELD && item_type != OBJ_ARMOUR
            && item_type != OBJ_JEWELLERY && choice_made && tmp_tgt
            && !check_warning_inscriptions(*tmp_tgt, oper))
        {
            choice_made = false;
        }

        if (choice_made)
            break;
        else if (allowcancel())
        {
            prompt_failed(PROMPT_ABORT);
            break;
        }
        else
            continue;
    }
    if (choice_made)
        target = tmp_tgt;

    ASSERT(!choice_made || target || item_type == OSEL_WIELD);
    return choice_made;
}

static bool _safe_to_remove_or_wear(const item_def &item, bool remove,
                                    bool quiet = false);

// Rather messy - we've gathered all the can't-wield logic from wield_weapon()
// here.
bool can_wield(const item_def *weapon, bool say_reason,
               bool ignore_temporary_disability, bool unwield, bool only_known, bool second_weapon)
{
#define SAY(x) {if (say_reason) { x; }}
    bool isDualWeapon = (you.species == SP_TWO_HEADED_OGRE);
    ASSERT(isDualWeapon || !second_weapon);
    
    auto target = !second_weapon ? EQ_WEAPON : EQ_SECOND_WEAPON;

    if (you.melded[target] && unwield)
    {
        SAY(mpr("Your weapon is melded into your body!"));
        return false;
    }

    if (!ignore_temporary_disability && !form_can_wield(you.form))
    {
        SAY(mpr("You can't wield anything in your present form."));
        return false;
    }

    if (!ignore_temporary_disability
        && (second_weapon || you.weapon() && is_weapon(*you.weapon()) && you.weapon()->cursed())
        && (!second_weapon || (you.second_weapon() && is_weapon(*you.second_weapon()) && you.second_weapon()->cursed())))
    {
        SAY(mprf("You can't unwield your weapon%s!",
                 !unwield ? " to draw a new one" : ""));
        return false;
    }

    // If we don't have an actual weapon to check, return now.
    if (!weapon)
        return true;

    if (you.get_mutation_level(MUT_MISSING_HAND)
            && you.hands_reqd(*weapon) == HANDS_TWO)
    {
        SAY(mpr("You can't wield that without your missing limb."));
        return false;
    }

    for (int i = EQ_MIN_ARMOUR; i <= EQ_MAX_WORN; i++)
    {
        if (you.equip[i] != -1 && &you.inv[you.equip[i]] == weapon)
        {
            SAY(mpr("You are wearing that object!"));
            return false;
        }
    }

    if (!you.could_wield(*weapon, true, true, !say_reason))
        return false;

    // All non-weapons only need a shield check.
    if (weapon->base_type != OBJ_WEAPONS)
    {
        if (!ignore_temporary_disability && is_shield_incompatible(*weapon))
        {
            SAY(mpr("You can't wield that with a shield."));
            return false;
        }
        else
            return true;
    }

    bool id_brand = false;

    if (you.undead_or_demonic() && is_holy_item(*weapon)
        && (item_type_known(*weapon) || !only_known))
    {
        if (say_reason)
        {
            mpr("This weapon is holy and will not allow you to wield it.");
            id_brand = true;
        }
        else
            return false;
    }
    else if (you.species == SP_DJINNI
             && get_weapon_brand(*weapon) == SPWPN_ANTIMAGIC
             && (item_type_known(*weapon) || !only_known))
    {
        if (say_reason)
        {
            mpr("As you grasp it, you feel your magic disrupted. Quickly, you stop.");
            id_brand = true;
        }
        else
            return false;
    }
    if (id_brand)
    {
        auto wwpn = const_cast<item_def*>(weapon);
        if (!is_artefact(*weapon) && !is_blessed(*weapon)
            && !item_type_known(*weapon))
        {
            set_ident_flags(*wwpn, ISFLAG_KNOW_TYPE);
            if (in_inventory(*weapon))
                mprf_nocap("%s", weapon->name(DESC_INVENTORY_EQUIP).c_str());
        }
        else if (is_artefact(*weapon) && !item_type_known(*weapon))
            artefact_learn_prop(*wwpn, ARTP_BRAND);
        return false;
    }

    if (!ignore_temporary_disability && is_shield_incompatible(*weapon))
    {
        SAY(mpr("You can't wield that with a shield."));
        return false;
    }

    // We can wield this weapon. Phew!
    return true;

#undef SAY
}


/**
 * Helper function for wield_weapon, wear_armour, and puton_ring
 * @param  item    item on floor (where the player is standing)
 * @param  quiet   print message or not
 * @return boolean can the player move the item into their inventory, or are
 *                 they out of space?
 */
static bool _can_move_item_from_floor_to_inv(const item_def &item)
{
    if (inv_count() < ENDOFPACK)
        return true;
    if (!is_stackable_item(item))
    {
        mpr("You can't carry that many items.");
        return false;
    }
    for (int i = 0; i < ENDOFPACK; ++i)
    {
        if (items_stack(you.inv[i], item))
            return true;
    }
    mpr("You can't carry that many items.");
    return false;
}

/**
 * Helper function for wield_weapon, wear_armour, and puton_ring
 * @param  to_get item on floor (where the player is standing) to move into
                  inventory
 * @return int -1 if failure due to already full inventory; otherwise, index in
 *             you.inv where the item ended up
 */
static int _move_item_from_floor_to_inv(const item_def &to_get)
{
    map<int,int> tmp_l_p = you.last_pickup; // if we need to restore
    you.last_pickup.clear();

    if (!move_item_to_inv(to_get.index(), to_get.quantity, true))
    {
        mpr("You can't carry that many items.");
        you.last_pickup = tmp_l_p;
    }
    // Get the slot of the last thing picked up
    // TODO: this is a bit hacky---perhaps it's worth making a function like
    // move_item_to_inv that returns the slot the item moved into
    else
    {
        ASSERT(you.last_pickup.size() == 1); // Sanity check...
        return you.last_pickup.begin()->first;
    }
    return -1;
}

static char _weapon_slot_key(equipment_type slot)
{
    switch (slot)
    {
    case EQ_WEAPON:      return '<';
    case EQ_SECOND_WEAPON:     return '>';
    default:
        die("Invalid weapon slot");
    }
}

static vector<equipment_type> _current_weapon_types()
{
    vector<equipment_type> ret;
    if (you.species == SP_TWO_HEADED_OGRE)
    {
        ret.push_back(EQ_WEAPON);
        ret.push_back(EQ_SECOND_WEAPON);
    }
    else
    {
        ret.push_back(EQ_WEAPON);
    }
    return ret;
}

static equipment_type _choose_weapon_slot()
{
    ASSERT(you.species == SP_TWO_HEADED_OGRE);

    clear_messages();

    mprf(MSGCH_PROMPT,
        "Wield weapon on which %s? (<w>Esc</w> to cancel)", you.hand_name(false).c_str());

    const vector<equipment_type> slots = _current_weapon_types();
    for (auto eq : slots)
    {
        string msg = "<w>";
        const char key = _weapon_slot_key(eq);
        msg += key;
        if (key == '<')
            msg += '<';

        item_def* amulet = you.slot_item(eq, true);
        if (amulet)
            msg += "</w> or " + amulet->name(DESC_INVENTORY);
        else
            msg += "</w> - no weapon";

        if (eq == EQ_WEAPON)
            msg += " (first)";
        else if (eq == EQ_SECOND_WEAPON)
            msg += " (second)";
        mprf_nocap("%s", msg.c_str());
    }
    flush_prev_message();

    equipment_type eqslot = EQ_NONE;
    mouse_control mc(MOUSE_MODE_PROMPT);
    int c;
    do
    {
        c = getchm();
        for (auto eq : slots)
        {
            if (c == _weapon_slot_key(eq)
                || (you.slot_item(eq, true)
                    && c == index_to_letter(you.slot_item(eq, true)->link)))
            {
                eqslot = eq;
                c = ' ';
                break;
            }
        }
    } while (!key_is_escape(c) && c != ' ');

    clear_messages();

    return eqslot;
}



/**
 * Helper function for wield_weapon, wear_armour, and puton_ring
 * @param  item  item on floor (where the player is standing) or in inventory
 * @return ret index in you.inv where the item is (either because it was already
 *               there or just got moved there), or -1 if we tried and failed to
 *               move the item into inventory
 */
static int _get_item_slot_maybe_with_move(const item_def &item)
{
    int ret = item.pos == ITEM_IN_INVENTORY
        ? item.link : _move_item_from_floor_to_inv(item);
    return ret;
}
/**
 * @param auto_wield false if this was initiated by the wield weapon command (w)
 *      true otherwise (e.g. switching between ranged and melee with the
 *      auto_switch option)
 * @param slot Index into inventory of item to equip. Or one of following
 *     special values:
 *      - -1 (default): meaning no particular weapon. We'll either prompt for a
 *        choice of weapon (if auto_wield is false) or choose one by default.
 *      - SLOT_BARE_HANDS: equip nothing (unwielding current weapon, if any)
 * @param second_weapon It only uses in drop_item @ items.cc. You consider this
 *      only for auto_wield option. Hence, we can't make autoswap in second weapon.
 */
bool wield_weapon(bool auto_wield, int slot, bool show_weff_messages,
                  bool show_unwield_msg, bool show_wield_msg,
                  bool adjust_time_taken, bool second_weapon)
{
    bool isDualWeapon = (you.species == SP_TWO_HEADED_OGRE);
    ASSERT(auto_wield || !second_weapon);
    bool first_curse = !can_wield(nullptr, false, false, slot == SLOT_BARE_HANDS);
    bool second_curse = !can_wield(nullptr, false, false, slot == SLOT_BARE_HANDS, false, true);
    // Abort immediately if there's some condition that could prevent wielding
    // weapons.

    if (!isDualWeapon)
    {
        if (!can_wield(nullptr, true, false, slot == SLOT_BARE_HANDS))
            return false;
    }

    if (isDualWeapon && first_curse && second_curse)
    {   // just give a message 
        // "We don't care the case both weapons are cursed!"
        can_wield(nullptr, true, false, slot == SLOT_BARE_HANDS); 
        return false;
    }

    item_def *to_wield = &you.inv[0]; // default is 'a'
        // we'll set this to nullptr to indicate bare hands

    // If we swap the weapon
    if (auto_wield)
    {
        // and the target slot is not second weapon
        if (!second_weapon) {
            if (to_wield == you.weapon()
                || you.equip[EQ_WEAPON] == -1 && !item_is_wieldable(*to_wield))
            {
                to_wield = &you.inv[1];      // backup is 'b'
            }
        }
        else {
            if (to_wield == you.second_weapon()
                || you.equip[EQ_SECOND_WEAPON] == -1 && !item_is_wieldable(*to_wield))
            {
                to_wield = &you.inv[1];      // backup is 'b'
            }
        }

        if (slot != -1)         // allow external override
        {
            if (slot == SLOT_BARE_HANDS)
                to_wield = nullptr;
            else
                to_wield = &you.inv[slot];
        }
    }

    // If you find something to wield
    if (to_wield)
    {
    // Prompt if not using the auto swap command
        if (!auto_wield)
        {
            if (!use_an_item(to_wield, OSEL_WIELD, OPER_WIELD, "Wield "
                             "which item (- for none, * to show all)?"))
            {
                return false;
            }
    // We abort if trying to wield from the floor with full inventory. We could
    // try something more sophisticated, e.g., drop the currently held weapon,
    // but that's surely not always what's wanted, and the problem persists if
    // the player's hands are empty. For now, let's stick with simple behaviour
    // over trying to guess what the player would like.
            if (to_wield && to_wield->pos != ITEM_IN_INVENTORY
                && !_can_move_item_from_floor_to_inv(*to_wield))
            {
                return false;
            }
        }

    // If autowielding and the swap slot has a bad or invalid item in it, the
    // swap will be to bare hands.
        else if (!to_wield->defined() || !item_is_wieldable(*to_wield))
            to_wield = nullptr;
    }

    // Ignore already equipped
    if (to_wield && (to_wield == you.weapon() || to_wield == you.second_weapon()))
    {
        if (Options.equip_unequip)
            to_wield = nullptr;
        else
        {
            mpr("You are already wielding that!");
            return true;
        }
    }
    // Reset the warning counter.
    you.received_weapon_warning = false;

    auto notcursepenance = [](const item_def& wpn, bool quiet){
            // you cannot unwield cursed weapon!
            if (wpn.cursed())
            {
                if (quiet)
                    return false;

                mpr("you can't unwield your cursed weapon!");
                return false;
            }

            bool penance = false;
            // Can we safely unwield this item?
            if (needs_handle_warning(wpn, OPER_WIELD, penance))
            {
                if (quiet)
                    return false;
                string prompt =
                    "Really unwield " + wpn.name(DESC_INVENTORY) + "?";
                if (penance)
                    prompt += " This could place you under penance!";

                if (!yesno(prompt.c_str(), false, 'n'))
                {
                    canned_msg(MSG_OK);
                    return false;
                }
            }
            return true;
        };

    auto _wieldable = [](const item_def& wpn, int slot = EQ_WEAPON){
            bool isSecond = slot == EQ_SECOND_WEAPON;
            // Ensure wieldable
            if (!can_wield(&wpn, true, false, false, true, isSecond))
                return false;

            // Really ensure wieldable, even unknown brand
            if (!can_wield(&wpn, true, false, false, false, isSecond))
                return false;

            // At this point, we know it's possible to equip this item. However, there
            // might be reasons it's not advisable.
            if (!check_warning_inscriptions(wpn, OPER_WIELD, isSecond)
                || !_safe_to_remove_or_wear(wpn, false))
            {
                canned_msg(MSG_OK);
                return false;
            }
            return true;
        };

    // If there is no natural weapon to wield(and so, tried to unwield), choose weapon manually
    if (!to_wield)
    {
        const item_def* wpn = you.weapon();
        // If you wield second weapon,
        if (isDualWeapon && you.second_weapon()) {
            // ..and if you don't wield a weapon,
            if (!wpn) {
                wpn = you.second_weapon();
            }
            // ..and if you wield a weapn,
            else {
                // autoswap choose a slot by second_weapon option
                if (auto_wield) {
                    wpn = !second_weapon ? you.weapon() : you.second_weapon();
                }
                // if not you choose weapon from slot
                else {
                    // choose first slot or second weapon slot (by player in game)
                    auto choosed_wpn = _choose_weapon_slot();

                    if (choosed_wpn == EQ_NONE)
                    {
                        canned_msg(MSG_OK);
                        return false;
                    }
                    wpn = you.slot_item(choosed_wpn, true);
                }
            }
        }

        if (wpn)
        {
            if (!notcursepenance(*wpn, false))
                return false;

            // check if you'd get stat-zeroed
            if (!_safe_to_remove_or_wear(*wpn, true))
                return false;

            if (!unwield_item(show_weff_messages, 
                wpn == you.weapon() ? EQ_WEAPON : EQ_SECOND_WEAPON))
                return false;

            if (show_unwield_msg)
            {
#ifdef USE_SOUND
                parse_sound(WIELD_NOTHING_SOUND);
#endif
                canned_msg(MSG_EMPTY_HANDED_NOW);
            }

            // Switching to bare hands is extra fast.
            you.turn_is_over = true;
            if (adjust_time_taken)
            {
                you.time_taken *= 3;
                you.time_taken /= 10;
            }
        }
        else
            canned_msg(MSG_EMPTY_HANDED_ALREADY);

        return true;
    }

    // By now we're sure we're swapping to a real weapon, not bare hands

    item_def& new_wpn = *to_wield;

    // Switching to a launcher while berserk is likely a mistake.
    if (you.berserk() && is_range_weapon(new_wpn))
    {
        string prompt = "You can't shoot while berserk! Really wield " +
                        new_wpn.name(DESC_INVENTORY) + "?";
        if (!yesno(prompt.c_str(), false, 'n'))
        {
            canned_msg(MSG_OK);
            return false;
        }
    }

    if (!isDualWeapon)
    {
        if (!_wieldable(new_wpn))
            return false;
    
        if (unwield_item(show_weff_messages, EQ_WEAPON))
        {
            // Enable skills so they can be re-disabled later
            update_can_currently_train();
        }
        else
            return false;
    }
    else
    {
        auto wpn = [](int EQ){ return (EQ == EQ_WEAPON)?you.weapon() : you.second_weapon(); };
        auto twoweapons = [](){ return you.weapon() && you.second_weapon(); };
        auto noweapons = [](){ return !you.weapon() || !you.second_weapon(); };

        if ((you.weapon() && you.hands_reqd(*you.weapon()) == HANDS_TWO)
            || you.hands_reqd(new_wpn) == HANDS_TWO)
        {
            //if two handed weapon(maybe range weapon) you should unwield weapon all
            while (!twoweapons() && !noweapons())
            {
                auto choosed_wpn = !you.weapon() ? EQ_WEAPON : EQ_SECOND_WEAPON;
                if (!_wieldable(new_wpn, choosed_wpn == EQ_SECOND_WEAPON))
                    return false;
                if (unwield_item(show_weff_messages, choosed_wpn))
                {
                    // Enable skills so they can be re-disabled later
                    update_can_currently_train();
                }
                else
                    return false;
            }
        }
        else if(is_range_weapon(new_wpn))
        {
            // if you tried to wield range_weapon, fix it EQ_WEAPON;
            // Does it need?
            if (!_wieldable(new_wpn, false))
                return false;
            // TODO Choose and Unwield old weapon.
            if (unwield_item(show_weff_messages, EQ_WEAPON))
            {
                // Enable skills so they can be re-disabled later
                update_can_currently_train();
            }
            else
                return false;
        }
        else if(twoweapons())
        {
            auto choosed_wpn = EQ_WEAPON;
            if (!auto_wield)
                choosed_wpn = _choose_weapon_slot();
            else if (first_curse)
                choosed_wpn = EQ_SECOND_WEAPON;

            bool isSecond = false;

            if (choosed_wpn == EQ_NONE)
            {
                canned_msg(MSG_OK);
                return false;
            }

            if (!_wieldable(new_wpn, choosed_wpn == EQ_SECOND_WEAPON))
                return false;

            // TODO Choose and Unwield old weapon.
            if (unwield_item(show_weff_messages, choosed_wpn))
            {
                // Enable skills so they can be re-disabled later
                update_can_currently_train();
            }
            else
                return false;
        }
        else
        {
            if (!_wieldable(new_wpn, you.weapon() ? EQ_SECOND_WEAPON : EQ_WEAPON))
                return false;
        }
    } 

    const unsigned int old_talents = your_talents(false).size();

    // If it's on the ground, pick it up. Once it's picked up, there should be
    // no aborting, lest we introduce a way to instantly pick things up
    // NB we already made sure there was space for the item
    int item_slot = _get_item_slot_maybe_with_move(new_wpn);

    // At this point new_wpn is potentially not the right thing anymore (the
    // thing actually in the player's inventory), that is, in the case where the
    // player chose something from the floor. So use item_slot from here on.

    if(isDualWeapon) {
        if(!you.weapon()) {
            equip_item(EQ_WEAPON, item_slot, show_weff_messages);
        } else {
            equip_item(EQ_SECOND_WEAPON, item_slot, show_weff_messages);
        }
    } else{
        // Go ahead and wield the weapon.
        equip_item(EQ_WEAPON, item_slot, show_weff_messages);
    }

    if (show_wield_msg)
    {
#ifdef USE_SOUND
        parse_sound(WIELD_WEAPON_SOUND);
#endif
        mprf_nocap("%s", you.inv[item_slot].name(DESC_INVENTORY_EQUIP).c_str());
    }

    check_item_hint(you.inv[item_slot], old_talents);

    // Time calculations.
    if (adjust_time_taken)
        you.time_taken /= 2;

    you.wield_change  = true;
    you.m_quiver.on_weapon_changed();
    you.turn_is_over  = true;

    return true;
}

bool item_is_worn(int inv_slot)
{
    for (int i = EQ_MIN_ARMOUR; i <= EQ_MAX_WORN; ++i)
        if (inv_slot == you.equip[i])
            return true;

    return false;
}

/**
 * Prompt user for carried armour.
 *
 * @param mesg Title for the prompt
 * @param index[out] the inventory slot of the item chosen; not initialised
 *                   if a valid item was not chosen.
 * @param oper if equal to OPER_TAKEOFF, only show items relevant to the 'T'
 *             command.
 * @return whether a valid armour item was chosen.
 */
bool armour_prompt(const string & mesg, int *index, operation_types oper)
{
    ASSERT(index != nullptr);

    if (you.berserk())
        canned_msg(MSG_TOO_BERSERK);
    else
    {
        int selector = OBJ_ARMOUR;
        if (oper == OPER_TAKEOFF && !Options.equip_unequip)
            selector = OSEL_WORN_ARMOUR;
        int slot = prompt_invent_item(mesg.c_str(), menu_type::invlist,
                                      selector, oper);

        if (!prompt_failed(slot))
        {
            *index = slot;
            return true;
        }
    }

    return false;
}

/**
 * The number of turns it takes to put on or take off a given piece of armour.
 *
 * @param item      The armour in question.
 * @return          The number of turns it takes to don or doff the item.
 */
static int armour_equip_delay(const item_def &/*item*/)
{
    return 5;
}

// If you can't wear a bardings, why not? (If you can, return "".)
static string _cant_wear_barding_reason(const int sub_type, bool ignore_temporary)
{
    if (!you.wear_barding(sub_type))
        return "You can't wear that!";

    if (!ignore_temporary && player_is_shapechanged())
        return "You can wear that only in your normal form.";

    return "";
}

/**
 * Can you wear this item of armour currently?
 *
 * Ignores whether or not an item is equipped in its slot already.
 * If the item is Lear's hauberk, some of this comment may be incorrect.
 *
 * @param item The item. Only the base_type and sub_type really should get
 *             checked, since you_can_wear passes in a dummy item.
 * @param verbose Whether to print a message about your inability to wear item.
 * @param ignore_temporary Whether to take into account forms/fishtail/2handers.
 *                         Note that no matter what this is set to, all
 *                         mutations will be taken into account, except for
 *                         ones from Beastly Appendage, which are only checked
 *                         if this is false.
 */
bool can_wear_armour(const item_def &item, bool verbose, bool ignore_temporary)
{
    const object_class_type base_type = item.base_type;
    if (base_type != OBJ_ARMOUR)
    {
        if (verbose)
            mpr("You can't wear that.");

        return false;
    }

    const int sub_type = item.sub_type;
    const equipment_type slot = get_armour_slot(item);

    if (you.species == SP_FELID &&
        !(slot == EQ_CLOAK && sub_type == ARM_SCARF)) {
        if (verbose)
            mpr("You can't wear that.");

        return false;
    }

    if (you.species == SP_CRUSTACEAN)
    {
        if (verbose)
            mpr("You can't wear that.");
        return false;
    }

    if (you.species == SP_OCTOPODE && slot != EQ_HELMET && slot != EQ_SHIELD
        && !(slot == EQ_CLOAK && sub_type == ARM_SCARF))
    {
        if (verbose)
            mpr("You can't wear that!");
        return false;
    }

    if (you.species == SP_TWO_HEADED_OGRE && slot == EQ_SHIELD)
    {
        if (verbose)
            mpr("You can't wear that!");
        return false;
    }

    if (you.species == SP_HYDRA && (slot == EQ_SHIELD || slot == EQ_CLOAK))
        {
            if (verbose)
                mpr("You can't wear that!");
            return false;
        }

    if (species_is_draconian(you.species) && slot == EQ_BODY_ARMOUR)
    {
        if (sub_type == ARM_RING_MAIL || sub_type == ARM_SCALE_MAIL || sub_type == ARM_CHAIN_MAIL
        || sub_type == ARM_PLATE_ARMOUR || sub_type == ARM_CRYSTAL_PLATE_ARMOUR )
        {
            if (verbose)
            {
                mprf("Your wings%s won't fit in that.", you.has_mutation(MUT_BIG_WINGS)
                                                        ? "" : ", even vestigial as they are,");
            }
            return false;
        }
    }

    if (sub_type == ARM_NAGA_BARDING || sub_type == ARM_CENTAUR_BARDING)
    {
        const string reason = _cant_wear_barding_reason(sub_type, ignore_temporary);
        if (reason == "")
            return true;
        if (verbose)
            mpr(reason);
        return false;
    }

    if (you.get_mutation_level(MUT_MISSING_HAND) && is_shield(item))
    {
        if (verbose)
        {
            if (you.species == SP_OCTOPODE)
                mpr("You need the rest of your tentacles for walking.");
            else
                mprf("You'd need another %s to do that!", you.hand_name(false).c_str());
        }
        return false;
    }

    if (!ignore_temporary && you.weapon()
        && is_shield(item)
        && is_shield_incompatible(*you.weapon(), &item))
    {
        if (verbose)
        {
            if (you.species == SP_OCTOPODE)
                mpr("You need the rest of your tentacles for walking.");
            else
            {
                // Singular hand should have already been handled above.
                mprf("You'd need three %s to do that!",
                     you.hand_name(true).c_str());
            }
        }
        return false;
    }

    // Lear's hauberk covers also head, hands and legs.
    if (is_unrandom_artefact(item, UNRAND_LEAR))
    {
        if (!player_has_feet(!ignore_temporary))
        {
            if (verbose)
                mpr("You have no feet.");
            return false;
        }

        if (you.get_mutation_level(MUT_CLAWS, !ignore_temporary) >= 3 || 
            you.get_mutation_level(MUT_SICKLE_HANDS, !ignore_temporary) >= 1)
        {
            if (verbose)
            {
                mprf("The hauberk won't fit your %s.",
                     you.hand_name(true).c_str());
            }
            return false;
        }

        if (you.get_mutation_level(MUT_HORNS, !ignore_temporary) >= 3
            || you.get_mutation_level(MUT_ANTENNAE, !ignore_temporary) >= 3)
        {
            if (verbose)
                mpr("The hauberk won't fit your head.");
            return false;
        }

        if (you.species == SP_HYDRA)
        {
            if (verbose)
            {
                mprf("Your body is too deformed to wear it.");
            }
            return false;
        }

        if (!ignore_temporary)
        {
            for (int s = EQ_HELMET; s <= EQ_BOOTS; s++)
            {
                // No strange race can wear this.
                const string parts[] = { "head", you.hand_name(true),
                                         you.foot_name(true) };
                COMPILE_CHECK(ARRAYSZ(parts) == EQ_BOOTS - EQ_HELMET + 1);

                // Auto-disrobing would be nice.
                if (you.equip[s] != -1)
                {
                    if (verbose)
                    {
                        mprf("You'd need your %s free.",
                             parts[s - EQ_HELMET].c_str());
                    }
                    return false;
                }

                if (!get_form()->slot_available(s))
                {
                    if (verbose)
                    {
                        mprf("The hauberk won't fit your %s.",
                             parts[s - EQ_HELMET].c_str());
                    }
                    return false;
                }
            }
        }
    }
    else if (slot >= EQ_HELMET && slot <= EQ_BOOTS
             && !ignore_temporary
             && player_equip_unrand(UNRAND_LEAR))
    {
        // The explanation is iffy for loose headgear, especially crowns:
        // kings loved hooded hauberks, according to portraits.
        if (verbose)
            mpr("You can't wear this over your hauberk.");
        return false;
    }

    size_type player_size = you.body_size(PSIZE_TORSO, ignore_temporary);
    int bad_size = fit_armour_size(item, player_size);
#if TAG_MAJOR_VERSION == 34
    if (is_unrandom_artefact(item, UNRAND_TALOS))
    {
        // adjust bad_size for the oversized plate armour
        // negative means levels too small, positive means levels too large
        bad_size = SIZE_LARGE - player_size;
    }
#endif

    if (bad_size)
    {
        if (verbose)
        {
            mprf("This armour is too %s for you!",
                 (bad_size > 0) ? "big" : "small");
        }

        return false;
    }

    if (sub_type == ARM_GLOVES)
    {
        if (you.has_claws(false) == 3)
        {
            if (verbose)
            {
                mprf("You can't wear a glove with your huge claw%s!",
                     you.get_mutation_level(MUT_MISSING_HAND) ? "" : "s");
            }
            return false;
        }

        if (you.has_sickle_hands(false) >= 1)
        {
            if (verbose)
            {
                mprf("You can't wear a glove with your sickle-like hand%s!",
                    you.get_mutation_level(MUT_MISSING_HAND) ? "" : "s");
            }
            return false;
        }

        if (you.species == SP_HYDRA)
        {
            if (verbose)
            {
                mprf("You have no hands.");
            }
            return false;
        }
    }

    if (sub_type == ARM_BOOTS)
    {
        if (you.get_mutation_level(MUT_HOOVES, false) == 3)
        {
            if (verbose)
                mpr("You can't wear boots with hooves!");
            return false;
        }

        if (you.has_talons(false) == 3)
        {
            if (verbose)
                mpr("Boots don't fit your talons!");
            return false;
        }

        if (you.species == SP_NAGA
            || you.species == SP_PALENTONGA
            || you.species == SP_DJINNI
            || you.species == SP_MELIAI)
        {
            if (verbose)
                mpr("You have no legs!");
            return false;
        }

        if (you.species == SP_HYDRA)
        {
            if (verbose)
                mpr("You have too large legs!");
            return false;
        }

        if (!ignore_temporary && you.fishtail)
        {
            if (verbose)
                mpr("You don't currently have feet!");
            return false;
        }
    }

    if (slot == EQ_HELMET)
    {   
        // Horns 3 & Antennae 3 mutations disallow all headgear
        if (you.get_mutation_level(MUT_HORNS, false) == 3)
        {
            if (verbose)
                mpr("You can't wear any headgear with your large horns!");
            return false;
        }

        if (you.get_mutation_level(MUT_ANTENNAE, false) == 3)
        {
            if (verbose)
                mpr("You can't wear any headgear with your large antennae!");
            return false;
        }

        if (you.species == SP_HYDRA)
        {
            if (verbose)
                mpr("Your have too big and slippery heads to wear it.");
            return false;
        }

        if (!ignore_temporary
             && you.hunger_state < HS_FULL
             && is_unrandom_artefact(item, UNRAND_JAWS)
             && you.undead_state() == US_ALIVE
             && !you_foodless())
        {
            if (verbose)
                mpr("This item is vampiric, and you must be Full or above to equip it.");
            return false;
        }
        // Soft helmets (caps and wizard hats) always fit, otherwise.
        if (is_hard_helmet(item))
        {
            if (you.get_mutation_level(MUT_HORNS, false))
            {
                if (verbose)
                    mpr("You can't wear that with your horns!");
                return false;
            }

            if (you.get_mutation_level(MUT_BEAK, false))
            {
                if (verbose)
                    mpr("You can't wear that with your beak!");
                return false;
            }

            if (you.get_mutation_level(MUT_ANTENNAE, false))
            {
                if (verbose)
                    mpr("You can't wear that with your antennae!");
                return false;
            }

            if (species_is_draconian(you.species))
            {
                if (verbose)
                    mpr("You can't wear that with your reptilian head.");
                return false;
            }

            if (you.species == SP_OCTOPODE)
            {
                if (verbose)
                    mpr("You can't wear that!");
                return false;
            }
        }
    }

    // Can't just use Form::slot_available because of shroom caps.
    if (!ignore_temporary && !get_form()->can_wear_item(item))
    {
        if (verbose)
            mpr("You can't wear that in your present form.");
        return false;
    }

    return true;
}

static bool _can_takeoff_armour(int item);

// Like can_wear_armour, but also takes into account currently worn equipment.
// e.g. you may be able to *wear* that robe, but you can't equip it if your
// currently worn armour is cursed, or melded.
// precondition: item is not already worn
static bool _can_equip_armour(const item_def &item)
{
    const object_class_type base_type = item.base_type;
    if (base_type != OBJ_ARMOUR)
    {
        mpr("You can't wear that.");
        return false;
    }

    const int ER = -property(item, PARM_EVASION) / 10;
    if (you_worship(GOD_IMUS) && ER > 4)
    {
        mpr("Your fragile body can't wear heavy armour.");
        return false;
    }

    const equipment_type slot = get_armour_slot(item);
    const int equipped = you.equip[slot];
    if (equipped != -1 && !_can_takeoff_armour(equipped))
        return false;
    return can_wear_armour(item, true, false);
}

// Try to equip the armour in the given inventory slot (or, if slot is -1,
// prompt for a choice of item, then try to wear it).
bool wear_armour(int item)
{
    if (!form_can_wear())
    {
        mpr("You can't wear anything in your present form.");
        return false;
    }

    if (you.berserk())
    {
        canned_msg(MSG_TOO_BERSERK);
        return false;
    }

    item_def *to_wear = nullptr;

    if (item == -1)
    {
        if (!use_an_item(to_wear, OBJ_ARMOUR, OPER_WEAR,
                         "Wear which item (* to show all)?"))
        {
            return false;
        }
        // use_an_item on armour should never return true and leave to_wear
        // nullptr
        if (to_wear->pos != ITEM_IN_INVENTORY
            && !_can_move_item_from_floor_to_inv(*to_wear))
        {
            return false;
        }
    }
    else
        to_wear = &you.inv[item];

    // First, let's check for any conditions that would make it impossible to
    // equip the given item
    if (!to_wear->defined())
    {
        mpr("You don't have any such object.");
        return false;
    }

    if (to_wear == you.weapon())
    {
        mpr("You are wielding that object!");
        return false;
    }

    if (to_wear->pos == ITEM_IN_INVENTORY && item_is_worn(to_wear->link))
    {
        if (Options.equip_unequip)
            // TODO: huh? Why are we inverting the return value?
            return !takeoff_armour(to_wear->link);
        else
        {
            mpr("You're already wearing that object!");
            return false;
        }
    }

    if (!_can_equip_armour(*to_wear))
        return false;

    const equipment_type slot = get_armour_slot(*to_wear);


    if (you_worship(GOD_IMUS) && slot == EQ_SHIELD)
    {
        mpr("Your fragile body can't wear shield.");
        return false;
    }

    // At this point, we know it's possible to equip this item. However, there
    // might be reasons it's not advisable. Warn about any dangerous
    // inscriptions, giving the player an opportunity to bail out.
    if (!check_warning_inscriptions(*to_wear, OPER_WEAR))
    {
        canned_msg(MSG_OK);
        return false;
    }

    bool swapping = false;
    if ((slot == EQ_CLOAK
           || slot == EQ_HELMET
           || slot == EQ_GLOVES
           || slot == EQ_BOOTS
           || slot == EQ_SHIELD
           || slot == EQ_BODY_ARMOUR)
        && you.equip[slot] != -1)
    {
        if (!takeoff_armour(you.equip[slot]))
            return false;
        swapping = true;
    }

    you.turn_is_over = true;

    // TODO: It would be nice if we checked this before taking off the item
    // currently in the slot. But doing so is not quite trivial. Also applies
    // to jewellery.
    if (!_safe_to_remove_or_wear(*to_wear, false))
        return false;

    // If it's on the ground, pick it up. Once it's picked up, there should be
    // no aborting
    // NB we already made sure there was space for the item
    int item_slot = _get_item_slot_maybe_with_move(*to_wear);

    const int delay = armour_equip_delay(*to_wear);
    if (delay)
    {
        start_delay<ArmourOnDelay>(delay - (swapping ? 0 : 1),
                                   you.inv[item_slot]);
    }

    return true;
}

static bool _can_takeoff_armour(int item)
{
    item_def& invitem = you.inv[item];
    if (invitem.base_type != OBJ_ARMOUR)
    {
        mpr("You aren't wearing that!");
        return false;
    }

    if (you.berserk())
    {
        canned_msg(MSG_TOO_BERSERK);
        return false;
    }

    const equipment_type slot = get_armour_slot(invitem);
    if (item == you.equip[slot] && you.melded[slot])
    {
        mprf("%s is melded into your body!",
             invitem.name(DESC_YOUR).c_str());
        return false;
    }

    if (!item_is_worn(item))
    {
        mpr("You aren't wearing that object!");
        return false;
    }

    // If we get here, we're wearing the item.
    if (invitem.cursed())
    {
        mprf("%s is stuck to your body!", invitem.name(DESC_YOUR).c_str());
        return false;
    }
    return true;
}

// TODO: It would be nice if this were made consistent with wear_armour,
// wield_weapon, puton_ring, etc. in terms of taking a default value of -1,
// which has the effect of prompting for an item to take off.
bool takeoff_armour(int item)
{
    if (!_can_takeoff_armour(item))
        return false;

    item_def& invitem = you.inv[item];

    // It's possible to take this thing off, but if it would drop a stat
    // below 0, we should get confirmation.
    if (!_safe_to_remove_or_wear(invitem, true))
        return false;

    const equipment_type slot = get_armour_slot(invitem);

    // TODO: isn't this check covered above by the call to item_is_worn? The
    // only way to return false inside this switch would be if the player is
    // wearing a hat on their feet or something like that.
    switch (slot)
    {
    case EQ_BODY_ARMOUR:
    case EQ_SHIELD:
    case EQ_CLOAK:
    case EQ_HELMET:
    case EQ_GLOVES:
    case EQ_BOOTS:
        if (item != you.equip[slot])
        {
            mpr("You aren't wearing that!");
            return false;
        }
        break;

    default:
        break;
    }

    you.turn_is_over = true;

    const int delay = armour_equip_delay(invitem);
    start_delay<ArmourOffDelay>(delay - 1, invitem);

    return true;
}

// Returns a list of possible ring slots.
static vector<equipment_type> _current_ring_types()
{
    vector<equipment_type> ret;
    if (you.species == SP_OCTOPODE)
    {
        for (int i = 0; i < 8; ++i)
        {
            const equipment_type slot = (equipment_type)(EQ_RING_ONE + i);

            if (you.get_mutation_level(MUT_MISSING_HAND)
                && slot == EQ_RING_EIGHT)
            {
                continue;
            }

            if (get_form()->slot_available(slot))
                ret.push_back(slot);
        }
    }
    else if (you.species != SP_HYDRA)
    {
        if (you.get_mutation_level(MUT_MISSING_HAND) == 0)
            ret.push_back(EQ_LEFT_RING);
        ret.push_back(EQ_RIGHT_RING);
    }
    if (player_equip_unrand(UNRAND_FINGER_AMULET))
        ret.push_back(EQ_RING_AMULET);
    return ret;
}


static vector<equipment_type> _current_amulet_types()
{
    vector<equipment_type> ret;
    if (you.species == SP_TWO_HEADED_OGRE)
    {
        ret.push_back(EQ_AMULET_LEFT);
        ret.push_back(EQ_AMULET_RIGHT);
    }
    else if (you.species == SP_HYDRA)
    {
        you.head_grow(0, false); // Just for calling _handle_amulet_loss().
        for (int eq = EQ_AMULET_ONE; eq <= EQ_AMULET_NINE; eq++)
        {
            if (you_can_wear((equipment_type) eq))
            {
                ret.push_back((equipment_type)eq);
            }
            
        }
    }
    else
    {
        ret.push_back(EQ_AMULET);
    }
    return ret;
}

static vector<equipment_type> _current_jewellery_types()
{
    vector<equipment_type> ret = _current_ring_types();
    vector<equipment_type> amulet_ret = _current_amulet_types();
    ret.insert(ret.begin(), amulet_ret.begin(), amulet_ret.end());
    return ret;
}

static char _ring_slot_key(equipment_type slot)
{
    switch (slot)
    {
    case EQ_LEFT_RING:      return '<';
    case EQ_RIGHT_RING:     return '>';
    case EQ_RING_AMULET:    return '^';
    case EQ_RING_ONE:       return '1';
    case EQ_RING_TWO:       return '2';
    case EQ_RING_THREE:     return '3';
    case EQ_RING_FOUR:      return '4';
    case EQ_RING_FIVE:      return '5';
    case EQ_RING_SIX:       return '6';
    case EQ_RING_SEVEN:     return '7';
    case EQ_RING_EIGHT:     return '8';
    default:
        die("Invalid ring slot");
    }
}

static char _amulet_slot_key(equipment_type slot)
{
    switch (slot)
    {
    case EQ_AMULET_LEFT:      return '<';
    case EQ_AMULET_RIGHT:     return '>';
    case EQ_AMULET_ONE:       return '1';
    case EQ_AMULET_TWO:       return '2';
    case EQ_AMULET_THREE:     return '3';
    case EQ_AMULET_FOUR:      return '4';
    case EQ_AMULET_FIVE:      return '5';
    case EQ_AMULET_SIX:       return '6';
    case EQ_AMULET_SEVEN:     return '7';
    case EQ_AMULET_EIGHT:     return '8';
    case EQ_AMULET_NINE:      return '9';
    default:
        die("Invalid amulet slot");
    }
}

static int _prompt_jewellry_to_remove(bool is_ring)
{
    const vector<equipment_type> jew_types = is_ring?_current_ring_types(): _current_amulet_types();
    vector<char> slot_chars;
    vector<item_def*> jews;
    for (auto eq : jew_types)
    {
        jews.push_back(you.slot_item(eq, true));
        ASSERT(jews.back());
        slot_chars.push_back(index_to_letter(jews.back()->link));
    }

    if (slot_chars.size() + 2 > msgwin_lines() || ui::has_layout())
    {
        // force a menu rather than a more().
        return EQ_NONE;
    }

    clear_messages();

    mprf(MSGCH_PROMPT,
         "You're wearing all the %s you can. Remove which one?",
         is_ring ? "rings" : "amulets");
    mprf(MSGCH_PROMPT, "(<w>?</w> for menu, <w>Esc</w> to cancel)");

    // FIXME: Needs TOUCH_UI version

    for (size_t i = 0; i < jews.size(); i++)
    {
        string m = "<w>";
        const char key = is_ring ? _ring_slot_key(jew_types[i]) : _amulet_slot_key(jew_types[i]);
        m += key;
        if (key == '<')
            m += '<';

        m += "</w> or " + jews[i]->name(DESC_INVENTORY);
        mprf_nocap("%s", m.c_str());
    }
    flush_prev_message();

    // Deactivate choice from tile inventory.
    // FIXME: We need to be able to get the choice (item letter)n
    //        *without* the choice taking action by itself!
    int eqslot = EQ_NONE;

    mouse_control mc(MOUSE_MODE_PROMPT);
    int c;
    do
    {
        c = getchm();
        for (size_t i = 0; i < slot_chars.size(); i++)
        {
            if (c == slot_chars[i]
                || c == (is_ring ? _ring_slot_key(jew_types[i]) : _amulet_slot_key(jew_types[i])))
            {
                eqslot = jew_types[i];
                c = ' ';
                break;
            }
        }
    } while (!key_is_escape(c) && c != ' ' && c != '?');

    clear_messages();

    if (c == '?')
        return EQ_NONE;
    else if (key_is_escape(c) || eqslot == EQ_NONE)
        return -2;

    return you.equip[eqslot];
}

// Checks whether a to-be-worn or to-be-removed item affects
// character stats and whether wearing/removing it could be fatal.
// If so, warns the player, or just returns false if quiet is true.
static bool _safe_to_remove_or_wear(const item_def &item, bool remove, bool quiet)
{
    if (remove && !safe_to_remove(item, quiet))
        return false;

    int prop_str = 0;
    int prop_dex = 0;
    int prop_int = 0;
    if (item.base_type == OBJ_JEWELLERY
        && item_ident(item, ISFLAG_KNOW_PLUSES))
    {
        switch (item.sub_type)
        {
        case RING_STRENGTH:
            if (item.plus != 0)
                prop_str = item.plus;
            break;
        case RING_DEXTERITY:
            if (item.plus != 0)
                prop_dex = item.plus;
            break;
        case RING_INTELLIGENCE:
            if (item.plus != 0)
                prop_int = item.plus;
            break;
        default:
            break;
        }
    }
    else if (item.base_type == OBJ_ARMOUR && item_type_known(item))
    {
        switch (item.brand)
        {
        case SPARM_STRENGTH:
            prop_str = 3;
            break;
        case SPARM_INTELLIGENCE:
            prop_int = 3;
            break;
        case SPARM_DEXTERITY:
            prop_dex = 3;
            break;
        default:
            break;
        }
    }

    if (is_artefact(item))
    {
        prop_str += artefact_known_property(item, ARTP_STRENGTH);
        prop_int += artefact_known_property(item, ARTP_INTELLIGENCE);
        prop_dex += artefact_known_property(item, ARTP_DEXTERITY);
    }

    if (!remove)
    {
        prop_str *= -1;
        prop_int *= -1;
        prop_dex *= -1;
    }
    stat_type red_stat = NUM_STATS;
    if (prop_str >= you.strength() && you.strength() > 0)
        red_stat = STAT_STR;
    else if (prop_int >= you.intel() && you.intel() > 0)
        red_stat = STAT_INT;
    else if (prop_dex >= you.dex() && you.dex() > 0)
        red_stat = STAT_DEX;

    if (red_stat == NUM_STATS)
        return true;

    if (quiet)
        return false;

    string verb = "";
    if (remove)
    {
        if (item.base_type == OBJ_WEAPONS)
            verb = "Unwield";
        else
            verb = "Remov"; // -ing, not a typo
    }
    else
    {
        if (item.base_type == OBJ_WEAPONS)
            verb = "Wield";
        else
            verb = "Wear";
    }

    string prompt = make_stringf("%sing this item will reduce your %s to zero "
                                 "or below. Continue?", verb.c_str(),
                                 stat_desc(red_stat, SD_NAME));
    if (!yesno(prompt.c_str(), true, 'n', true, false))
    {
        canned_msg(MSG_OK);
        return false;
    }
    return true;
}

// Checks whether removing an item would cause flight to end and the
// player to fall to their death.
bool safe_to_remove(const item_def &item, bool quiet)
{
    item_info inf = get_item_info(item);

    const bool grants_flight =
         inf.is_type(OBJ_JEWELLERY, RING_FLIGHT)
         || inf.base_type == OBJ_ARMOUR && inf.brand == SPARM_FLYING
         || is_artefact(inf)
            && artefact_known_property(inf, ARTP_FLY);

    // assumes item can't grant flight twice
    const bool removing_ends_flight = you.airborne()
          && !you.racial_permanent_flight()
          && !you.attribute[ATTR_FLIGHT_UNCANCELLABLE]
          && (you.evokable_flight() == 1);

    const dungeon_feature_type feat = grd(you.pos());

    if (grants_flight && removing_ends_flight
        && is_feat_dangerous(feat, false, true))
    {
        if (!quiet)
            mpr("Losing flight right now would be fatal!");
        return false;
    }

    return true;
}

// Assumptions:
// item is an item in inventory or on the floor where the player is standing
// EQ_LEFT_RING and EQ_RIGHT_RING are both occupied, and item is not
// in one of those slots.
//
static bool _swap_jewellrys(const item_def& to_puton, bool is_ring)
{
    vector<equipment_type> jew_types = is_ring ? _current_ring_types() : _current_amulet_types();
    const int num_jews = jew_types.size();
    int unwanted = 0;
    int last_inscribed = 0;
    int cursed = 0;
    int inscribed = 0;
    int melded = 0; // Both melded rings and unavailable slots.
    int available = 0;
    bool all_same = true;
    item_def* first_jew = nullptr;
    for (auto eq : jew_types)
    {
        item_def* jewellry = you.slot_item(eq, true);
        if (!you_can_wear(eq, true) || you.melded[eq])
            melded++;
        else if (jewellry != nullptr)
        {
            if (first_jew == nullptr)
                first_jew = jewellry;
            else if (all_same)
            {
                if (jewellry->sub_type != first_jew->sub_type
                    || jewellry->plus  != first_jew->plus
                    || is_artefact(*jewellry) || is_artefact(*first_jew))
                {
                    all_same = false;
                }
            }

            if (jewellry->cursed())
                cursed++;
            else if (strstr(jewellry->inscription.c_str(), "=R"))
            {
                inscribed++;
                last_inscribed = you.equip[eq];
            }
            else
            {
                available++;
                unwanted = you.equip[eq];
            }
        }
    }

    // If the only swappable rings are inscribed =R, go ahead and use them.
    if (available == 0 && inscribed > 0)
    {
        available += inscribed;
        unwanted = last_inscribed;
    }

    // We can't put a ring on, because we're wearing all cursed ones.
    if (melded == num_jews)
    {
        // Shouldn't happen, because hogs and bats can't put on jewellery at
        // all and thus won't get this far.
        mpr("You can't wear that in your present form.");
        return false;
    }
    else if (available == 0)
    {
        mprf("You're already wearing %s cursed %s%s!%s",
             number_in_words(cursed).c_str(),
             is_ring ? "ring" : "amulet",
             (cursed == 1 ? "" : "s"),
             (cursed > 2 ? " Isn't that enough for you?" : ""));
        return false;
    }
    // The simple case - only one available jewellry.
    // If the jewellery_prompt option is true, always allow choosing the
    // jewellry slot (even if we still have empty slots).
    else if (available == 1 && !Options.jewellery_prompt)
    {
        if (!remove_ring(unwanted, false))
            return false;
    }
    // We can't put a ring on without swapping - because we found
    // multiple available rings.
    else
    {
        // Don't prompt if all the rings are the same.
        if (!all_same || Options.jewellery_prompt)
            unwanted = _prompt_jewellry_to_remove(is_ring);

        if (unwanted == EQ_NONE)
        {
            // do this here rather than in remove_ring so that the custom
            // message is visible.
            if (is_ring) {
                unwanted = prompt_invent_item(
                    "You're wearing all the rings you can. Remove which one?",
                    menu_type::invlist, OSEL_UNCURSED_WORN_RINGS, OPER_REMOVE,
                    invprompt_flag::no_warning | invprompt_flag::hide_known);
            }
            else {
                unwanted = prompt_invent_item(
                    "You're wearing all the amulets you can. Remove which one?",
                    menu_type::invlist, OSEL_UNCURSED_WORN_AMULETS, OPER_REMOVE,
                    invprompt_flag::no_warning | invprompt_flag::hide_known);
            }
        }

        // Cancelled:
        if (unwanted < 0)
        {
            canned_msg(MSG_OK);
            return false;
        }

        if (!remove_ring(unwanted, false))
            return false;
    }

    // Put on the new ring.
    start_delay<JewelleryOnDelay>(1, to_puton);

    return true;
}

static equipment_type _choose_ring_slot()
{
    clear_messages();

    mprf(MSGCH_PROMPT,
         "Put ring on which %s? (<w>Esc</w> to cancel)", you.hand_name(false).c_str());

    const vector<equipment_type> slots = _current_ring_types();
    for (auto eq : slots)
    {
        string msg = "<w>";
        const char key = _ring_slot_key(eq);
        msg += key;
        if (key == '<')
            msg += '<';

        item_def* ring = you.slot_item(eq, true);
        if (ring)
            msg += "</w> or " + ring->name(DESC_INVENTORY);
        else
            msg += "</w> - no ring";

        if (eq == EQ_LEFT_RING)
            msg += " (left)";
        else if (eq == EQ_RIGHT_RING)
            msg += " (right)";
        else if (eq == EQ_RING_AMULET)
            msg += " (amulet)";
        mprf_nocap("%s", msg.c_str());
    }
    flush_prev_message();

    equipment_type eqslot = EQ_NONE;
    mouse_control mc(MOUSE_MODE_PROMPT);
    int c;
    do
    {
        c = getchm();
        for (auto eq : slots)
        {
            if (c == _ring_slot_key(eq)
                || (you.slot_item(eq, true)
                    && c == index_to_letter(you.slot_item(eq, true)->link)))
            {
                eqslot = eq;
                c = ' ';
                break;
            }
        }
    } while (!key_is_escape(c) && c != ' ');

    clear_messages();

    return eqslot;
}

static equipment_type _choose_amulet_slot()
{
    ASSERT(you.species == SP_TWO_HEADED_OGRE || you.species == SP_HYDRA);

    clear_messages();

    mprf(MSGCH_PROMPT,
        "Put amulet on which %s? (<w>Esc</w> to cancel)",
        you.species == SP_TWO_HEADED_OGRE ? you.hand_name(false).c_str()
                                            :"neck");

    const vector<equipment_type> slots = _current_amulet_types();
    for (auto eq : slots)
    {
        string msg = "<w>";
        const char key = _amulet_slot_key(eq);
        msg += key;
        if (key == '<')
            msg += '<';

        item_def* amulet = you.slot_item(eq, true);
        if (amulet)
            msg += "</w> or " + amulet->name(DESC_INVENTORY);
        else
            msg += "</w> - no amulet";

        if (eq == EQ_AMULET_LEFT)
            msg += " (left)";
        else if (eq == EQ_AMULET_RIGHT)
            msg += " (right)";
        mprf_nocap("%s", msg.c_str());
    }
    flush_prev_message();

    equipment_type eqslot = EQ_NONE;
    mouse_control mc(MOUSE_MODE_PROMPT);
    int c;
    do
    {
        c = getchm();
        for (auto eq : slots)
        {
            if (c == _amulet_slot_key(eq)
                || (you.slot_item(eq, true)
                    && c == index_to_letter(you.slot_item(eq, true)->link)))
            {
                eqslot = eq;
                c = ' ';
                break;
            }
        }
    } while (!key_is_escape(c) && c != ' ');

    clear_messages();

    return eqslot;
}

// Is it possible to put on the given item in a jewellery slot?
// Preconditions:
// - item is not already equipped in a jewellery slot
static bool _can_puton_jewellery(const item_def &item)
{
    // TODO: between this function, _puton_item, _swap_jewellerys, and remove_ring,
    // there's a bit of duplicated work, and sep. of concerns not clear
    if (&item == you.weapon())
    {
        mpr("You are wielding that object.");
        return false;
    }

    if (item.base_type != OBJ_JEWELLERY)
    {
        mpr("You can only put on jewellery.");
        return false;
    }
    
    const bool is_amulet = jewellery_is_amulet(item);

    if (!is_amulet && !player_equip_unrand(UNRAND_FINGER_AMULET)
        && you.species == SP_HYDRA)
    {
        mpr("You have no fingers and your toes are too big to put on.");
        return false;
    }

    if (is_amulet && !you_can_wear(EQ_AMULETS, true)
        || !is_amulet && !you_can_wear(EQ_RINGS, true))
    {
        mpr("You can't wear that in your present form.");
        return false;
    }

    // Make sure there's at least one slot where we could equip this item
    if (is_amulet)
    {
        if (you.species == SP_TWO_HEADED_OGRE || you.species == SP_HYDRA) {
            const vector<equipment_type> slots = _current_amulet_types();
            int melded = 0;
            int cursed = 0;
            for (auto eq : slots)
            {
                if (!you_can_wear(eq, true) || you.melded[eq])
                {
                    melded++;
                    continue;
                }
                int existing = you.equip[eq];
                if (existing != -1 && you.inv[existing].cursed())
                    cursed++;
                else
                    // We found an available slot. We're done.
                    return true;
            }
            if (melded == (int)slots.size())
                mpr("You can't wear that in your present form.");
            else
                mprf("You're already wearing %s cursed amulet%s!%s",
                    number_in_words(cursed).c_str(),
                    (cursed == 1 ? "" : "s"),
                    (cursed > 2 ? " Isn't that enough for you?" : ""));
            return false;
        }
        else {
            int existing = you.equip[EQ_AMULET];
            if (existing != -1 && you.inv[existing].cursed())
            {
                mprf("%s is stuck to you!",
                    you.inv[existing].name(DESC_YOUR).c_str());
                return false;
            }
            else
                return true;
        }
    }
    // The ring case is a bit more complicated
    else
    {
        const vector<equipment_type> slots = _current_ring_types();
        int melded = 0;
        int cursed = 0;
        for (auto eq : slots)
        {
            if (!you_can_wear(eq, true) || you.melded[eq])
            {
                melded++;
                continue;
            }
            int existing = you.equip[eq];
            if (existing != -1 && you.inv[existing].cursed())
                cursed++;
            else
                // We found an available slot. We're done.
                return true;
        }
        // If we got this far, there are no available slots.
        if (melded == (int)slots.size())
            mpr("You can't wear that in your present form.");
        else
            mprf("You're already wearing %s cursed ring%s!%s",
                 number_in_words(cursed).c_str(),
                 (cursed == 1 ? "" : "s"),
                 (cursed > 2 ? " Isn't that enough for you?" : ""));
        return false;
    }
}

// Put on a particular ring or amulet
static bool _puton_item(const item_def& item, bool prompt_slot,
    bool check_for_inscriptions)
{
    vector<equipment_type> current_jewellery = _current_ring_types();
    vector<equipment_type> current_amulet = _current_amulet_types();
    current_jewellery.insert(current_jewellery.end(), current_amulet.begin(), current_amulet.end());

    for (auto eq : current_jewellery)
    {
        if (&item == you.slot_item(eq, true))
        {
            // "Putting on" an equipped item means taking it off.
            if (Options.equip_unequip)
                // TODO: why invert the return value here? failing to remove
                // a ring is equivalent to successfully putting one on?
                return !remove_ring(item.link);
            else
            {
                mpr("You're already wearing that object!");
                return false;
            }
        }
    }

    if (!_can_puton_jewellery(item))
        return false;

    // It looks to be possible to equip this item. Before going any further,
    // we should prompt the user with any warnings that come with trying to
    // put it on, except when they have already been prompted with them
    // from switching rings.
    if (check_for_inscriptions && !check_warning_inscriptions(item, OPER_PUTON))
    {
        canned_msg(MSG_OK);
        return false;
    }

    const bool is_amulet = jewellery_is_amulet(item);

    const vector<equipment_type> ring_types = _current_ring_types();
    const vector<equipment_type> amulet_types = _current_amulet_types();

    if (!is_amulet)     // i.e. it's a ring
    {
        // Check whether there are any unused ring slots
        bool need_swap = true;
        for (auto eq : ring_types)
        {
            if (!you.slot_item(eq, true))
            {
                need_swap = false;
                break;
            }
        }

        // No unused ring slots. Swap out a worn ring for the new one.
        if (need_swap)
            return _swap_jewellrys(item, true);
    }
    else
    {
        if (you.species == SP_TWO_HEADED_OGRE || you.species == SP_HYDRA) {
            // Check whether there are any unused ring slots
            bool need_swap = true;
            for (auto eq : amulet_types)
            {
                if (!you.slot_item(eq, true))
                {
                    need_swap = false;
                    break;
                }
            }

            // No unused ring slots. Swap out a worn ring for the new one.
            if (need_swap)
                return _swap_jewellrys(item, false);
        }
        else if (you.slot_item(EQ_AMULET, true))
        {
            // Remove the previous one.
            if (!remove_ring(you.equip[EQ_AMULET], true))
                return false;

            // Check for stat loss.
            if (!_safe_to_remove_or_wear(item, false))
                return false;

            // Put on the new amulet.
            start_delay<JewelleryOnDelay>(1, item);

            // Assume it's going to succeed.
            return true;
        }
    }
    // At this point, we know there's an empty slot for the ring/amulet we're
    // trying to equip.

    // Check for stat loss.
    if (!_safe_to_remove_or_wear(item, false))
        return false;

    equipment_type hand_used = EQ_NONE;

    if (is_amulet) {
        if (you.species == SP_TWO_HEADED_OGRE || you.species == SP_HYDRA) {
            if (prompt_slot)
            {
                // Prompt for a slot, even if we have empty amulet slots.
                hand_used = _choose_amulet_slot();

                if (hand_used == EQ_NONE)
                {
                    canned_msg(MSG_OK);
                    return false;
                }
                // Allow swapping out a ring.
                else if (you.slot_item(hand_used, true))
                {
                    if (!remove_ring(you.equip[hand_used], false))
                        return false;

                    start_delay<JewelleryOnDelay>(1, item);
                    return true;
                }
            }
            else
            {
                for (auto eq : amulet_types)
                {
                    if (!you.slot_item(eq, true))
                    {
                        hand_used = eq;
                        break;
                    }
                }
            }
        }
        else {
            hand_used = EQ_AMULET;
        }
    }
    else if (prompt_slot)
    {
        // Prompt for a slot, even if we have empty ring slots.
        hand_used = _choose_ring_slot();

        if (hand_used == EQ_NONE)
        {
            canned_msg(MSG_OK);
            return false;
        }
        // Allow swapping out a ring.
        else if (you.slot_item(hand_used, true))
        {
            if (!remove_ring(you.equip[hand_used], false))
                return false;

            start_delay<JewelleryOnDelay>(1, item);
            return true;
        }
    }
    else
    {
        for (auto eq : ring_types)
        {
            if (!you.slot_item(eq, true))
            {
                hand_used = eq;
                break;
            }
        }
    }

    const unsigned int old_talents = your_talents(false).size();

    // Actually equip the item.
    int item_slot = _get_item_slot_maybe_with_move(item);
    equip_item(hand_used, item_slot);

    check_item_hint(you.inv[item_slot], old_talents);
#ifdef USE_TILE_LOCAL
    if (your_talents(false).size() != old_talents)
    {
        tiles.layout_statcol();
        redraw_screen();
    }
#endif

    // Putting on jewellery is fast.
    you.time_taken /= 2;
    you.turn_is_over = true;

    return true;
}

// Put on a ring or amulet. (Most of the work is in _puton_item.)
bool puton_ring(const item_def &to_puton, bool allow_prompt,
                bool check_for_inscriptions)
{
    if (you.berserk())
    {
        canned_msg(MSG_TOO_BERSERK);
        return false;
    }
    if (!to_puton.defined())
    {
        mpr("You don't have any such object.");
        return false;
    }
    if (to_puton.pos != ITEM_IN_INVENTORY
        && !_can_move_item_from_floor_to_inv(to_puton))
    {
        return false;
    }

    bool prompt = allow_prompt ? Options.jewellery_prompt : false;
    return _puton_item(to_puton, prompt, check_for_inscriptions);
}

// Wraps version of puton_ring with item_def param. If slot is -1, prompt for
// which item to put on; otherwise, pass on the item in inventory slot
bool puton_ring(int slot, bool allow_prompt, bool check_for_inscriptions)
{
    item_def *to_puton_ptr = nullptr;
    if (slot == -1)
    {
        if (!use_an_item(to_puton_ptr, OBJ_JEWELLERY, OPER_PUTON,
                        "Put on which piece of jewellery (* to show all)?"))
        {
            return false;
        }
    }
    else
        to_puton_ptr = &you.inv[slot];

    return puton_ring(*to_puton_ptr, allow_prompt, check_for_inscriptions);
}

// Remove the amulet/ring at given inventory slot (or, if slot is -1, prompt
// for which piece of jewellery to remove)
bool remove_ring(int slot, bool announce)
{
    equipment_type hand_used = EQ_NONE;
    int ring_wear_2;
    bool has_jewellery = false;
    bool has_melded = false;
    const vector<equipment_type> jewellery_slots = _current_jewellery_types();

    for (auto eq : jewellery_slots)
    {
        if (you.slot_item(eq))
        {
            if (has_jewellery || Options.jewellery_prompt)
            {
                // At least one other piece, which means we'll have to ask
                hand_used = EQ_NONE;
            }
            else
                hand_used = eq;

            has_jewellery = true;
        }
        else if (you.melded[eq])
            has_melded = true;
    }

    if (!has_jewellery)
    {
        if (has_melded)
            mpr("You aren't wearing any unmelded rings or amulets.");
        else
            mpr("You aren't wearing any rings or amulets.");

        return false;
    }

    if (you.berserk())
    {
        canned_msg(MSG_TOO_BERSERK);
        return false;
    }

    // If more than one equipment slot had jewellery, we need to figure out
    // which one to remove from.
    if (hand_used == EQ_NONE)
    {
        const int equipn =
            (slot == -1)? prompt_invent_item("Remove which piece of jewellery?",
                                             menu_type::invlist,
                                             OBJ_JEWELLERY,
                                             OPER_REMOVE,
                                             invprompt_flag::no_warning
                                                | invprompt_flag::hide_known)
                        : slot;

        if (prompt_failed(equipn))
            return false;

        hand_used = item_equip_slot(you.inv[equipn]);
        if (hand_used == EQ_NONE)
        {
            mpr("You aren't wearing that.");
            return false;
        }
        else if (you.inv[equipn].base_type != OBJ_JEWELLERY)
        {
            mpr("That isn't a piece of jewellery.");
            return false;
        }
    }

    if (you.equip[hand_used] == -1)
    {
        mpr("I don't think you really meant that.");
        return false;
    }
    else if (you.melded[hand_used])
    {
        mpr("You can't take that off while it's melded.");
        return false;
    }
    else if (is_unrandom_artefact(*you.slot_item(hand_used, true), UNRAND_FINGER_AMULET)
            && you.equip[EQ_RING_AMULET] != -1)
    {
        // This can be removed in the future if more ring amulets are added.
        ASSERT(player_equip_unrand(UNRAND_FINGER_AMULET));

        mpr("The amulet cannot be taken off without first removing the ring!");
        return false;
    }

    if (!check_warning_inscriptions(you.inv[you.equip[hand_used]],
                                    OPER_REMOVE))
    {
        canned_msg(MSG_OK);
        return false;
    }

    if (you.inv[you.equip[hand_used]].cursed())
    {
        if (announce)
        {
            mprf("%s is stuck to you!",
                 you.inv[you.equip[hand_used]].name(DESC_YOUR).c_str());
        }
        else
            mpr("It's stuck to you!");

        set_ident_flags(you.inv[you.equip[hand_used]], ISFLAG_KNOW_CURSE);
        return false;
    }

    ring_wear_2 = you.equip[hand_used];

    // Remove the ring.
    if (!_safe_to_remove_or_wear(you.inv[ring_wear_2], true))
        return false;

#ifdef USE_SOUND
    parse_sound(REMOVE_JEWELLERY_SOUND);
#endif
    mprf("You remove %s.", you.inv[ring_wear_2].name(DESC_YOUR).c_str());
#ifdef USE_TILE_LOCAL
    const unsigned int old_talents = your_talents(false).size();
#endif
    unequip_item(hand_used);
#ifdef USE_TILE_LOCAL
    if (your_talents(false).size() != old_talents)
    {
        tiles.layout_statcol();
        redraw_screen();
    }
#endif

    you.time_taken /= 2;
    you.turn_is_over = true;

    return true;
}

void prompt_inscribe_item()
{
    if (inv_count() < 1)
    {
        mpr("You don't have anything to inscribe.");
        return;
    }

    int item_slot = prompt_invent_item("Inscribe which item?",
                                       menu_type::invlist, OSEL_ANY);

    if (prompt_failed(item_slot))
        return;

    inscribe_item(you.inv[item_slot]);
}


static bool _drink_fountain()
{
    const dungeon_feature_type feat = grd(you.pos());

    ASSERT(feat >= DNGN_FOUNTAIN_BLUE && feat <= DNGN_DRY_FOUNTAIN);

    if (you.berserk())
    {
        canned_msg(MSG_TOO_BERSERK);
        return true;
    }

    potion_type fountain_effect = NUM_POTIONS;
    if (feat == DNGN_DRY_FOUNTAIN)
        return mpr("This fountain has no liquid!"), false;
    if (feat == DNGN_FOUNTAIN_BLUE) {
        mpr("You drink the clean water.");
        fountain_effect = POT_WATER;
    }
    else if (feat == DNGN_FOUNTAIN_BLOOD)
    {
        if (!yesno("Drink from the fountain of blood?", true, 'n'))
            return false;

        mpr("You drink the blood.");
        fountain_effect = POT_BLOOD;
    }
    else
    {
        if (!yesno("Drink from the sparkling fountain?", true, 'n'))
            return false;

        mpr("You drink the sparkling water.");

        fountain_effect =
            random_choose_weighted(467, POT_WATER,
                48, POT_DEGENERATION,
                40, POT_UNSTABLE_MUTATION,
                40, POT_CURING,
                40, POT_HEAL_WOUNDS,
                40, POT_HASTE,
                40, POT_MIGHT,
                40, POT_AGILITY,
                40, POT_BRILLIANCE,
                27, POT_FLIGHT,
                27, POT_POISON,
                27, POT_SLOWING,
                27, POT_AMBROSIA,
                27, POT_INVISIBILITY,
                20, POT_MAGIC,
                20, POT_RESISTANCE,
                20, POT_STRONG_POISON,
                20, POT_BERSERK_RAGE,
                12, POT_MUTATION);
    }

    if (fountain_effect != NUM_POTIONS && fountain_effect != POT_BLOOD)
        xom_is_stimulated(50);

    // Good gods do not punish for bad random effects. However, they do
    // punish drinking from a fountain of blood.

    get_potion_effect(fountain_effect)->quaff(false);
    //potion_effect(fountain_effect, 100, nullptr, feat != DNGN_FOUNTAIN_SPARKLING, true);

    bool gone_dry = false;
    if (feat == DNGN_FOUNTAIN_BLUE)
    {
        if (one_chance_in(20))
            gone_dry = true;
    }
    else if (feat == DNGN_FOUNTAIN_BLOOD)
    {
        // High chance of drying up, to prevent abuse.
        if (one_chance_in(3))
            gone_dry = true;
    }
    else   // sparkling fountain
    {
        if (one_chance_in(10))
            gone_dry = true;
        else if (random2(50) > 40)
        {
            // Turn fountain into a normal fountain without any message
            // but the glyph colour gives it away (lightblue vs. blue).
            grd(you.pos()) = DNGN_FOUNTAIN_BLUE;
            set_terrain_changed(you.pos());
        }
    }

    if (gone_dry)
    {
        mpr("The fountain dries up!");

        grd(you.pos()) = DNGN_DRY_FOUNTAIN;
        set_terrain_changed(you.pos());

        crawl_state.cancel_cmd_repeat();
    }

    you.turn_is_over = true;
    return true;
}


void drink(item_def* potion)
{
    if (you_foodless(true, true) && you.species != SP_VAMPIRE)
    {
        mpr("You can't drink.");
        return;
    }
    if (you.form == transformation::eldritch)
    {
        mpr("You can't drink in this form.");
        return;
    }
    if (is_able_into_wall())
    {
        mpr("In this state, you cannot do this");
        return;
    }

    if (potion == nullptr)
    {
        const dungeon_feature_type feat = grd(you.pos());
        if (feat >= DNGN_FOUNTAIN_BLUE && feat <= DNGN_DRY_FOUNTAIN) {
            if (_drink_fountain()) {
                return;

            }
        }
    }

    if (you.berserk())
    {
        canned_msg(MSG_TOO_BERSERK);
        return;
    }

    if (you.species == SP_MUMMY || you.species == SP_LICH ||  you.species == SP_WIGHT)
    {
        if (you.duration[DUR_NO_POTIONS])
            mpr("You cannot drink potions in your current state!");
        else
            mpr("you cannot drink!");

        return;
    }
    
    // The Great Wyrm: sometimes you will waste potion
    if (player_under_penance(GOD_WYRM) && one_chance_in(3))
    {
        if (in_inventory(*potion))
        {
            dec_inv_item_quantity(potion->link, 1);
            auto_assign_item_slot(*potion);
        }
        else if (in_bag(*potion))
        {
            potion->quantity--;
            if (potion->quantity == 0) {
                potion->base_type = OBJ_UNASSIGNED;
                potion->props.clear();
            }
        }
        else {
            dec_mitm_item_quantity(potion->index(), 1);
        }
        count_action(CACT_USE, OBJ_POTIONS);
        
        simple_god_message(" extracts your potion just before you drink!", GOD_WYRM);
        you.turn_is_over = true;
        return;
    }

    if (!potion)
    {
        if (!use_an_item(potion, OBJ_POTIONS, OPER_QUAFF, "Drink which item (* to show all)?"))
            return;
    }

    if (potion->base_type != OBJ_POTIONS)
    {
        mpr("You can't drink that!");
        return;
    }

    const bool alreadyknown = item_type_known(*potion);

    if (alreadyknown && is_bad_item(*potion, true))
    {
        canned_msg(MSG_UNTHINKING_ACT);
        return;
    }

    bool penance = god_hates_item(*potion);
    string prompt = make_stringf("Really quaff the %s?%s",
                                 potion->name(DESC_DBNAME).c_str(),
                                 penance ? " This action would place"
                                           " you under penance!" : "");
    if (alreadyknown && (is_dangerous_item(*potion, true) || penance)
        && Options.bad_item_prompt
        && !yesno(prompt.c_str(), false, 'n'))
    {
        canned_msg(MSG_OK);
        return;
    }

    // The "> 1" part is to reduce the amount of times that Xom is
    // stimulated when you are a low-level 1 trying your first unknown
    // potions on monsters.
    const bool dangerous = (player_in_a_dangerous_place()
                            && you.experience_level > 1);

    if (player_under_penance(GOD_GOZAG) && one_chance_in(3))
    {
        simple_god_message(" petitions for your drink to fail.", GOD_GOZAG);
        you.turn_is_over = true;
        return;
    }

    if (!quaff_potion(*potion))
        return;

    if (!alreadyknown && dangerous)
    {
        // Xom loves it when you drink an unknown potion and there is
        // a dangerous monster nearby...
        xom_is_stimulated(200);
    }

    // We'll need this later, after destroying the item.
    const bool was_exp = potion->sub_type == POT_EXPERIENCE;
    if (in_inventory(*potion))
    {
        dec_inv_item_quantity(potion->link, 1);
        auto_assign_item_slot(*potion);
    }
    else if (in_bag(*potion))
    {
        potion->quantity--;
        if (potion->quantity == 0) {
            potion->base_type = OBJ_UNASSIGNED;
            potion->props.clear();
        }
    }
    else {
        dec_mitm_item_quantity(potion->index(), 1);
    }
    count_action(CACT_USE, OBJ_POTIONS);
    you.turn_is_over = true;

    // This got deferred from PotionExperience::effect to prevent SIGHUP abuse.
    if (was_exp)
        level_change();
}

// XXX: there's probably a nicer way of doing this.
bool god_hates_brand(const int brand)
{
    if (is_good_god(you.religion)
        && (brand == SPWPN_DRAINING
            || brand == SPWPN_VAMPIRISM
            || brand == SPWPN_CHAOS
            || brand == SPWPN_PAIN))
    {
        return true;
    }

    if (you_worship(GOD_CHEIBRIADOS) && (brand == SPWPN_CHAOS
                                         || brand == SPWPN_SPEED))
    {
        return true;
    }

    if (you_worship(GOD_YREDELEMNUL) && brand == SPWPN_HOLY_WRATH)
        return true;

    return false;
}

static void _rebrand_weapon(item_def& wpn)
{
    if (&wpn == you.weapon() && (you.duration[DUR_EXCRUCIATING_WOUNDS] || you.duration[DUR_POISON_WEAPON]))
        end_weapon_brand(wpn);
    if (&wpn == you.weapon() && you.duration[DUR_ELEMENTAL_WEAPON])
        end_elemental_weapon(wpn);


    const brand_type old_brand = get_weapon_brand(wpn);

    monster * spect = find_spectral_weapon(&you);
    if (&wpn == you.weapon() && old_brand == SPWPN_SPECTRAL && spect)
        end_spectral_weapon(spect, false);

    brand_type new_brand = old_brand;

    // now try and find an appropriate brand
    while (old_brand == new_brand || god_hates_brand(new_brand))
    {
        if (is_range_weapon(wpn))
        {
            new_brand = random_choose_weighted(
                                    33, SPWPN_FLAMING,
                                    33, SPWPN_FREEZING,
                                    23, SPWPN_VENOM,
                                    23, SPWPN_VORPAL,
                                    5, SPWPN_ELECTROCUTION,
                                    3, SPWPN_CHAOS);
        }
        else
        {
            new_brand = random_choose_weighted(
                                    28, SPWPN_FLAMING,
                                    28, SPWPN_FREEZING,
                                    23, SPWPN_VORPAL,
                                    18, SPWPN_VENOM,
                                    14, SPWPN_DRAINING,
                                    14, SPWPN_ELECTROCUTION,
                                    11, SPWPN_PROTECTION,
                                    11, SPWPN_SPECTRAL,
                                    8, SPWPN_VAMPIRISM,
                                    3, SPWPN_CHAOS);
        }
    }

    set_item_ego_type(wpn, OBJ_WEAPONS, new_brand);
    convert2bad(wpn);
}

static string _item_name(item_def &item)
{
    return item.name((in_inventory(item) || in_bag(item)) ? DESC_YOUR : DESC_THE);
}

static void _brand_weapon(item_def &wpn)
{
    you.wield_change = true;

    const string itname = _item_name(wpn);

    _rebrand_weapon(wpn);

    bool success = true;
    colour_t flash_colour = BLACK;

    switch (get_weapon_brand(wpn))
    {
    case SPWPN_VORPAL:
        flash_colour = YELLOW;
        mprf("%s emits a brilliant flash of light!",itname.c_str());
        break;

    case SPWPN_PROTECTION:
        flash_colour = YELLOW;
        mprf("%s projects an invisible shield of force!",itname.c_str());
        break;

    case SPWPN_FLAMING:
        flash_colour = RED;
        mprf("%s is engulfed in flames!", itname.c_str());
        break;

    case SPWPN_FREEZING:
        flash_colour = LIGHTCYAN;
        mprf("%s is covered with a thin layer of ice!", itname.c_str());
        break;

    case SPWPN_DRAINING:
        flash_colour = DARKGREY;
        mprf("%s craves living souls!", itname.c_str());
        break;

    case SPWPN_VAMPIRISM:
        flash_colour = DARKGREY;
        mprf("%s thirsts for the lives of mortals!", itname.c_str());
        break;

    case SPWPN_VENOM:
        flash_colour = GREEN;
        mprf("%s drips with poison.", itname.c_str());
        break;

    case SPWPN_ELECTROCUTION:
        flash_colour = LIGHTCYAN;
        mprf("%s crackles with electricity.", itname.c_str());
        break;

    case SPWPN_CHAOS:
        flash_colour = random_colour();
        mprf("%s erupts in a glittering mayhem of colour.", itname.c_str());
        break;

    case SPWPN_ACID:
        flash_colour = ETC_SLIME;
        mprf("%s oozes corrosive slime.", itname.c_str());
        break;

    case SPWPN_PACIFING:
        flash_colour = WHITE;
        mprf("%s purify.", itname.c_str());
        break;

    case SPWPN_SLUGGISH:
        flash_colour = LIGHTBLUE;
        mprf("%s slow down.", itname.c_str());
        break;

    case SPWPN_SLIMIFYING:
        flash_colour = ETC_SLIME;
        mprf("%s oozes corrosive slime.", itname.c_str());
        break;

    case SPWPN_SILVER:
        flash_colour = YELLOW;
        mprf("%s emits a brilliant flash of light!", itname.c_str());
        break;
    
    case SPWPN_SPECTRAL:
        flash_colour = BLUE;
        mprf("%s acquires a faint afterimage.", itname.c_str());
        break;
        
    default:
        success = false;
        break;
    }

    if (success)
    {
        item_set_appearance(wpn);
        // Message would spoil this even if we didn't identify.
        set_ident_flags(wpn, ISFLAG_KNOW_TYPE);
        mprf_nocap("%s", wpn.name(DESC_INVENTORY_EQUIP).c_str());
        // Might be rebranding to/from protection or evasion.
        you.redraw_armour_class = true;
        you.redraw_evasion = true;
        // Might be removing antimagic.
        calc_mp();
        flash_view_delay(UA_PLAYER, flash_colour, 300);
    }
    return;
}

static item_def* _choose_target_item_for_scroll(bool scroll_known, object_selector selector,
                                                const char* prompt)
{
    item_def *target = nullptr;
    bool success = false;

    success = use_an_item(target, selector, OPER_ANY, prompt,
                       [=]()
                       {
                           if (scroll_known
                               || crawl_state.seen_hups
                               || yesno("Really abort (and waste the scroll)?", false, 0))
                           {
                               return true;
                           }
                           return false;
                       });
    return success ? target : nullptr;
}

static object_selector _enchant_selector(scroll_type scroll)
{
    if (scroll == SCR_BRAND_WEAPON)
        return OSEL_BRANDABLE_WEAPON;
    else if (scroll == SCR_ENCHANT_WEAPON)
        return OSEL_ENCHANTABLE_WEAPON;
    die("Invalid scroll type %d for _enchant_selector", (int)scroll);
}

// Returns nullptr if no weapon was chosen.
static item_def* _scroll_choose_weapon(bool alreadyknown, const string &pre_msg,
                                       scroll_type scroll)
{
    const bool branding = scroll == SCR_BRAND_WEAPON;

    item_def* target = _choose_target_item_for_scroll(alreadyknown, _enchant_selector(scroll),
                                                      branding ? "Brand which weapon?"
                                                               : "Enchant which weapon?");
    if (!target)
        return target;

    if (alreadyknown)
        mpr(pre_msg);

    return target;
}

// Returns true if the scroll is used up.
static bool _handle_brand_weapon(bool alreadyknown, const string &pre_msg)
{
    item_def* weapon = _scroll_choose_weapon(alreadyknown, pre_msg,
                                             SCR_BRAND_WEAPON);
    if (!weapon)
        return !alreadyknown;

    _brand_weapon(*weapon);
    return true;
}

bool enchant_weapon(item_def &wpn, bool quiet)
{
    bool success = false;

    // Get item name now before changing enchantment.
    string iname = _item_name(wpn);

    if (is_weapon(wpn)
        && !is_artefact(wpn)
        && wpn.base_type == OBJ_WEAPONS
        && wpn.plus < MAX_WPN_ENCHANT)
    {
        wpn.plus++;
        success = true;
        if (!quiet)
            mprf("%s glows red for a moment.", iname.c_str());
    }

    if (!success && !quiet)
        canned_msg(MSG_NOTHING_HAPPENS);

    if (success)
        you.wield_change = true;

    return success;
}

/**
 * Prompt for an item to identify (either in the player's inventory or in
 * the ground), and then, if one is chosen, identify it.
 *
 * @param alreadyknown  Did we know that this was an ID scroll before we
 *                      started reading it?
 * @param pre_msg       'As you read the scroll of foo, it crumbles to dust.'
 * @param link[in,out]  The location of the ID scroll in the player's inventory
 *                      or, if it's on the floor, -1.
 *                      auto_assign_item_slot() may require us to update this.
 * @return  true if the scroll is used up. (That is, whether it was used or
 *          whether it was previously unknown (& thus uncancellable).)
 */
static bool _identify(bool alreadyknown, const string &pre_msg, int &link)
{
    item_def* itemp = _choose_target_item_for_scroll(alreadyknown, OSEL_UNIDENT,
                       "Identify which item? (\\ to view known items)");

    if (!itemp)
        return !alreadyknown;

    item_def& item = *itemp;
    if (alreadyknown)
        mpr(pre_msg);

    set_ident_type(item, true);
    set_ident_flags(item, ISFLAG_IDENT_MASK);

    // Output identified item.
    mprf_nocap("%s", menu_colour_item_name(item, DESC_INVENTORY_EQUIP).c_str());
    if (in_inventory(item))
    {
        if (item.link == you.equip[EQ_WEAPON])
            you.wield_change = true;

        if (item.is_type(OBJ_JEWELLERY, AMU_INACCURACY)
            && (item.link == you.equip[EQ_AMULET]
                || item.link == you.equip[EQ_AMULET_LEFT]
                || item.link == you.equip[EQ_AMULET_RIGHT]
                || item.link == you.equip[EQ_AMULET_ONE]
                || item.link == you.equip[EQ_AMULET_TWO]
                || item.link == you.equip[EQ_AMULET_THREE]
                || item.link == you.equip[EQ_AMULET_FOUR]
                || item.link == you.equip[EQ_AMULET_FIVE]
                || item.link == you.equip[EQ_AMULET_SIX]
                || item.link == you.equip[EQ_AMULET_SEVEN]
                || item.link == you.equip[EQ_AMULET_EIGHT]
                || item.link == you.equip[EQ_AMULET_NINE])
            && !item_known_cursed(item))
        {
            learned_something_new(HINT_INACCURACY);
        }

        const int target_link = item.link;
        item_def* moved_target = auto_assign_item_slot(item);
        if (moved_target != nullptr && moved_target->link == link)
        {
            // auto-swapped ID'd item with scrolls being used to ID it
            // correct input 'link' to the new location of the ID scroll stack
            // so that we decrement *it* instead of the ID'd item (10663)
            ASSERT(you.inv[target_link].defined());
            ASSERT(you.inv[target_link].is_type(OBJ_SCROLLS, SCR_IDENTIFY));
            link = target_link;
        }
    }
    return true;
}

static bool _handle_enchant_weapon(bool alreadyknown, const string &pre_msg)
{
    item_def* weapon = _scroll_choose_weapon(alreadyknown, pre_msg,
                                             SCR_ENCHANT_WEAPON);
    if (!weapon)
        return !alreadyknown;

    if (weapon->base_type == OBJ_RODS) {
        return recharge_wand(*weapon);
    }
    else {
        enchant_weapon(*weapon, false);
    }
    return true;
}

bool enchant_armour(int &ac_change, bool quiet, item_def &arm)
{
    ASSERT(arm.defined());
    ASSERT(arm.base_type == OBJ_ARMOUR);

    ac_change = 0;

    // Cannot be enchanted.
    if (!is_enchantable_armour(arm))
    {
        if (!quiet)
            canned_msg(MSG_NOTHING_HAPPENS);
        return false;
    }

    // Output message before changing enchantment and curse status.
    if (!quiet)
    {
        const bool plural = armour_is_hide(arm)
                            && arm.sub_type != ARM_TROLL_LEATHER_ARMOUR;
        mprf("%s %s green for a moment.",
             _item_name(arm).c_str(),
             conjugate_verb("glow", plural).c_str());
    }

    arm.plus++;
    ac_change++;

    return true;
}

static int _handle_enchant_armour(bool alreadyknown, const string &pre_msg)
{
    item_def* target = _choose_target_item_for_scroll(alreadyknown, OSEL_ENCHANTABLE_ARMOUR,
                                                      "Enchant which item?");

    if (!target)
        return alreadyknown ? -1 : 0;

    // Okay, we may actually (attempt to) enchant something.
    if (alreadyknown)
        mpr(pre_msg);

    int ac_change;
    bool result = enchant_armour(ac_change, false, *target);

    if (ac_change)
        you.redraw_armour_class = true;

    return result ? 1 : 0;
}

void random_uselessness()
{
    ASSERT(!crawl_state.game_is_arena());

    switch (random2(8))
    {
    case 0:
    case 1:
        mprf("The dust glows %s!", weird_glowing_colour().c_str());
        break;

    case 2:
        if (you.weapon())
        {
            mprf("%s glows %s for a moment.",
                 you.weapon()->name(DESC_YOUR).c_str(),
                 weird_glowing_colour().c_str());
        }
        else
        {
            mpr(you.hands_act("glow", weird_glowing_colour()
                                      + " for a moment."));
        }
        break;

    case 3:
        if (you.species == SP_MUMMY || you.species == SP_LICH)
            mpr("Your bandages flutter.");
        else // if (you.can_smell())
            mprf("You smell %s.", _weird_smell().c_str());
        break;

    case 4:
        mpr("You experience a momentary feeling of inescapable doom!");
        break;

    case 5:
        if (you.get_mutation_level(MUT_BEAK) || one_chance_in(3))
            mpr("Your brain hurts!");
        else if (you.species == SP_MUMMY || you.species == SP_LICH || coinflip())
            mpr("Your ears itch!");
        else
            mpr("Your nose twitches suddenly!");
        break;

    case 6:
    case 7:
        mprf(MSGCH_SOUND, "You hear %s.", _weird_sound().c_str());
        noisy(2, you.pos());
        break;
    }
}

static void _handle_read_book(item_def& book)
{
    if (you.berserk())
    {
        canned_msg(MSG_TOO_BERSERK);
        return;
    }

    if (you.duration[DUR_BRAINLESS])
    {
        mpr("Reading books requires mental cohesion, which you lack.");
        return;
    }

    ASSERT(book.sub_type != BOOK_MANUAL);

#if TAG_MAJOR_VERSION == 34
    if (book.sub_type == BOOK_BUGGY_DESTRUCTION)
    {
        mpr("This item has been removed, sorry!");
        return;
    }
#endif

    set_ident_flags(book, ISFLAG_IDENT_MASK);
    read_book(book);
}

static void _vulnerability_scroll()
{
    mon_enchant lowered_mr(ENCH_LOWERED_MR, 1, &you, 400);

    // Go over all creatures in LOS.
    for (radius_iterator ri(you.pos(), LOS_NO_TRANS); ri; ++ri)
    {
        if (monster* mon = monster_at(*ri))
        {
            // If relevant, monsters have their MR halved.
            if (!mons_immune_magic(*mon))
                mon->add_ench(lowered_mr);

            // Annoying but not enough to turn friendlies against you.
            if (!mon->wont_attack())
                behaviour_event(mon, ME_ANNOY, &you);
        }
    }

    you.set_duration(DUR_LOWERED_MR, 40, 0, "Magic quickly surges around you.");
}

static bool _is_cancellable_scroll(scroll_type scroll)
{
    return scroll == SCR_IDENTIFY
           || scroll == SCR_BLINKING
           || scroll == SCR_ENCHANT_ARMOUR
           || scroll == SCR_AMNESIA
           || scroll == SCR_REMOVE_CURSE
#if TAG_MAJOR_VERSION == 34
           || scroll == SCR_CURSE_ARMOUR
           || scroll == SCR_CURSE_JEWELLERY
           || scroll == SCR_RECHARGING
#endif
           || scroll == SCR_BRAND_WEAPON
           || scroll == SCR_ENCHANT_WEAPON
           || scroll == SCR_MAGIC_MAPPING
           || scroll == SCR_ACQUIREMENT
           || scroll == SCR_COLLECTION
           || scroll == SCR_WISH;
}

/**
 * Is the player currently able to use the 'r' command (to read books or
 * scrolls). Being too berserk, confused, or having no reading material will
 * prevent this.
 *
 * Prints corresponding messages. (Thanks, canned_msg().)
 */
bool player_can_read()
{
    if (you.berserk())
    {
        canned_msg(MSG_TOO_BERSERK);
        return false;
    }    
    if (you.form == transformation::eldritch)
    {
        mpr("You can't read in this form.");
        return false;
    }
    if (is_able_into_wall())
    {
        mpr("In this state, you cannot do this");
        return false;
    }
    if (you.confused())
    {
        canned_msg(MSG_TOO_CONFUSED);
        return false;
    }

    return true;
}

/**
 * If the player has no items matching the given selector, give an appropriate
 * response to print. Otherwise, if they do have such items, return the empty
 * string.
 */
static string _no_items_reason(object_selector type, bool check_floor = false)
{
    if (!any_items_of_type(type, -1, check_floor))
        return no_selectables_message(type);
    return "";
}

/**
 * If the player is unable to (r)ead the item in the given slot, return the
 * reason why. Otherwise (if they are able to read it), returns "", the empty
 * string.
 */
string cannot_read_item_reason(const item_def &item)
{
    // can read books, except for manuals...
    if (item.base_type == OBJ_BOOKS)
    {
        if (item.sub_type == BOOK_MANUAL)
            return "You can't read that!";
        return "";
    }

    // and scrolls - but nothing else.
    if (item.base_type != OBJ_SCROLLS)
        return "You can't read that!";

    // the below only applies to scrolls. (it's easier to read books, since
    // that's just a UI/strategic thing.)

    if (silenced(you.pos()))
        return "Magic scrolls do not work when you're silenced!";

    // water elementals
    if (you.duration[DUR_WATER_HOLD] && !you.res_water_drowning())
        return "You cannot read scrolls while unable to breathe!";

    // ru
    if (you.duration[DUR_NO_SCROLLS])
        return "You cannot read scrolls in your current state!";

    // Prevent hot lava orcs reading scrolls
    if (you.species == SP_LAVA_ORC && temperature_effect(LORC_NO_SCROLLS))
        return "You'd burn any scroll you tried to read!";\

    // don't waste the player's time reading known scrolls in situations where
    // they'd be useless

    if (!item_type_known(item))
        return "";

    switch (item.sub_type)
    {
        case SCR_BLINKING:
        case SCR_TELEPORTATION:
            return you.no_tele_reason(false, item.sub_type == SCR_BLINKING);

        case SCR_AMNESIA:
            if (you.spell_no == 0)
                return "You have no spells to forget!";
            return "";

        case SCR_ENCHANT_ARMOUR:
            return _no_items_reason(OSEL_ENCHANTABLE_ARMOUR, true);

        case SCR_ENCHANT_WEAPON:
            return _no_items_reason(OSEL_ENCHANTABLE_WEAPON, true);

        case SCR_IDENTIFY:
            return _no_items_reason(OSEL_UNIDENT, true);

        case SCR_REMOVE_CURSE:
            return _no_items_reason(OSEL_CURSED_WORN);

#if TAG_MAJOR_VERSION == 34
        case SCR_CURSE_WEAPON:
            if (!you.weapon())
                return "This scroll only affects a wielded weapon!";

            // assumption: wielded weapons always have their curse & brand known
            if (you.weapon()->cursed())
                return "Your weapon is already cursed!";

            if (get_weapon_brand(*you.weapon()) == SPWPN_HOLY_WRATH)
                return "Holy weapons cannot be cursed!";
            return "";

        case SCR_CURSE_ARMOUR:
            return _no_items_reason(OSEL_UNCURSED_WORN_ARMOUR);

        case SCR_CURSE_JEWELLERY:
            return _no_items_reason(OSEL_UNCURSED_WORN_JEWELLERY);
#endif

        default:
            return "";
    }
}

/**
 * Check if a particular scroll type would hurt a monster.
 *
 * @param scr           Scroll type in question
 * @param m             Actor as a potential victim to the scroll
 * @return  true if the provided scroll type is harmful to the actor.
 */
static bool _scroll_will_harm(const scroll_type scr, const actor &m)
{
    if (!m.alive())
        return false;

    switch (scr)
    {
        case SCR_HOLY_WORD:
            if (m.undead_or_demonic())
                return true;
            break;
        case SCR_TORMENT:
            if (!m.res_torment())
                return true;
            break;
        default: break;
    }

    return false;
}

/**
 * Check to see if the player can read the item in the given slot, and if so,
 * reads it. (Examining books and using scrolls.)
 *
 * @param slot      The slot of the item in the player's inventory. If -1, the
 *                  player is prompted to choose a slot.
 */
void read(item_def* scroll)
{
    if (!player_can_read())
        return;

    if (!scroll)
    {
        if (!use_an_item(scroll, OBJ_SCROLLS, OPER_READ, "Read which item (* to show all)?"))
            return;
    }

    const string failure_reason = cannot_read_item_reason(*scroll);
    if (!failure_reason.empty())
    {
        mprf(MSGCH_PROMPT, "%s", failure_reason.c_str());
        return;
    }

    if (scroll->base_type == OBJ_BOOKS)
    {
        _handle_read_book(*scroll);
        return;
    }

    const scroll_type which_scroll = static_cast<scroll_type>(scroll->sub_type);
    // Handle player cancels before we waste time (with e.g. blurryvis)
    if (item_type_known(*scroll)) {
        bool penance = god_hates_item(*scroll);
        string verb_object = "read the " + scroll->name(DESC_DBNAME);

        string penance_prompt = make_stringf("Really %s? This action would"
                                             " place you under penance!",
                                             verb_object.c_str());

        targeter_radius hitfunc(&you, LOS_NO_TRANS);

        if (stop_attack_prompt(hitfunc, verb_object.c_str(),
                               [which_scroll] (const actor* m)
                               {
                                   return _scroll_will_harm(which_scroll, *m);
                               },
                               nullptr, nullptr))
        {
            return;
        }
        else if (penance && !yesno(penance_prompt.c_str(), false, 'n'))
        {
            canned_msg(MSG_OK);
            return;
        }
        else if ((is_dangerous_item(*scroll, true)
                  || is_bad_item(*scroll, true))
                 && Options.bad_item_prompt
                 && !yesno(make_stringf("Really %s?",
                                        verb_object.c_str()).c_str(),
                           false, 'n'))
        {
            canned_msg(MSG_OK);
            return;
        }

        if (scroll->sub_type == SCR_BLINKING
            && orb_limits_translocation()
            && !yesno("Your blink will be uncontrolled - continue anyway?",
                      false, 'n'))
        {
            canned_msg(MSG_OK);
            return;
        }
    }

    if (you.get_mutation_level(MUT_BLURRY_VISION)
        && !i_feel_safe(false, false, true)
        && !yesno("Really read with blurry vision while enemies are nearby?",
                  false, 'n'))
    {
        canned_msg(MSG_OK);
        return;
    }

    // Ok - now we FINALLY get to read a scroll !!! {dlb}
    you.turn_is_over = true;

    if (you.duration[DUR_BRAINLESS] && !one_chance_in(5))
    {
        mpr("You almost manage to decipher the scroll,"
            " but fail in this attempt.");
        return;
    }

    // if we have blurry vision, we need to start a delay before the actual
    // scroll effect kicks in.
    if (you.get_mutation_level(MUT_BLURRY_VISION))
    {
        // takes 0.5, 1, 2 extra turns
        const int turns = max(1, you.get_mutation_level(MUT_BLURRY_VISION) - 1);
        start_delay<BlurryScrollDelay>(turns, *scroll);
        if (you.get_mutation_level(MUT_BLURRY_VISION) == 1)
            you.time_taken /= 2;
    }
    else
        read_scroll(*scroll);
}

/**
 * Read the provided scroll.
 *
 * Does NOT check whether the player can currently read, whether the scroll is
 * currently useless, etc. Likewise doesn't handle blurry vision, setting
 * you.turn_is_over, and other externals. DOES destroy one scroll, unless the
 * player chooses to cancel at the last moment.
 *
 * @param scroll The scroll to be read.
 */
void read_scroll(item_def& scroll)
{
    const scroll_type which_scroll = static_cast<scroll_type>(scroll.sub_type);
    const int prev_quantity = scroll.quantity;
    int link = in_inventory(scroll) ? scroll.link : -1;
    const bool alreadyknown = item_type_known(scroll);

    // For cancellable scrolls leave printing this message to their
    // respective functions.
    const string pre_succ_msg =
            make_stringf("As you read the %s, it crumbles to dust.",
                          scroll.name(DESC_QUALNAME).c_str());
    if (!_is_cancellable_scroll(which_scroll))
    {
        mpr(pre_succ_msg);
        // Actual removal of scroll done afterwards. -- bwr
    }

    const bool dangerous = player_in_a_dangerous_place();

    // ... but some scrolls may still be cancelled afterwards.
    bool cancel_scroll = false;
    bool bad_effect = false; // for Xom: result is bad (or at least dangerous)

    switch (which_scroll)
    {
    case SCR_RANDOM_USELESSNESS:
        random_uselessness();
        break;

    case SCR_BLINKING:
    {
        const string reason = you.no_tele_reason(true, true);
        if (!reason.empty())
        {
            mpr(pre_succ_msg);
            mpr(reason);
            break;
        }

        const bool safely_cancellable
            = alreadyknown && !you.get_mutation_level(MUT_BLURRY_VISION);

        if (orb_limits_translocation())
        {
            mprf(MSGCH_ORB, "The Orb prevents control of your translocation!");
            uncontrolled_blink();
        }
        else
        {
            cancel_scroll = (cast_controlled_blink(false, safely_cancellable)
                             == spret::abort) && alreadyknown;
        }

        if (!cancel_scroll)
            mpr(pre_succ_msg); // ordering is iffy but w/e
    }
        break;

    case SCR_TELEPORTATION:
        you_teleport();
        break;

    case SCR_REMOVE_CURSE:
        if (!alreadyknown)
        {
            mpr(pre_succ_msg);
            remove_curse(false);
        }
        else
            cancel_scroll = !remove_curse(true, pre_succ_msg);
        break;

    case SCR_ACQUIREMENT:
            mpr("This is a scroll of acquirement!");

        // included in default force_more_message
        // Identify it early in case the player checks the '\' screen.
        set_ident_type(scroll, true);

        if (feat_eliminates_items(grd(you.pos())))
        {
            mpr("Anything you acquired here would fall and be lost!");
            cancel_scroll = true;
            break;
            // yes, we cancel out even if the scroll wasn't known beforehand.
            // there's no plausible abuse of this, and it's much better to
            // never have to worry about "am i over dangerous terrain?" while
            // IDing scrolls. (Not an interesting ID game mechanic!)
        }

        cancel_scroll = !acquirement_menu();
        break;

    case SCR_COLLECTION:
        if (player_in_branch(BRANCH_ABYSS) 
            || player_in_branch(BRANCH_PANDEMONIUM) )
        {
            mpr("You can't summon artefact in unstable location!");
            cancel_scroll = true;
        }
        else
        {
            mpr("This is a scroll of collection!!!");
            cancel_scroll = !artefact_acquirement_menu();
        }
        break;
    case SCR_WISH:
        {
            mpr("This is a scroll of wish!");
            cancel_scroll = !scroll_of_wish_menu();
        }
        break;
    case SCR_FEAR:
        mpr("You assume a fearsome visage.");
        mass_enchantment(ENCH_FEAR, 1000);
        break;

    case SCR_NOISE:
        noisy(25, you.pos(), "You hear a loud clanging noise!");
        break;

    case SCR_SUMMONING:
        cast_shadow_creatures(MON_SUMM_SCROLL);
        break;

    case SCR_FOG:
    {
        if (alreadyknown && (env.level_state & LSTATE_STILL_WINDS))
        {
            mpr("The air is too still for clouds to form.");
            cancel_scroll = true;
            break;
        }
        mpr("The scroll dissolves into smoke.");
        auto smoke = random_smoke_type();
        big_cloud(smoke, &you, you.pos(), 50, 8 + random2(8));
        break;
    }

    case SCR_MAGIC_MAPPING:
        if (alreadyknown && !is_map_persistent())
        {
            cancel_scroll = true;
            mpr("It would have no effect in this place.");
            break;
        }
        mpr(pre_succ_msg);
        magic_mapping(500, 100, false);
        break;

    case SCR_TORMENT:
        torment(&you, TORMENT_SCROLL, you.pos());

        // This is only naughty if you know you're doing it.
        did_god_conduct(DID_EVIL, 10, item_type_known(scroll));
        bad_effect = !player_res_torment(false);
        break;

    case SCR_IMMOLATION:
    {
        bool had_effect = false;
        for (monster_near_iterator mi(you.pos(), LOS_NO_TRANS); mi; ++mi)
        {           
            // Don't leak information about Mara and rakshasa clones.
            if (mons_immune_magic(**mi)
                || mi->is_summoned() && !mi->is_illusion())
                continue;

            if (mi->add_ench(mon_enchant(ENCH_INNER_FLAME, 0, &you)))
                had_effect = true;
        }

        if (had_effect)
            mpr("The creatures around you are filled with an inner flame!");
        else
            mpr("The air around you briefly surges with heat, but it dissipates.");

        bad_effect = true;
        break;
    }

#if TAG_MAJOR_VERSION == 34
    case SCR_CURSE_WEAPON:
    {
        // Not you.weapon() because we want to handle melded weapons too.
        item_def * const weapon = you.slot_item(EQ_WEAPON, true);
        if (!weapon || !is_weapon(*weapon) || weapon->cursed())
        {
            bool plural = false;
            const string weapon_name =
                weapon ? weapon->name(DESC_YOUR)
                       : "Your " + you.hand_name(true, &plural);
            mprf("%s very briefly gain%s a black sheen.",
                 weapon_name.c_str(), plural ? "" : "s");
        }
        else
        {
            // Also sets wield_change.
            do_curse_item(*weapon, false);
            learned_something_new(HINT_YOU_CURSED);
            bad_effect = true;
        }
        break;
    }
#endif

    case SCR_ENCHANT_WEAPON:
        if (!alreadyknown)
        {
            mpr(pre_succ_msg);
            mpr("It is a scroll of enchant weapon.");
            // included in default force_more_message (to show it before menu)
        }

        cancel_scroll = !_handle_enchant_weapon(alreadyknown, pre_succ_msg);
        break;

    case SCR_BRAND_WEAPON:
        if (!alreadyknown)
        {
            mpr(pre_succ_msg);
            mpr("It is a scroll of brand weapon.");
            // included in default force_more_message (to show it before menu)
        }

        cancel_scroll = !_handle_brand_weapon(alreadyknown, pre_succ_msg);
        break;

    case SCR_IDENTIFY:
        if (!alreadyknown)
        {
            mpr(pre_succ_msg);
            mpr("It is a scroll of identify.");
            // included in default force_more_message (to show it before menu)
            // Do this here so it doesn't turn up in the ID menu.
            set_ident_type(scroll, true);
        }
        cancel_scroll = !_identify(alreadyknown, pre_succ_msg, link);
        break;

    case SCR_ENCHANT_ARMOUR:
        if (!alreadyknown)
        {
            mpr(pre_succ_msg);
            mpr("It is a scroll of enchant armour.");
            // included in default force_more_message (to show it before menu)
        }
        cancel_scroll =
            (_handle_enchant_armour(alreadyknown, pre_succ_msg) == -1);
        break;
#if TAG_MAJOR_VERSION == 34
    // Should always be identified by Ashenzari.
    case SCR_CURSE_ARMOUR:
    case SCR_CURSE_JEWELLERY:
    {
        const bool armour = which_scroll == SCR_CURSE_ARMOUR;
        cancel_scroll = !curse_item(armour, pre_succ_msg);
        break;
    }

    case SCR_RECHARGING:
    {
        mpr("This item has been removed, sorry!");
        cancel_scroll = true;
        break;
    }
#endif

    case SCR_HOLY_WORD:
    {
        holy_word(100, HOLY_WORD_SCROLL, you.pos(), false, &you);

        // This is always naughty, even if you didn't affect anyone.
        // Don't speak those foul holy words even in jest!
        did_god_conduct(DID_HOLY, 10, item_type_known(scroll));
        bad_effect = you.undead_or_demonic();
        break;
    }

    case SCR_SILENCE:
        cast_silence(30);
        break;

    case SCR_VULNERABILITY:
        _vulnerability_scroll();
        break;

    case SCR_AMNESIA:
        if (!alreadyknown)
        {
            mpr(pre_succ_msg);
            mpr("It is a scroll of amnesia.");
            // included in default force_more_message (to show it before menu)
        }
        if (you.spell_no == 0)
            mpr("You feel forgetful for a moment.");
        else
        {
            bool done;
            bool aborted;
            do
            {
                aborted = cast_selective_amnesia() == -1;
                done = !aborted
                       || alreadyknown
                       || crawl_state.seen_hups
                       || yesno("Really abort (and waste the scroll)?",
                                false, 0);
                cancel_scroll = aborted && alreadyknown;
            } while (!done);
            if (aborted)
                canned_msg(MSG_OK);
        }
        break;

    default:
        mpr("Read a buggy scroll, please report this.");
        break;
    }

    if (cancel_scroll)
        you.turn_is_over = false;

    set_ident_type(scroll, true);
    set_ident_flags(scroll, ISFLAG_KNOW_TYPE); // for notes

    string scroll_name = scroll.name(DESC_QUALNAME);

    if (!cancel_scroll)
    {
        if (in_inventory(scroll))
            dec_inv_item_quantity(link, 1);
        else if (in_bag(scroll))
        {
            scroll.quantity--;
            if (scroll.quantity == 0) {
                scroll.base_type = OBJ_UNASSIGNED;
                scroll.props.clear();
            }
        }
        else
        {
            dec_mitm_item_quantity(scroll.index(), 1);
        }
        count_action(CACT_USE, OBJ_SCROLLS);
    }

    if (!alreadyknown
        && which_scroll != SCR_BRAND_WEAPON
        && which_scroll != SCR_ENCHANT_WEAPON
        && which_scroll != SCR_IDENTIFY
        && which_scroll != SCR_ENCHANT_ARMOUR
#if TAG_MAJOR_VERSION == 34
        && which_scroll != SCR_RECHARGING
#endif
        && which_scroll != SCR_AMNESIA
        && which_scroll != SCR_ACQUIREMENT)
    {
        mprf("It %s a %s.",
             scroll.quantity < prev_quantity ? "was" : "is",
             scroll_name.c_str());
    }

    if (!alreadyknown && dangerous)
    {
        // Xom loves it when you read an unknown scroll and there is a
        // dangerous monster nearby... (though not as much as potions
        // since there are no *really* bad scrolls, merely useless ones).
        xom_is_stimulated(bad_effect ? 100 : 50);
    }

    if (!alreadyknown)
        auto_assign_item_slot(scroll);

}

vector<equipment_type> current_equip_types()
{
    vector<equipment_type> weap_ret = _current_weapon_types();
    vector<equipment_type> ret = _current_ring_types();
    vector<equipment_type> amulet_ret = _current_amulet_types();
    weap_ret.insert(weap_ret.begin(), ret.begin(), ret.end());
    weap_ret.insert(weap_ret.begin(), amulet_ret.begin(), amulet_ret.end());
    return weap_ret;
}

vector<equipment_type> current_armour_types()
{
    vector<equipment_type> ret;
    for(int i = 0; i < 6; ++i)
    {
        const equipment_type slot = (equipment_type)(EQ_CLOAK + i);

        if (get_form()->slot_available(slot) && you.equip[slot] != -1)
            ret.push_back(slot);
    }
    return ret;
}

#ifdef USE_TILE
// Interactive menu for item drop/use.

void tile_item_use_floor(int idx)
{
    if (mitm[idx].is_type(OBJ_CORPSES, CORPSE_BODY))
        butchery(&mitm[idx]);
}

void tile_item_pickup(int idx, bool part)
{
    if (item_is_stationary(mitm[idx]))
    {
        mpr("You can't pick that up.");
        return;
    }

    if (part)
    {
        pickup_menu(idx);
        return;
    }
    pickup_single_item(idx, -1);
}

void tile_item_drop(int idx, bool partdrop)
{
    int quantity = you.inv[idx].quantity;
    if (partdrop && quantity > 1)
    {
        quantity = prompt_for_int("Drop how many? ", true);
        if (quantity < 1)
        {
            canned_msg(MSG_OK);
            return;
        }
        if (quantity > you.inv[idx].quantity)
            quantity = you.inv[idx].quantity;
    }
    drop_item(idx, quantity);
}

void tile_item_eat_floor(int idx)
{
    if (can_eat(mitm[idx], false))
        eat_item(mitm[idx]);
}

void tile_item_use_secondary(int idx)
{
    const item_def item = you.inv[idx];

    if (item.base_type == OBJ_WEAPONS && is_throwable(&you, item))
    {
        if (check_warning_inscriptions(item, OPER_FIRE))
            fire_thing(idx); // fire weapons
    }
    else if (you.equip[EQ_WEAPON] == idx)
        wield_weapon(true, SLOT_BARE_HANDS);
    else if (item_is_wieldable(item))
    {
        // secondary wield for several spells and such
        wield_weapon(true, idx); // wield
    }
}

void tile_item_use(int idx)
{
    const item_def item = you.inv[idx];

    // Equipped?
    bool equipped = false;
    bool equipped_weapon = false;
    for (unsigned int i = EQ_FIRST_EQUIP; i < NUM_EQUIP; i++)
    {
        if (you.equip[i] == idx)
        {
            equipped = true;
            if (i == EQ_WEAPON)
                equipped_weapon = true;
            break;
        }
    }

    // Special case for folks who are wielding something
    // that they shouldn't be wielding.
    // Note that this is only a problem for equipables
    // (otherwise it would only waste a turn)
    if (you.equip[EQ_WEAPON] == idx
        && (item.base_type == OBJ_ARMOUR
            || item.base_type == OBJ_JEWELLERY))
    {
        wield_weapon(true, SLOT_BARE_HANDS);
        return;
    }

    const int type = item.base_type;

    // Use it
    switch (type)
    {
        case OBJ_WEAPONS:
        case OBJ_STAVES:
        case OBJ_RODS:
        case OBJ_MISCELLANY:
        case OBJ_WANDS:
            // Wield any unwielded item of these types.
            if (!equipped && item_is_wieldable(item))
            {
                wield_weapon(true, idx);
                return;
            }
            // Evoke misc. items or wands.
            if (item_is_evokable(item, false))
            {
                evoke_item(idx);
                return;
            }
            // Unwield wielded items.
            if (equipped)
                wield_weapon(true, SLOT_BARE_HANDS);
            return;

        case OBJ_MISSILES:
            if (check_warning_inscriptions(item, OPER_FIRE))
                fire_thing(idx);
            return;

        case OBJ_ARMOUR:
            if (!form_can_wear())
            {
                mpr("You can't wear or remove anything in your present form.");
                return;
            }
            if (equipped && !equipped_weapon)
            {
                if (check_warning_inscriptions(item, OPER_TAKEOFF))
                    takeoff_armour(idx);
            }
            else if (check_warning_inscriptions(item, OPER_WEAR))
                wear_armour(idx);
            return;

        case OBJ_FOOD:
            if (check_warning_inscriptions(item, OPER_EAT))
                eat_food(idx);
            return;

        case OBJ_SCROLLS:
            if (check_warning_inscriptions(item, OPER_READ))
                read(&you.inv[idx]);
            return;

        case OBJ_JEWELLERY:
            if (equipped && !equipped_weapon)
                remove_ring(idx);
            else if (check_warning_inscriptions(item, OPER_PUTON))
                puton_ring(idx);
            return;

        case OBJ_POTIONS:
            if (check_warning_inscriptions(item, OPER_QUAFF))
                drink(&you.inv[idx]);
            return;

        default:
            return;
    }
}
#endif
