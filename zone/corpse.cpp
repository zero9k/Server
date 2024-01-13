/*	EQEMu: Everquest Server Emulator
	Copyright (C) 2001-2003 EQEMu Development Team (http://eqemulator.net)

	This program is free software; you can redistribute it and/or modify
	it under the terms of the GNU General Public License as published by
	the Free Software Foundation; version 2 of the License.

	This program is distributed in the hope that it will be useful,
	but WITHOUT ANY WARRANTY except by those people which sell it, which
	are required to give you total support for your newly bought product;
	without even the implied warranty of MERCHANTABILITY or FITNESS FOR
	A PARTICULAR PURPOSE. See the GNU General Public License for more details.

	You should have received a copy of the GNU General Public License
	along with this program; if not, write to the Free Software
	Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA
*/
/*
New class for handeling corpses and everything associated with them.
Child of the Mob class.
-Quagmire
*/

#ifdef _WINDOWS
	#if (!defined(_MSC_VER) || (defined(_MSC_VER) && _MSC_VER < 1900))
		#define snprintf	_snprintf
		#define vsnprintf	_vsnprintf
	#endif
    #define strncasecmp	_strnicmp
    #define strcasecmp	_stricmp
#endif

#include "../common/data_verification.h"
#include "../common/global_define.h"
#include "../common/eqemu_logsys.h"
#include "../common/rulesys.h"
#include "../common/strings.h"
#include "../common/say_link.h"

#include "corpse.h"
#include "entity.h"
#include "expedition.h"
#include "groups.h"
#include "mob.h"
#include "raids.h"

#include "bot.h"

#include "quest_parser_collection.h"
#include "string_ids.h"
#include "worldserver.h"
#include "../common/events/player_event_logs.h"
#include <iostream>


extern EntityList entity_list;
extern Zone* zone;
extern WorldServer worldserver;
extern npcDecayTimes_Struct npcCorpseDecayTimes[100];

void Corpse::SendEndLootErrorPacket(Client* client) {
	auto outapp = new EQApplicationPacket(OP_LootComplete, 0);
	client->QueuePacket(outapp);
	safe_delete(outapp);
}

void Corpse::SendLootReqErrorPacket(Client* client, LootResponse response) {
	auto outapp = new EQApplicationPacket(OP_MoneyOnCorpse, sizeof(moneyOnCorpseStruct));
	moneyOnCorpseStruct* d = (moneyOnCorpseStruct*) outapp->pBuffer;
	d->response		= static_cast<uint8>(response);
	d->unknown1		= 0x5a;
	d->unknown2		= 0x40;
	client->QueuePacket(outapp);
	safe_delete(outapp);
}

Corpse* Corpse::LoadCharacterCorpseEntity(uint32 in_dbid, uint32 in_charid, std::string in_charname, const glm::vec4& position, std::string time_of_death, bool rezzed, bool was_at_graveyard, uint32 guild_consent_id) {
	CharacterCorpseEntry ce;
	if (!database.LoadCharacterCorpseData(in_dbid, ce)) {
		LogDebug("Unable to create a corpse entity for [{}] [{}] [{}]", in_dbid, in_charid, in_charname);
		return nullptr;
	}

	ItemList itemlist;
	for (auto &item: ce.items) {
		auto tmp = new ServerLootItem_Struct;

		tmp->equip_slot = item.equip_slot;
		tmp->item_id    = item.item_id;
		tmp->charges    = item.charges;
		tmp->lootslot   = item.lootslot;
		tmp->aug_1      = item.aug_1;
		tmp->aug_2      = item.aug_2;
		tmp->aug_3      = item.aug_3;
		tmp->aug_4      = item.aug_4;
		tmp->aug_5      = item.aug_5;
		tmp->aug_6      = item.aug_6;
		tmp->attuned    = item.attuned;

		itemlist.push_back(tmp);
	}

	/* Create Corpse Entity */
	auto pc = new Corpse(
		in_dbid,          // uint32 in_dbid
		in_charid,          // uint32 in_charid
		in_charname.c_str(), // char* in_charname
		&itemlist,          // ItemList* in_itemlist
		ce.copper,     // uint32 in_copper
		ce.silver,     // uint32 in_silver
		ce.gold,          // uint32 in_gold
		ce.plat,          // uint32 in_plat
		position,
		ce.size,    // float in_size
		ce.gender,      // uint8 in_gender
		ce.race,    // uint16 in_race
		ce.class_,      // uint8 in_class
		ce.deity,       // uint8 in_deity
		ce.level,       // uint8 in_level
		ce.texture,     // uint8 in_texture
		ce.helmtexture, // uint8 in_helmtexture
		ce.exp,     // uint32 in_rezexp
		was_at_graveyard  // bool wasAtGraveyard
	);

	if (ce.locked)
		pc->Lock();

	/* Load Item Tints */
	pc->item_tint.Head.Color      = ce.item_tint.Head.Color;
	pc->item_tint.Chest.Color     = ce.item_tint.Chest.Color;
	pc->item_tint.Arms.Color      = ce.item_tint.Arms.Color;
	pc->item_tint.Wrist.Color     = ce.item_tint.Wrist.Color;
	pc->item_tint.Hands.Color     = ce.item_tint.Hands.Color;
	pc->item_tint.Legs.Color      = ce.item_tint.Legs.Color;
	pc->item_tint.Feet.Color      = ce.item_tint.Feet.Color;
	pc->item_tint.Primary.Color   = ce.item_tint.Primary.Color;
	pc->item_tint.Secondary.Color = ce.item_tint.Secondary.Color;

	/* Load Physical Appearance */
	pc->haircolor        = ce.haircolor;
	pc->beardcolor       = ce.beardcolor;
	pc->eyecolor1        = ce.eyecolor1;
	pc->eyecolor2        = ce.eyecolor2;
	pc->hairstyle        = ce.hairstyle;
	pc->luclinface       = ce.face;
	pc->beard            = ce.beard;
	pc->drakkin_heritage = ce.drakkin_heritage;
	pc->drakkin_tattoo   = ce.drakkin_tattoo;
	pc->drakkin_details  = ce.drakkin_details;
	pc->IsRezzed(rezzed);
	pc->become_npc         = false;
	pc->consented_guild_id = guild_consent_id;

	pc->UpdateEquipmentLight(); // itemlist populated above..need to determine actual values

	return pc;
}

Corpse::Corpse(
	NPC *in_npc,
	ItemList *in_itemlist,
	uint32 in_npctypeid,
	const NPCType **in_npctypedata,
	uint32 in_decaytime
) : Mob(
	"Unnamed_Corpse", // in_name
	"", // in_lastname
	0, // in_cur_hp
	0, // in_max_hp
	in_npc->GetGender(), // in_gender
	in_npc->GetRace(), // in_race
	in_npc->GetClass(), // in_class
	BT_Humanoid, // in_bodytype
	in_npc->GetDeity(), // in_deity
	in_npc->GetLevel(), // in_level
	in_npc->GetNPCTypeID(), // in_npctype_id
	in_npc->GetSize(), // in_size
	0.0f, // in_runspeed
	in_npc->GetPosition(), // position
	in_npc->GetInnateLightType(), // in_light
	in_npc->GetTexture(), // in_texture
	in_npc->GetHelmTexture(), // in_helmtexture
	0, // in_ac
	0, // in_atk
	0, // in_str
	0, // in_sta
	0, // in_dex
	0, // in_agi
	0, // in_int
	0, // in_wis
	0, // in_cha
	0, // in_haircolor
	0, // in_beardcolor
	0, // in_eyecolor1
	0, // in_eyecolor2
	0, // in_hairstyle
	0, // in_luclinface
	0, // in_beard
	0, // in_drakkin_heritage
	0, // in_drakkin_tattoo
	0, // in_drakkin_details
	EQ::TintProfile(), // in_armor_tint
	0xFF, // in_aa_title
	0, // in_see_invis
	0, // in_see_invis_undead
	0, // in_see_hide
	0, // in_see_improved_hide
	0, // in_hp_regen
	0, // in_mana_regen
	0, // in_qglobal
	0, // in_maxlevel
	0, // in_scalerate
	0, // in_armtexture
	0, // in_bracertexture
	0, // in_handtexture
	0, // in_legtexture
	0, // in_feettexture
	(*in_npctypedata)->use_model, // in_usemodel
	false, // in_always_aggros_foes
	0, // in_heroic_strikethrough
	false // in_keeps_sold_items
),
	  corpse_decay_timer(in_decaytime),
	  corpse_rez_timer(0),
	  corpse_delay_timer(RuleI(NPC, CorpseUnlockTimer)),
	  corpse_graveyard_timer(0),
	  loot_cooldown_timer(10)
{
	corpse_graveyard_timer.Disable();

	is_corpse_changed = false;
	is_player_corpse = false;
	is_locked = false;
	being_looted_by = 0xFFFFFFFF;
	if (in_itemlist) {
		itemlist = *in_itemlist;
		in_itemlist->clear();
	}

	SetCash(in_npc->GetCopper(), in_npc->GetSilver(), in_npc->GetGold(), in_npc->GetPlatinum());

	npctype_id = in_npctypeid;
	SetPlayerKillItemID(0);
	char_id = 0;
	corpse_db_id = 0;
	player_corpse_depop = false;
	strcpy(corpse_name, in_npc->GetName());
	strcpy(name, in_npc->GetName());

	for(int count = 0; count < 100; count++) {
		if (
			EQ::ValueWithin(
				level,
				npcCorpseDecayTimes[count].minlvl,
				npcCorpseDecayTimes[count].maxlvl
			)
		) {
			corpse_decay_timer.SetTimer(npcCorpseDecayTimes[count].seconds*1000);
			break;
		}
	}

	if (IsEmpty()) {
		corpse_decay_timer.SetTimer(RuleI(NPC,EmptyNPCCorpseDecayTimeMS)+1000);
	}


	if (in_npc->HasPrivateCorpse()) {
		corpse_delay_timer.SetTimer(corpse_decay_timer.GetRemainingTime() + 1000);
	}

	for (int i = 0; i < MAX_LOOTERS; i++){
		allowed_looters[i] = 0;
	}

	rez_experience = 0;

	UpdateEquipmentLight();
	UpdateActiveLight();

	loot_request_type = LootRequestType::Forbidden;
}

Corpse::Corpse(Client* client, int32 in_rezexp) : Mob (
	"Unnamed_Corpse", // in_name
	"", // in_lastname
	0, // in_cur_hp
	0, // in_max_hp
	client->GetGender(), // in_gender
	client->GetRace(), // in_race
	client->GetClass(), // in_class
	BT_Humanoid, // in_bodytype
	client->GetDeity(), // in_deity
	client->GetLevel(), // in_level
	0, // in_npctype_id
	client->GetSize(), // in_size
	0, // in_runspeed
	client->GetPosition(), // position
	client->GetInnateLightType(), // in_light
	client->GetTexture(), // in_texture
	client->GetHelmTexture(), // in_helmtexture
	0, // in_ac
	0, // in_atk
	0, // in_str
	0, // in_sta
	0, // in_dex
	0, // in_agi
	0, // in_int
	0, // in_wis
	0, // in_cha
	client->GetPP().haircolor, // in_haircolor
	client->GetPP().beardcolor, // in_beardcolor
	client->GetPP().eyecolor1, // in_eyecolor1
	client->GetPP().eyecolor2, // in_eyecolor2
	client->GetPP().hairstyle, // in_hairstyle
	client->GetPP().face, // in_luclinface
	client->GetPP().beard, // in_beard
	client->GetPP().drakkin_heritage, // in_drakkin_heritage
	client->GetPP().drakkin_tattoo, // in_drakkin_tattoo
	client->GetPP().drakkin_details, // in_drakkin_details
	EQ::TintProfile(), // in_armor_tint
	0xff, // in_aa_title
	0, // in_see_invis
	0, // in_see_invis_undead
	0, // in_see_hide
	0, // in_see_improved_hide
	0, // in_hp_regen
	0, // in_mana_regen
	0, // in_qglobal
	0, // in_maxlevel
	0, // in_scalerate
	0, // in_armtexture
	0, // in_bracertexture
	0, // in_handtexture
	0, // in_legtexture
	0, // in_feettexture
	0, // in_usemodel
	false, // in_always_aggro
	0, // in_heroic_strikethrough
	false // in_keeps_sold_items
),
	corpse_decay_timer(RuleI(Character, CorpseDecayTimeMS)),
	corpse_rez_timer(RuleI(Character, CorpseResTimeMS)),
	corpse_delay_timer(RuleI(NPC, CorpseUnlockTimer)),
	corpse_graveyard_timer(RuleI(Zone, GraveyardTimeMS)),
	loot_cooldown_timer(10)
{
	PlayerProfile_Struct *pp = &client->GetPP();
	EQ::ItemInstance *item = nullptr;

	if (!zone->HasGraveyard()) {
		corpse_graveyard_timer.Disable();
	}

	for (int i = 0; i < MAX_LOOTERS; i++){
		allowed_looters[i] = 0;
	}

	if (client->AutoConsentGroupEnabled()) {
		auto* g = client->GetGroup();
		consented_group_id = g ? g->GetID() : 0;
	}

	if (client->AutoConsentRaidEnabled()) {
		auto* r = client->GetRaid();
		consented_raid_id = r ? r->GetID() : 0;
	}

	consented_guild_id = client->AutoConsentGuildEnabled() ? client->GuildID() : 0;

	is_corpse_changed    = true;
	rez_experience       = in_rezexp;
	is_player_corpse     = true;
	is_locked            = false;
	being_looted_by      = 0xFFFFFFFF;
	char_id              = client->CharacterID();
	corpse_db_id         = 0;
	player_corpse_depop  = false;
	copper               = 0;
	silver               = 0;
	gold                 = 0;
	platinum             = 0;

	strcpy(corpse_name, pp->name);
	strcpy(name, pp->name);

	/* become_npc was not being initialized which led to some pretty funky things with newly created corpses */
	become_npc = false;

	SetPlayerKillItemID(0);

	/* Check Rule to see if we can leave corpses */
	if (
		(!RuleB(Character, LeaveNakedCorpses) || RuleB(Character, LeaveCorpses)) &&
		GetLevel() >= RuleI(Character, DeathItemLossLevel)
	) {
		// cash
		// Let's not move the cash when 'RespawnFromHover = true' && 'client->GetClientVersion() < EQClientSoF' since the client doesn't.
		// (change to first client that supports 'death hover' mode, if not SoF.)
		if (
			!RuleB(Character, RespawnFromHover) ||
			client->ClientVersion() < EQ::versions::ClientVersion::SoF
		) {
			auto corpse_copper   = pp->copper;
			auto corpse_silver   = pp->silver;
			auto corpse_gold     = pp->gold;
			auto corpse_platinum = pp->platinum;

			pp->copper   = 0;
			pp->silver   = 0;
			pp->gold     = 0;
			pp->platinum = 0;

			if (RuleB(Character, LeaveCursorMoneyOnCorpse)) {
				corpse_copper += pp->copper_cursor;
				corpse_silver += pp->silver_cursor;
				corpse_gold += pp->gold_cursor;
				corpse_platinum += pp->platinum_cursor;

				pp->copper_cursor   = 0;
				pp->silver_cursor   = 0;
				pp->gold_cursor     = 0;
				pp->platinum_cursor = 0;
			}

			SetCash(corpse_copper, corpse_silver, corpse_gold, corpse_platinum);
		}

		// get their tints
		memcpy(&item_tint.Slot, &client->GetPP().item_tint, sizeof(item_tint));

		// TODO soulbound items need not be added to corpse, but they need
		// to go into the regular slots on the player, out of bags
		std::list<uint32> removed_list;

		// ideally, we would start at invslot::slotGeneral1 and progress to invslot::slotCursor..
		// ..then regress and process invslot::EQUIPMENT_BEGIN through invslot::EQUIPMENT_END...
		// without additional work to database loading of player corpses, this order is not
		// currently preserved and a re-work of this processing loop is not warranted.
		for (int i = EQ::invslot::POSSESSIONS_BEGIN; i <= EQ::invslot::POSSESSIONS_END; ++i) {
			item = client->GetInv().GetItem(i);
			if (!item) {
				continue;
			}

			if (
				!client->IsBecomeNPC() ||
				(
					client->IsBecomeNPC() &&
					!item->GetItem()->NoRent
				)
			) {
				MoveItemToCorpse(client, item, i, removed_list);
			}
		}

		database.TransactionBegin();

		// this should not be modified to include the entire range of invtype::TYPE_POSSESSIONS slots by default..
		// ..due to the possibility of 'hidden' items from client version bias..or, possibly, soul-bound items (WoW?)
		if (!removed_list.empty()) {
			std::list<uint32>::const_iterator iter = removed_list.begin();

			if (iter != removed_list.end()) {
				std::stringstream ss("");
				ss << "DELETE FROM `inventory` WHERE `charid` = " << client->CharacterID();
				ss << " AND `slotid` IN (" << (*iter);
				++iter;

				while (iter != removed_list.end()) {
					ss << ", " << (*iter);
					++iter;
				}
				ss << ")";

				database.QueryDatabase(ss.str().c_str());
			}
		}

		auto start = client->GetInv().cursor_cbegin();
		auto finish = client->GetInv().cursor_cend();
		database.SaveCursor(client->CharacterID(), start, finish);

		client->CalcBonuses();
		client->Save();

		IsRezzed(false);
		Save();

		database.TransactionCommit();

		UpdateEquipmentLight();
		UpdateActiveLight();

		return;
	}

	UpdateEquipmentLight();
	UpdateActiveLight();

	loot_request_type = LootRequestType::Forbidden;

	IsRezzed(false);
	Save();
}

void Corpse::MoveItemToCorpse(Client *client, EQ::ItemInstance *inst, int16 equipSlot, std::list<uint32> &removedList)
{
	AddItem(
		inst->GetItem()->ID,
		inst->GetCharges(),
		equipSlot,
		inst->GetAugmentItemID(0),
		inst->GetAugmentItemID(1),
		inst->GetAugmentItemID(2),
		inst->GetAugmentItemID(3),
		inst->GetAugmentItemID(4),
		inst->GetAugmentItemID(5),
		inst->IsAttuned(),
		inst->GetCustomDataString(),
		inst->GetOrnamentationIcon(),
		inst->GetOrnamentationIDFile(),
		inst->GetOrnamentHeroModel()
	);

	removedList.push_back(equipSlot);

	while (true) {
		if (!inst->IsClassBag()) { break; }
		if (equipSlot < EQ::invslot::GENERAL_BEGIN || equipSlot > EQ::invslot::slotCursor) { break; }

		for (int16 sub_index = EQ::invbag::SLOT_BEGIN; sub_index <= EQ::invbag::SLOT_END; ++sub_index) {
			int16 real_bag_slot = EQ::InventoryProfile::CalcSlotId(equipSlot, sub_index);
			auto bag_inst = client->GetInv().GetItem(real_bag_slot);
			if (bag_inst == nullptr) { continue; }

			AddItem(
				bag_inst->GetItem()->ID,
				bag_inst->GetCharges(),
				real_bag_slot,
				bag_inst->GetAugmentItemID(0),
				bag_inst->GetAugmentItemID(1),
				bag_inst->GetAugmentItemID(2),
				bag_inst->GetAugmentItemID(3),
				bag_inst->GetAugmentItemID(4),
				bag_inst->GetAugmentItemID(5),
				bag_inst->IsAttuned(),
				bag_inst->GetCustomDataString(),
				bag_inst->GetOrnamentationIcon(),
				bag_inst->GetOrnamentationIDFile(),
				bag_inst->GetOrnamentHeroModel()
			);

			removedList.push_back(real_bag_slot);
			client->DeleteItemInInventory(real_bag_slot, 0, true, false);
		}
		break;
	}
	client->DeleteItemInInventory(equipSlot, 0, true, false);
}

// To be called from LoadFromDBData
Corpse::Corpse(uint32 in_dbid, uint32 in_charid, const char* in_charname, ItemList* in_itemlist, uint32 in_copper, uint32 in_silver, uint32 in_gold, uint32 in_plat, const glm::vec4& position, float in_size, uint8 in_gender, uint16 in_race, uint8 in_class, uint8 in_deity, uint8 in_level, uint8 in_texture, uint8 in_helmtexture,uint32 in_rezexp, bool wasAtGraveyard) : Mob(
	"Unnamed_Corpse", // in_name
	"", // in_lastname
	0, // in_cur_hp
	0, // in_max_hp
	in_gender, // in_gender
	in_race, // in_race
	in_class, // in_class
	BT_Humanoid, // in_bodytype
	in_deity, // in_deity
	in_level, // in_level
	0, // in_npctype_id
	in_size, // in_size
	0.0f, // in_runspeed
	position, // position
	0, // in_light
	in_texture, // in_texture
	in_helmtexture, // in_helmtexture
	0, // in_ac
	0, // in_atk
	0, // in_str
	0, // in_sta
	0, // in_dex
	0, // in_agi
	0, // in_int
	0, // in_wis
	0, // in_cha
	0, // in_haircolor
	0, // in_beardcolor
	0, // in_eyecolor1
	0, // in_eyecolor2
	0, // in_hairstyle
	0, // in_luclinface
	0, // in_beard
	0, // in_drakkin_heritage
	0, // in_drakkin_tattoo
	0, // in_drakkin_details
	EQ::TintProfile(), // in_armor_tint
	0xFF, // in_aa_title
	0, // in_see_invis
	0, // in_see_invis_undead
	0, // in_see_hide
	0, // in_see_improved_hide
	0, // in_hp_regen
	0, // in_mana_regen
	0, // in_qglobal
	0, // in_maxlevel
	0, // in_scalerate
	0, // in_armtexture
	0, // in_bracertexture
	0, // in_handtexture
	0, // in_legtexture
	0, // in_feettexture
	0, // in_usemodel
	false, // in_always_aggros_foes
	0, // in_heroic_strikethrough
	false // in_keeps_sold_items
),
	corpse_decay_timer(RuleI(Character, CorpseDecayTimeMS)),
	corpse_rez_timer(RuleI(Character, CorpseResTimeMS)),
	corpse_delay_timer(RuleI(NPC, CorpseUnlockTimer)),
	corpse_graveyard_timer(RuleI(Zone, GraveyardTimeMS)),
	loot_cooldown_timer(10)
{
	LoadPlayerCorpseDecayTime(in_dbid);

	if (!zone->HasGraveyard() || wasAtGraveyard) {
		corpse_graveyard_timer.Disable();
	}

	is_corpse_changed = false;
	is_player_corpse = true;
	is_locked = false;
	being_looted_by = 0xFFFFFFFF;
	corpse_db_id = in_dbid;
	player_corpse_depop = false;
	char_id = in_charid;
	itemlist = *in_itemlist;
	in_itemlist->clear();

	strcpy(corpse_name, in_charname);
	strcpy(name, in_charname);

	copper = in_copper;
	silver = in_silver;
	gold = in_gold;
	platinum = in_plat;

	rez_experience = in_rezexp;

	for (int i = 0; i < MAX_LOOTERS; i++){
		allowed_looters[i] = 0;
	}

	SetPlayerKillItemID(0);

	UpdateEquipmentLight();
	UpdateActiveLight();

	loot_request_type = LootRequestType::Forbidden;
}

Corpse::~Corpse() {
	if (is_player_corpse && !(player_corpse_depop && corpse_db_id == 0)) {
		Save();
	}
	ItemList::iterator cur,end;
	cur = itemlist.begin();
	end = itemlist.end();
	for(; cur != end; ++cur) {
		ServerLootItem_Struct* item = *cur;
		safe_delete(item);
	}
	itemlist.clear();
}

/*
this needs to be called AFTER the entity_id is set
the client does this too, so it's unchangable
*/
void Corpse::CalcCorpseName() {
	EntityList::RemoveNumbers(name);
	char tmp[64];
	if (is_player_corpse){
		snprintf(tmp, sizeof(tmp), "'s corpse%d", GetID());
	}
	else{
		snprintf(tmp, sizeof(tmp), "`s_corpse%d", GetID());
	}
	name[(sizeof(name) - 1) - strlen(tmp)] = 0;
	strcat(name, tmp);
}

bool Corpse::Save() {
	if (!is_player_corpse) {
		return true;
	}

	if (!is_corpse_changed) {
		return true;
	}

	CharacterCorpseEntry ce;

	ce.size             = size;
	ce.locked           = is_locked;
	ce.copper           = copper;
	ce.silver           = silver;
	ce.gold             = gold;
	ce.plat             = platinum;
	ce.race             = race;
	ce.class_           = class_;
	ce.gender           = gender;
	ce.deity            = deity;
	ce.level            = level;
	ce.texture          = texture;
	ce.helmtexture      = helmtexture;
	ce.exp              = rez_experience;
	ce.item_tint        = item_tint;
	ce.haircolor        = haircolor;
	ce.beardcolor       = beardcolor;
	ce.eyecolor2        = eyecolor1;
	ce.hairstyle        = hairstyle;
	ce.face             = luclinface;
	ce.beard            = beard;
	ce.drakkin_heritage = drakkin_heritage;
	ce.drakkin_tattoo   = drakkin_tattoo;
	ce.drakkin_details  = drakkin_details;

	for (auto &item: itemlist) {
		CharacterCorpseItemEntry e;

		e.item_id             = item->item_id;
		e.equip_slot          = item->equip_slot;
		e.charges             = item->charges;
		e.lootslot            = item->lootslot;
		e.aug_1               = item->aug_1;
		e.aug_2               = item->aug_2;
		e.aug_3               = item->aug_3;
		e.aug_4               = item->aug_4;
		e.aug_5               = item->aug_5;
		e.aug_6               = item->aug_6;
		e.attuned             = item->attuned;
		e.custom_data         = item->custom_data;
		e.ornamenticon        = item->ornamenticon;
		e.ornamentidfile      = item->ornamentidfile;
		e.ornament_hero_model = item->ornament_hero_model;

		ce.items.emplace_back(std::move(e));
	}

	/* Create New Corpse*/
	if (corpse_db_id == 0) {
		corpse_db_id = database.SaveCharacterCorpse(char_id, corpse_name, zone->GetZoneID(), zone->GetInstanceID(), ce, m_Position, consented_guild_id);
	}
	/* Update Corpse Data */
	else{
		corpse_db_id = database.UpdateCharacterCorpse(corpse_db_id, char_id, corpse_name, zone->GetZoneID(), zone->GetInstanceID(), ce, m_Position, consented_guild_id, IsRezzed());
	}

	return true;
}

void Corpse::Delete() {
	if (IsPlayerCorpse() && corpse_db_id != 0)
		database.DeleteCharacterCorpse(corpse_db_id);

	corpse_db_id = 0;
	player_corpse_depop = true;
}

void Corpse::Bury() {
	if (IsPlayerCorpse() && corpse_db_id != 0)
		database.BuryCharacterCorpse(corpse_db_id);
	corpse_db_id = 0;
	player_corpse_depop = true;
}

void Corpse::DepopNPCCorpse() {
	if (IsNPCCorpse())
		player_corpse_depop = true;
}

void Corpse::DepopPlayerCorpse() {
	player_corpse_depop = true;
}

void Corpse::AddConsentName(std::string consent_player_name)
{
	for (const auto& consented_player_name : consented_player_names) {
		if (strcasecmp(consented_player_name.c_str(), consent_player_name.c_str()) == 0) {
			return;
		}
	}
	consented_player_names.emplace_back(consent_player_name);
}

void Corpse::RemoveConsentName(std::string consent_player_name)
{
	consented_player_names.erase(std::remove_if(consented_player_names.begin(), consented_player_names.end(),
		[consent_player_name](const std::string& consented_player_name) {
			return strcasecmp(consented_player_name.c_str(), consent_player_name.c_str()) == 0;
		}
	), consented_player_names.end());
}

uint32 Corpse::CountItems() {
	return itemlist.size();
}

void Corpse::AddItem(uint32 itemnum,
	uint16 charges,
	int16 slot,
	uint32 aug1,
	uint32 aug2,
	uint32 aug3,
	uint32 aug4,
	uint32 aug5,
	uint32 aug6,
	bool attuned,
	const std::string& custom_data,
	uint32 ornamenticon,
	uint32 ornamentidfile,
	uint32 ornament_hero_model) {

	if (!database.GetItem(itemnum))
		return;

	is_corpse_changed = true;

	auto item = new ServerLootItem_Struct;

	item->item_id = itemnum;
	item->charges = charges;
	item->equip_slot = slot;
	item->aug_1 = aug1;
	item->aug_2 = aug2;
	item->aug_3 = aug3;
	item->aug_4 = aug4;
	item->aug_5 = aug5;
	item->aug_6 = aug6;
	item->attuned = attuned;
	item->custom_data = custom_data;
	item->ornamenticon = ornamenticon;
	item->ornamentidfile = ornamentidfile;
	item->ornament_hero_model = ornament_hero_model;

	itemlist.push_back(item);

	UpdateEquipmentLight();
}

ServerLootItem_Struct* Corpse::GetItem(uint16 lootslot, ServerLootItem_Struct** bag_item_data) {
	ServerLootItem_Struct *sitem = nullptr, *sitem2 = nullptr;

	ItemList::iterator cur,end;
	cur = itemlist.begin();
	end = itemlist.end();
	for(; cur != end; ++cur) {
		if((*cur)->lootslot == lootslot) {
			sitem = *cur;
			break;
		}
	}

	if (sitem && bag_item_data && EQ::InventoryProfile::SupportsContainers(sitem->equip_slot)) {
		int16 bagstart = EQ::InventoryProfile::CalcSlotId(sitem->equip_slot, EQ::invbag::SLOT_BEGIN);

		cur = itemlist.begin();
		end = itemlist.end();
		for (; cur != end; ++cur) {
			sitem2 = *cur;
			if (sitem2->equip_slot >= bagstart && sitem2->equip_slot < bagstart + 10) {
				bag_item_data[sitem2->equip_slot - bagstart] = sitem2;
			}
		}
	}

	return sitem;
}

uint32 Corpse::GetWornItem(int16 equipSlot) const {
	ItemList::const_iterator cur,end;
	cur = itemlist.begin();
	end = itemlist.end();
	for(; cur != end; ++cur) {
		ServerLootItem_Struct* item = *cur;
		if (item->equip_slot == equipSlot) {
			return item->item_id;
		}
	}

	return 0;
}

void Corpse::RemoveItem(uint16 lootslot) {
	if (lootslot == 0xFFFF)
		return;

	ItemList::iterator cur,end;
	cur = itemlist.begin();
	end = itemlist.end();
	for (; cur != end; ++cur) {
		ServerLootItem_Struct* sitem = *cur;
		if (sitem->lootslot == lootslot) {
			RemoveItem(sitem);
			return;
		}
	}
}

void Corpse::RemoveItem(ServerLootItem_Struct* item_data)
{
	for (auto iter = itemlist.begin(); iter != itemlist.end(); ++iter) {
		auto sitem = *iter;
		if (sitem != item_data) { continue; }

		is_corpse_changed = true;
		itemlist.erase(iter);

		uint8 material = EQ::InventoryProfile::CalcMaterialFromSlot(sitem->equip_slot); // autos to unsigned char
		if (material != EQ::textures::materialInvalid)
			SendWearChange(material);

		UpdateEquipmentLight();
		if (UpdateActiveLight())
			SendAppearancePacket(AppearanceType::Light, GetActiveLightType());

		safe_delete(sitem);
		return;
	}
}

void Corpse::RemoveItemByID(uint32 item_id, int quantity) {
	if (!database.GetItem(item_id)) {
		return;
	}

	if (!HasItem(item_id)) {
		return;
	}

	int removed_count = 0;
	for (auto current_item = itemlist.begin(); current_item != itemlist.end(); ++current_item) {
		ServerLootItem_Struct* sitem = *current_item;
		if (removed_count == quantity) {
			break;
		}

		if (sitem && sitem->item_id == item_id) {
			int stack_size = sitem->charges > 1 ? sitem->charges : 1;
			if ((removed_count + stack_size) <= quantity) {
				removed_count += stack_size;
				is_corpse_changed = true;
				itemlist.erase(current_item);
			} else {
				int amount_left = (quantity - removed_count);
				if (amount_left > 0) {
					if (stack_size > amount_left) {
						removed_count += amount_left;
						sitem->charges -= amount_left;
						is_corpse_changed = true;
					} else if (stack_size == amount_left) {
						removed_count += amount_left;
						itemlist.erase(current_item);
					}
				}
			}
		}
	}
}

void Corpse::SetCash(uint32 in_copper, uint32 in_silver, uint32 in_gold, uint32 in_platinum) {
	copper = in_copper;
	silver = in_silver;
	gold = in_gold;
	platinum = in_platinum;
	is_corpse_changed = true;
}

void Corpse::RemoveCash() {
	copper = 0;
	silver = 0;
	gold = 0;
	platinum = 0;
	is_corpse_changed = true;
}

bool Corpse::IsEmpty() const {
	if (copper != 0 || silver != 0 || gold != 0 || platinum != 0)
		return false;

	return itemlist.empty();
}

bool Corpse::Process() {
	if (player_corpse_depop)
		return false;

	if (corpse_delay_timer.Check()) {
		for (int i = 0; i < MAX_LOOTERS; i++)
			allowed_looters[i] = 0;
		corpse_delay_timer.Disable();
		return true;
	}

	if (corpse_graveyard_timer.Check()) {
		MovePlayerCorpseToGraveyard();
		corpse_graveyard_timer.Disable();
		return false;
	}
	/*
	if(corpse_res_timer.Check()) {
		can_rez = false;
		corpse_res_timer.Disable();
	}
	*/

	/* This is when a corpse hits decay timer and does checks*/
	if (corpse_decay_timer.Check()) {
		/* NPC */
		if (IsNPCCorpse()){
			corpse_decay_timer.Disable();
			return false;
		}
		/* Client */
		if (!RuleB(Zone, EnableShadowrest)){
			Delete();
		}
		else {
			if (database.BuryCharacterCorpse(corpse_db_id)) {
				Save();
				player_corpse_depop = true;
				corpse_db_id = 0;
				LogDebug("Tagged [{}] player corpse has buried", GetName());
			}
			else {
				LogError("Unable to bury [{}] player corpse", GetName());
				return true;
			}
		}
		corpse_decay_timer.Disable();
		return false;
	}

	return true;
}

void Corpse::ResetDecayTimer()
{
	int decay_ms = level > 54 ? RuleI(NPC, MajorNPCCorpseDecayTimeMS) : RuleI(NPC, MinorNPCCorpseDecayTimeMS);

	if (IsPlayerCorpse())
	{
		decay_ms = RuleI(Character, CorpseDecayTimeMS);
	}
	else if (IsEmpty())
	{
		decay_ms = RuleI(NPC, EmptyNPCCorpseDecayTimeMS) + 1000;
	}
	else
	{
		for (const npcDecayTimes_Struct& decay_time : npcCorpseDecayTimes)
		{
			if (level >= decay_time.minlvl && level <= decay_time.maxlvl)
			{
				decay_ms = decay_time.seconds * 1000;
				break;
			}
		}
	}

	corpse_decay_timer.SetTimer(decay_ms);
}

void Corpse::SetDecayTimer(uint32 decaytime) {
	if (decaytime == 0)
		corpse_decay_timer.Trigger();
	else
		corpse_decay_timer.Start(decaytime);
}

bool Corpse::CanPlayerLoot(int charid) {
	uint8 looters = 0;
	for (int i = 0; i < MAX_LOOTERS; i++) {
		if (allowed_looters[i] != 0){
			looters++;
		}

		if (allowed_looters[i] == charid)
			return true;
	}
	/* If we have no looters, obviously client can loot */
	return looters == 0;
}

void Corpse::AllowPlayerLoot(Mob *them, uint8 slot) {
	if(slot >= MAX_LOOTERS)
		return;
	if(them == nullptr || !them->IsClient())
		return;

	allowed_looters[slot] = them->CastToClient()->CharacterID();
}

void Corpse::MakeLootRequestPackets(Client* client, const EQApplicationPacket* app) {
	if (!client) {
		return;
	}

	// Added 12/08. Started compressing loot struct on live.
	if (player_corpse_depop) {
		SendLootReqErrorPacket(client, LootResponse::SomeoneElse);
		return;
	}

	if (IsPlayerCorpse() && !corpse_db_id) { // really should try to resave in this case
		// SendLootReqErrorPacket(client, 0);
		client->Message(Chat::Red, "Warning: Corpse's dbid = 0! Corpse will not survive zone shutdown!");
		std::cout << "Error: PlayerCorpse::MakeLootRequestPackets: dbid = 0!" << std::endl;
		// return;
	}

	if (is_locked && client->Admin() < AccountStatus::GMAdmin) {
		SendLootReqErrorPacket(client, LootResponse::SomeoneElse);
		client->Message(Chat::Red, "Error: Corpse locked by GM.");
		return;
	}

	if (!being_looted_by || (being_looted_by != 0xFFFFFFFF && !entity_list.GetID(being_looted_by))) {
		being_looted_by = 0xFFFFFFFF;
	}

	if (DistanceSquaredNoZ(client->GetPosition(), m_Position) > 625) {
		SendLootReqErrorPacket(client, LootResponse::TooFar);
		return;
	}

	if (being_looted_by != 0xFFFFFFFF && being_looted_by != client->GetID()) {
		SendLootReqErrorPacket(client, LootResponse::SomeoneElse);
		return;
	}

	// all loot session disqualifiers should occur before this point as not to interfere with any current looter
	loot_request_type = LootRequestType::Forbidden;

	// loot_request_type is scoped to class Corpse and reset on a per-loot session basis
	if (client->GetGM()) {
		if (client->Admin() >= AccountStatus::GMAdmin) {
			loot_request_type = LootRequestType::GMAllowed;

		} else {
			loot_request_type = LootRequestType::GMPeek;
		}
	}
	else {
		if (IsPlayerCorpse()) {
			if (char_id == client->CharacterID()) {
				loot_request_type = LootRequestType::Self;
			}

			else if (CanPlayerLoot(client->CharacterID())) {
				if (GetPlayerKillItem() == -1) {
					loot_request_type = LootRequestType::AllowedPVPAll;

				} else if (GetPlayerKillItem() == 1) {
					loot_request_type = LootRequestType::AllowedPVPSingle;

				} else if (GetPlayerKillItem() > 1) {
					loot_request_type = LootRequestType::AllowedPVPDefined;
				}
			}
		}
		else if ((IsNPCCorpse() || become_npc) && CanPlayerLoot(client->CharacterID())) {
			loot_request_type = LootRequestType::AllowedPVE;
		}

	}

	LogInventory("MakeLootRequestPackets() LootRequestType [{}] for [{}]", (int) loot_request_type, client->GetName());

	if (loot_request_type == LootRequestType::Forbidden) {
		SendLootReqErrorPacket(client, LootResponse::NotAtThisTime);
		return;
	}

	being_looted_by = client->GetID();
	client->CommonBreakInvisible(); // we should be "all good" so lets break invis now instead of earlier before all error checking is done

	// process coin
	bool loot_coin = false;
	std::string tmp;
	if (database.GetVariable("LootCoin", tmp)) {
		loot_coin = (tmp[0] == 1 && tmp[1] == '\0');
	}

	if (loot_request_type == LootRequestType::GMPeek || loot_request_type == LootRequestType::GMAllowed) {
		if (
			GetPlatinum() ||
			GetGold() ||
			GetSilver() ||
			GetCopper()
		) {
			client->Message(
				Chat::Yellow,
				fmt::format(
					"This corpse contains {}.",
					Strings::Money(
						GetPlatinum(),
						GetGold(),
						GetSilver(),
						GetCopper()
					)
				).c_str()
			);
		} else {
			client->Message(Chat::Yellow, "This corpse contains no money.");
		}

		auto outapp = new EQApplicationPacket(OP_MoneyOnCorpse, sizeof(moneyOnCorpseStruct));
		moneyOnCorpseStruct* d = (moneyOnCorpseStruct*)outapp->pBuffer;

		d->response = static_cast<uint8>(LootResponse::Normal);
		d->unknown1 = 0x42;
		d->unknown2 = 0xef;

		d->copper = 0;
		d->silver = 0;
		d->gold = 0;
		d->platinum = 0;

		outapp->priority = 6;
		client->QueuePacket(outapp);

		safe_delete(outapp);
	}
	else {
		auto outapp = new EQApplicationPacket(OP_MoneyOnCorpse, sizeof(moneyOnCorpseStruct));
		moneyOnCorpseStruct* d = (moneyOnCorpseStruct*)outapp->pBuffer;

		d->response = static_cast<uint8>(LootResponse::Normal);
		d->unknown1 = 0x42;
		d->unknown2 = 0xef;

		Group* cgroup = client->GetGroup();

		// this can be reworked into a switch and/or massaged to include specialized pve loot rules based on 'LootRequestType'
		if (!IsPlayerCorpse() && client->IsGrouped() && client->AutoSplitEnabled() && cgroup) {
			d->copper = 0;
			d->silver = 0;
			d->gold = 0;
			d->platinum = 0;
			cgroup->SplitMoney(GetCopper(), GetSilver(), GetGold(), GetPlatinum(), client);
		}
		else {
			d->copper = GetCopper();
			d->silver = GetSilver();
			d->gold = GetGold();
			d->platinum = GetPlatinum();
			client->AddMoneyToPP(GetCopper(), GetSilver(), GetGold(), GetPlatinum());
		}

		RemoveCash();
		Save();

		outapp->priority = 6;
		client->QueuePacket(outapp);

		safe_delete(outapp);
	}

	// process items
	auto timestamps = database.GetItemRecastTimestamps(client->CharacterID());

	if (loot_request_type == LootRequestType::AllowedPVPDefined) {
		auto pkitemid = GetPlayerKillItem();
		auto pkitem = database.GetItem(pkitemid);
		auto pkinst = database.CreateItem(pkitem, pkitem->MaxCharges);

		if (pkinst) {
			if (pkitem->RecastDelay) {
				if (pkitem->RecastType != RECAST_TYPE_UNLINKED_ITEM) {
					pkinst->SetRecastTimestamp(timestamps.count(pkitem->RecastType) ? timestamps.at(pkitem->RecastType) : 0);
				} else {
					pkinst->SetRecastTimestamp(timestamps.count(pkitem->ID) ? timestamps.at(pkitem->ID) : 0);
				}
			}

			LogInventory("MakeLootRequestPackets() Slot [{}], Item [{}]", EQ::invslot::CORPSE_BEGIN, pkitem->Name);

			client->SendItemPacket(EQ::invslot::CORPSE_BEGIN, pkinst, ItemPacketLoot);
			safe_delete(pkinst);
		}
		else {
			LogInventory("MakeLootRequestPackets() PlayerKillItem [{}] not found", pkitemid);

			client->Message(Chat::Red, "PlayerKillItem (id: %i) could not be found!", pkitemid);
		}

		client->QueuePacket(app);
		return;
	}

	auto loot_slot = EQ::invslot::CORPSE_BEGIN;
	auto corpse_mask = client->GetInv().GetLookup()->CorpseBitmask;

	for (auto item_data : itemlist) {
		// every loot session must either set all items' lootslots to 'invslot::SLOT_INVALID'
		// or to a valid enumerated client-versioned corpse slot (lootslot is not equip_slot)
		item_data->lootslot = 0xFFFF;

		// align server and client corpse slot mappings so translators can function properly
		while (loot_slot <= EQ::invslot::CORPSE_END && (((uint64)1 << loot_slot) & corpse_mask) == 0)
			++loot_slot;
		if (loot_slot > EQ::invslot::CORPSE_END)
			continue;

		if (IsPlayerCorpse()) {
			if (loot_request_type == LootRequestType::AllowedPVPSingle && loot_slot != EQ::invslot::CORPSE_BEGIN)
				continue;

			if (item_data->equip_slot < EQ::invslot::POSSESSIONS_BEGIN || item_data->equip_slot > EQ::invslot::POSSESSIONS_END)
				continue;
		}

		const auto *item = database.GetItem(item_data->item_id);
		auto inst = database.CreateItem(
			item,
			item_data->charges,
			item_data->aug_1,
			item_data->aug_2,
			item_data->aug_3,
			item_data->aug_4,
			item_data->aug_5,
			item_data->aug_6,
			item_data->attuned,
			item_data->custom_data,
			item_data->ornamenticon,
			item_data->ornamentidfile,
			item_data->ornament_hero_model
		);
		if (!inst)
			continue;

		if (item->RecastDelay) {
			if (item->RecastType != RECAST_TYPE_UNLINKED_ITEM) {
				inst->SetRecastTimestamp(timestamps.count(item->RecastType) ? timestamps.at(item->RecastType) : 0);
			} else {
				inst->SetRecastTimestamp(timestamps.count(item->ID) ? timestamps.at(item->ID) : 0);
			}
		}

		LogInventory("MakeLootRequestPackets() Slot [{}], Item [{}]", loot_slot, item->Name);

		client->SendItemPacket(loot_slot, inst, ItemPacketLoot);
		safe_delete(inst);

		item_data->lootslot = loot_slot++;
	}

	// Disgrace: Client seems to require that we send the packet back...
	client->QueuePacket(app);

	// This is required for the 'Loot All' feature to work for SoD clients. I expect it is to tell the client that the
	// server has now sent all the items on the corpse.
	if (client->ClientVersion() >= EQ::versions::ClientVersion::SoD)
		SendLootReqErrorPacket(client, LootResponse::LootAll);
}

void Corpse::LootItem(Client *client, const EQApplicationPacket *app)
{
	if (!client) {
		return;
	}

	auto lootitem = (LootingItem_Struct *)app->pBuffer;

	LogInventory("LootItem() LootRequestType [{}], Slot [{}] for [{}]", (int) loot_request_type, lootitem->slot_id, client->GetName());

	if (loot_request_type < LootRequestType::GMAllowed) { // LootRequestType::Forbidden and LootRequestType::GMPeek
		client->QueuePacket(app);
		SendEndLootErrorPacket(client);
		// unlock corpse for others

		if (IsBeingLootedBy(client)) {
			ResetLooter();
		}
		return;
	}

	if (!loot_cooldown_timer.Check()) {
		client->QueuePacket(app);
		SendEndLootErrorPacket(client);
		// unlock corpse for others
		if (IsBeingLootedBy(client)) {
			ResetLooter();
		}
		return;
	}

	/* To prevent item loss for a player using 'Loot All' who doesn't have inventory space for all their items. */
	if (RuleB(Character, CheckCursorEmptyWhenLooting) && !client->GetInv().CursorEmpty()) {
		client->Message(Chat::Red, "You may not loot an item while you have an item on your cursor.");
		client->QueuePacket(app);
		SendEndLootErrorPacket(client);
		/* Unlock corpse for others */
		if (IsBeingLootedBy(client)) {
			ResetLooter();
		}
		return;
	}

	if (!IsBeingLootedBy(client)) {
		client->QueuePacket(app);
		SendEndLootErrorPacket(client);
		return;
	}

	if (IsPlayerCorpse() && !CanPlayerLoot(client->CharacterID()) && !become_npc &&
		(char_id != client->CharacterID() && client->Admin() < AccountStatus::GMLeadAdmin)) {
		client->Message(Chat::Red, "Error: This is a player corpse and you dont own it.");
		client->QueuePacket(app);
		SendEndLootErrorPacket(client);
		return;
	}

	if (is_locked && client->Admin() < AccountStatus::GMAdmin) {
		client->QueuePacket(app);
		SendLootReqErrorPacket(client, LootResponse::SomeoneElse);
		client->Message(Chat::Red, "Error: Corpse locked by GM.");
		return;
	}

	if (IsPlayerCorpse() && (char_id != client->CharacterID()) && CanPlayerLoot(client->CharacterID()) &&
		GetPlayerKillItem() == 0) {
		client->Message(Chat::Red, "Error: You cannot loot any more items from this corpse.");
		client->QueuePacket(app);
		SendEndLootErrorPacket(client);
		ResetLooter();
		return;
	}

	const EQ::ItemData *item = nullptr;
	EQ::ItemInstance *inst = nullptr;
	ServerLootItem_Struct *item_data = nullptr, *bag_item_data[10] = {};

	memset(bag_item_data, 0, sizeof(bag_item_data));
	if (GetPlayerKillItem() > 1) {
		item = database.GetItem(GetPlayerKillItem());
	}
	else if (GetPlayerKillItem() == -1 || GetPlayerKillItem() == 1) {
		item_data =
			GetItem(lootitem->slot_id); // dont allow them to loot entire bags of items as pvp reward
	}
	else {
		item_data = GetItem(lootitem->slot_id, bag_item_data);
	}

	if (GetPlayerKillItem() <= 1 && item_data != 0) {
		item = database.GetItem(item_data->item_id);
	}

	if (item != 0) {
		if (item_data) {
			inst = database.CreateItem(item, item_data ? item_data->charges : 0, item_data->aug_1,
				item_data->aug_2, item_data->aug_3, item_data->aug_4,
				item_data->aug_5, item_data->aug_6, item_data->attuned,
				item_data->custom_data, item_data->ornamenticon,
				item_data->ornamentidfile, item_data->ornament_hero_model);
		}
		else {
			inst = database.CreateItem(item);
		}
	}

	if (client && inst) {
		if (client->CheckLoreConflict(item)) {
			client->MessageString(Chat::White, LOOT_LORE_ERROR);
			client->QueuePacket(app);
			SendEndLootErrorPacket(client);
			ResetLooter();
			delete inst;
			return;
		}

		if (inst->IsAugmented()) {
			for (int i = EQ::invaug::SOCKET_BEGIN; i <= EQ::invaug::SOCKET_END; i++) {
				EQ::ItemInstance *itm = inst->GetAugment(i);
				if (itm) {
					if (client->CheckLoreConflict(itm->GetItem())) {
						client->MessageString(Chat::White, LOOT_LORE_ERROR);
						client->QueuePacket(app);
						SendEndLootErrorPacket(client);
						ResetLooter();
						delete inst;
						return;
					}
				}
			}
		}

		auto prevent_loot = false;

		if (RuleB(Zone, UseZoneController)) {
			auto controller = entity_list.GetNPCByNPCTypeID(ZONE_CONTROLLER_NPC_ID);
			if (controller) {
				if (parse->HasQuestSub(ZONE_CONTROLLER_NPC_ID, EVENT_LOOT_ZONE)) {
					const auto& export_string = fmt::format(
						"{} {} {} {}",
						inst->GetItem()->ID,
						inst->GetCharges(),
						EntityList::RemoveNumbers(corpse_name),
						GetID()
					);

					std::vector<std::any> args = { inst, this };
					if (parse->EventNPC(EVENT_LOOT_ZONE, controller, client, export_string, 0, &args) != 0) {
						prevent_loot = true;
					}
				}
			}
		}

		if (parse->PlayerHasQuestSub(EVENT_LOOT)) {
			const auto& export_string = fmt::format(
				"{} {} {} {}",
				inst->GetItem()->ID,
				inst->GetCharges(),
				EntityList::RemoveNumbers(corpse_name),
				GetID()
			);

			std::vector<std::any> args = { inst, this };
			if (parse->EventPlayer(EVENT_LOOT, client, export_string, 0, &args) != 0) {
				prevent_loot = true;
			}
		}

		if (player_event_logs.IsEventEnabled(PlayerEvent::LOOT_ITEM) && !IsPlayerCorpse()) {
			auto e = PlayerEvent::LootItemEvent{
				.item_id = inst->GetItem()->ID,
				.item_name = inst->GetItem()->Name,
				.charges = inst->GetCharges(),
				.npc_id = GetNPCTypeID(),
				.corpse_name = EntityList::RemoveNumbers(corpse_name)
			};

			RecordPlayerEventLogWithClient(client, PlayerEvent::LOOT_ITEM, e);
		}

		if (!IsPlayerCorpse())
		{
			// dynamic zones may prevent looting by non-members or based on lockouts
			auto dz = zone->GetDynamicZone();
			if (dz && !dz->CanClientLootCorpse(client, GetNPCTypeID(), GetID()))
			{
				prevent_loot = true;
				// note on live this message is only sent once on the first loot attempt of an open corpse
				client->MessageString(Chat::Loot, LOOT_NOT_ALLOWED, inst->GetItem()->Name);
			}
		}

		if (parse->ItemHasQuestSub(inst, EVENT_LOOT)) {
			const auto& export_string = fmt::format(
				"{} {} {} {}",
				inst->GetItem()->ID,
				inst->GetCharges(),
				EntityList::RemoveNumbers(corpse_name),
				GetID()
			);

			std::vector<std::any> args = { inst, this };
			if (parse->EventItem(EVENT_LOOT, client, inst, this, export_string, 0, &args) != 0) {
				prevent_loot = true;
			}
		}

		if (prevent_loot) {
			lootitem->auto_loot = -1;
			client->QueuePacket(app);
			safe_delete(inst);
			return;
		}


		// safe to ACK now
		client->QueuePacket(app);

		if (
			!IsPlayerCorpse() &&
			RuleB(Character, EnableDiscoveredItems) &&
			client &&
			!client->GetGM() &&
			!client->IsDiscovered(inst->GetItem()->ID)
		) {
			client->DiscoverItem(inst->GetItem()->ID);
		}

		if (zone->adv_data) {
			ServerZoneAdventureDataReply_Struct *ad = (ServerZoneAdventureDataReply_Struct *)zone->adv_data;
			if (ad->type == Adventure_Collect && !IsPlayerCorpse()) {
				if (ad->data_id == inst->GetItem()->ID) {
					zone->DoAdventureCountIncrease();
				}
			}
		}

		// get count for task update before it's mutated by AutoPutLootInInventory
		int count = inst->IsStackable() ? inst->GetCharges() : 1;
		//Set recast on item when looting it!
		auto timestamps = database.GetItemRecastTimestamps(client->CharacterID());
		const auto* d = inst->GetItem();
		if (d->RecastDelay) {
			if (d->RecastType != RECAST_TYPE_UNLINKED_ITEM) {
				inst->SetRecastTimestamp(timestamps.count(d->RecastType) ? timestamps.at(d->RecastType) : 0);
			} else {
				inst->SetRecastTimestamp(timestamps.count(d->ID) ? timestamps.at(d->ID) : 0);
			}
		}

		/* First add it to the looter - this will do the bag contents too */
		if (lootitem->auto_loot > 0) {
			if (!client->AutoPutLootInInventory(*inst, true, true, bag_item_data))
				client->PutLootInInventory(EQ::invslot::slotCursor, *inst, bag_item_data);
		}
		else {
			client->PutLootInInventory(EQ::invslot::slotCursor, *inst, bag_item_data);
		}

		/* Update any tasks that have an activity to loot this item */
		if (RuleB(TaskSystem, EnableTaskSystem) && IsNPCCorpse()) {
			client->UpdateTasksOnLoot(this, item->ID, count);
		}

		/* Remove it from Corpse */
		if (item_data) {
			/* Delete needs to be before RemoveItem because its deletes the pointer for
			* item_data/bag_item_data */
			database.DeleteItemOffCharacterCorpse(corpse_db_id, item_data->equip_slot,
				item_data->item_id);
			/* Delete Item Instance */
			RemoveItem(item_data->lootslot);
		}

		/* Remove Bag Contents */
		if (item->IsClassBag() && (GetPlayerKillItem() != -1 || GetPlayerKillItem() != 1)) {
			for (int i = EQ::invbag::SLOT_BEGIN; i <= EQ::invbag::SLOT_END; i++) {
				if (bag_item_data[i]) {
					/* Delete needs to be before RemoveItem because its deletes the pointer for
					* item_data/bag_item_data */
					database.DeleteItemOffCharacterCorpse(corpse_db_id,
						bag_item_data[i]->equip_slot,
						bag_item_data[i]->item_id);
					/* Delete Item Instance */
					RemoveItem(bag_item_data[i]);
				}
			}
		}

		if (GetPlayerKillItem() != -1) {
			SetPlayerKillItemID(0);
		}

		/* Send message with item link to groups and such */
		EQ::SayLinkEngine linker;
		linker.SetLinkType(EQ::saylink::SayLinkItemInst);
		linker.SetItemInst(inst);

		linker.GenerateLink();

		client->MessageString(Chat::Loot, LOOTED_MESSAGE, linker.Link().c_str());

		if (!IsPlayerCorpse()) {
			Group *g = client->GetGroup();
			if (g != nullptr) {
				g->GroupMessageString(client, Chat::Loot, OTHER_LOOTED_MESSAGE,
					client->GetName(), linker.Link().c_str());
			}
			else {
				Raid *r = client->GetRaid();
				if (r != nullptr) {
					r->RaidMessageString(client, Chat::Loot, OTHER_LOOTED_MESSAGE,
						client->GetName(), linker.Link().c_str());
				}
			}
		}
	}
	else {
		SendEndLootErrorPacket(client);
		safe_delete(inst);
		return;
	}

	if (IsPlayerCorpse()) {
		client->SendItemLink(inst);
	}
	else {
		client->SendItemLink(inst, true);
	}

	safe_delete(inst);
}

void Corpse::EndLoot(Client* client, const EQApplicationPacket* app) {
	auto outapp = new EQApplicationPacket;
	outapp->SetOpcode(OP_LootComplete);
	outapp->size = 0;
	client->QueuePacket(outapp);
	safe_delete(outapp);

	being_looted_by = 0xFFFFFFFF;
	if (IsEmpty())
		Delete();
	else
		Save();
}

void Corpse::FillSpawnStruct(NewSpawn_Struct* ns, Mob* ForWho) {
	Mob::FillSpawnStruct(ns, ForWho);

	ns->spawn.max_hp = 120;
	ns->spawn.NPC = 2;

	UpdateActiveLight();
	ns->spawn.light = m_Light.Type[EQ::lightsource::LightActive];
}

void Corpse::QueryLoot(Client* to) {
	if (itemlist.size() > 0) {
		int player_corpse_limit = to->GetInv().GetLookup()->InventoryTypeSize.Corpse;
		to->Message(
			Chat::White,
			fmt::format(
				"Loot | Name: {} ID: {}",
				GetName(),
				GetNPCTypeID()
			).c_str()
		);

		int item_count = 0;
		for (auto current_item : itemlist) {
			int item_number = (item_count + 1);
			if (!current_item) {
				LogError("Corpse::QueryLoot() - ItemList error, null item.");
				continue;
			}

			if (!current_item->item_id || !database.GetItem(current_item->item_id)) {
				LogError("Corpse::QueryLoot() - Database error, invalid item.");
				continue;
			}

			EQ::SayLinkEngine linker;
			linker.SetLinkType(EQ::saylink::SayLinkLootItem);
			linker.SetLootData(current_item);

			to->Message(
				Chat::White,
				fmt::format(
					"Item {} | Name: {} ({}){}",
					item_number,
					linker.GenerateLink().c_str(),
					current_item->item_id,
					(
						current_item->charges > 1 ?
						fmt::format(
							" Amount: {}",
							current_item->charges
						) :
						""
					)
				).c_str()
			);
			item_count++;
		}
	}

	if (
		platinum ||
		gold ||
		silver ||
		copper
	) {
		to->Message(
			Chat::White,
			fmt::format(
				"Money | {}",
				Strings::Money(
					platinum,
					gold,
					silver,
					copper
				)
			).c_str()
		);
	}
}

bool Corpse::HasItem(uint32 item_id) {
	if (!database.GetItem(item_id)) {
		return false;
	}

	for (auto current_item  = itemlist.begin(); current_item != itemlist.end(); ++current_item) {
		ServerLootItem_Struct* loot_item = *current_item;
		if (!loot_item) {
			LogError("Corpse::HasItem() - ItemList error, null item");
			continue;
		}

		if (!loot_item->item_id || !database.GetItem(loot_item->item_id)) {
			LogError("Corpse::HasItem() - Database error, invalid item");
			continue;
		}

		if (loot_item->item_id == item_id) {
			return true;
		}
	}
	return false;
}

uint16 Corpse::CountItem(uint32 item_id) {
	uint16 item_count = 0;
	if (!database.GetItem(item_id)) {
		return item_count;
	}

	for (auto current_item  = itemlist.begin(); current_item != itemlist.end(); ++current_item) {
		ServerLootItem_Struct* loot_item = *current_item;
		if (!loot_item) {
			LogError("Corpse::CountItem() - ItemList error, null item");
			continue;
		}

		if (!loot_item->item_id || !database.GetItem(loot_item->item_id)) {
			LogError("Corpse::CountItem() - Database error, invalid item");
			continue;
		}

		if (loot_item->item_id == item_id) {
			item_count += loot_item->charges > 0 ? loot_item->charges : 1;
		}
	}
	return item_count;
}

uint32 Corpse::GetItemIDBySlot(uint16 loot_slot) {
	for (auto current_item  = itemlist.begin(); current_item != itemlist.end(); ++current_item) {
		ServerLootItem_Struct* loot_item = *current_item;
		if (loot_item->lootslot == loot_slot) {
			return loot_item->item_id;
		}
	}
	return 0;
}

uint16 Corpse::GetFirstSlotByItemID(uint32 item_id) {
	for (auto current_item  = itemlist.begin(); current_item != itemlist.end(); ++current_item) {
		ServerLootItem_Struct* loot_item = *current_item;
		if (loot_item->item_id == item_id) {
			return loot_item->lootslot;
		}
	}
	return 0;
}

bool Corpse::Summon(Client* client, bool spell, bool CheckDistance) {
	uint32 dist2 = 10000; // pow(100, 2);
	if (!spell) {
		if (GetCharID() == client->CharacterID()) {
			if (IsLocked() && client->Admin() < AccountStatus::GMAdmin) {
				client->Message(Chat::Red, "That corpse is locked by a GM.");
				return false;
			}
			if (!CheckDistance || (DistanceSquaredNoZ(m_Position, client->GetPosition()) <= dist2)) {
				GMMove(client->GetX(), client->GetY(), client->GetZ());
				is_corpse_changed = true;
			}
			else {
				client->MessageString(Chat::Red, CORPSE_TOO_FAR);
				return false;
			}
		}
		else
		{
			bool consented = false;
			for (const auto& consented_player_name : consented_player_names) {
				if (strcasecmp(client->GetName(), consented_player_name.c_str()) == 0) {
					consented = true;
					break;
				}
			}

			if (!consented && consented_guild_id && consented_guild_id != GUILD_NONE) {
				if (client->GuildID() == consented_guild_id) {
					consented = true;
				}
			}
			if (!consented && consented_group_id) {
				Group* grp = client->GetGroup();
				if (grp && grp->GetID() == consented_group_id) {
					consented = true;
				}
			}
			if (!consented && consented_raid_id) {
				Raid* raid = client->GetRaid();
				if (raid && raid->GetID() == consented_raid_id) {
					consented = true;
				}
			}

			if (consented) {
				if (!CheckDistance || (DistanceSquaredNoZ(m_Position, client->GetPosition()) <= dist2)) {
					GMMove(client->GetX(), client->GetY(), client->GetZ());
					is_corpse_changed = true;
				}
				else {
					client->MessageString(Chat::Red, CORPSE_TOO_FAR);
					return false;
				}
			}
			else {
				client->MessageString(Chat::Red, CONSENT_DENIED);
				return false;
			}
		}
	}
	else {
		GMMove(client->GetX(), client->GetY(), client->GetZ());
		is_corpse_changed = true;
	}
	Save();
	return true;
}

void Corpse::CompleteResurrection(){
	rez_experience = 0;
	is_corpse_changed = true;
	Save();
}

void Corpse::Spawn() {
	auto app = new EQApplicationPacket;
	CreateSpawnPacket(app, this);
	entity_list.QueueClients(this, app);
	safe_delete(app);
}

uint32 Corpse::GetEquippedItemFromTextureSlot(uint8 material_slot) const {
	int16 invslot;

	if (material_slot > EQ::textures::LastTexture) {
		return 0;
	}

	invslot = EQ::InventoryProfile::CalcSlotFromMaterial(material_slot);
	if(invslot == INVALID_INDEX) // GetWornItem() should be returning a 0 for any invalid index...
		return 0;

	return GetWornItem(invslot);
}

uint32 Corpse::GetEquipmentColor(uint8 material_slot) const {
	const EQ::ItemData *item = nullptr;

	if (material_slot > EQ::textures::LastTexture) {
		return 0;
	}

	item = database.GetItem(GetEquippedItemFromTextureSlot(material_slot));
	if(item != 0) {
		return (item_tint.Slot[material_slot].UseTint ? item_tint.Slot[material_slot].Color : item->Color);
	}

	return 0;
}

void Corpse::UpdateEquipmentLight()
{
	m_Light.Type[EQ::lightsource::LightEquipment] = 0;
	m_Light.Level[EQ::lightsource::LightEquipment] = 0;

	for (auto iter = itemlist.begin(); iter != itemlist.end(); ++iter) {
		if ((*iter)->equip_slot < EQ::invslot::EQUIPMENT_BEGIN || (*iter)->equip_slot > EQ::invslot::EQUIPMENT_END) { continue; }
		if ((*iter)->equip_slot == EQ::invslot::slotAmmo) { continue; }

		auto item = database.GetItem((*iter)->item_id);
		if (item == nullptr) { continue; }

		if (EQ::lightsource::IsLevelGreater(item->Light, m_Light.Type[EQ::lightsource::LightEquipment]))
			m_Light.Type[EQ::lightsource::LightEquipment] = item->Light;
	}

	uint8 general_light_type = 0;
	for (auto iter = itemlist.begin(); iter != itemlist.end(); ++iter) {
		if ((*iter)->equip_slot < EQ::invslot::GENERAL_BEGIN || (*iter)->equip_slot > EQ::invslot::GENERAL_END) { continue; }

		auto item = database.GetItem((*iter)->item_id);
		if (item == nullptr) { continue; }

		if (!item->IsClassCommon()) { continue; }
		if (item->Light < 9 || item->Light > 13) { continue; }

		if (EQ::lightsource::TypeToLevel(item->Light))
			general_light_type = item->Light;
	}

	if (EQ::lightsource::IsLevelGreater(general_light_type, m_Light.Type[EQ::lightsource::LightEquipment]))
		m_Light.Type[EQ::lightsource::LightEquipment] = general_light_type;

	m_Light.Level[EQ::lightsource::LightEquipment] = EQ::lightsource::TypeToLevel(m_Light.Type[EQ::lightsource::LightEquipment]);
}

void Corpse::AddLooter(Mob* who) {
	for (int i = 0; i < MAX_LOOTERS; i++) {
		if (allowed_looters[i] == 0) {
			allowed_looters[i] = who->CastToClient()->CharacterID();
			break;
		}
	}
}

void Corpse::LoadPlayerCorpseDecayTime(uint32 corpse_db_id){
	if(!corpse_db_id)
		return;

	uint32 active_corpse_decay_timer = database.GetCharacterCorpseDecayTimer(corpse_db_id);
	if (active_corpse_decay_timer > 0 && RuleI(Character, CorpseDecayTimeMS) > (active_corpse_decay_timer * 1000)) {
		corpse_decay_timer.SetTimer(RuleI(Character, CorpseDecayTimeMS) - (active_corpse_decay_timer * 1000));
	}
	else {
		corpse_decay_timer.SetTimer(2000);
	}
	if (active_corpse_decay_timer > 0 && RuleI(Zone, GraveyardTimeMS) > (active_corpse_decay_timer * 1000)) {
		corpse_graveyard_timer.SetTimer(RuleI(Zone, GraveyardTimeMS) - (active_corpse_decay_timer * 1000));
	}
	else {
		corpse_graveyard_timer.SetTimer(3000);
	}
}

void Corpse::SendWorldSpawnPlayerCorpseInZone(uint32_t zone_id)
{
	auto pack = std::make_unique<ServerPacket>(ServerOP_SpawnPlayerCorpse, sizeof(SpawnPlayerCorpse_Struct));
	SpawnPlayerCorpse_Struct* spc = reinterpret_cast<SpawnPlayerCorpse_Struct*>(pack->pBuffer);
	spc->player_corpse_id = corpse_db_id;
	spc->zone_id = zone_id;
	worldserver.SendPacket(pack.get());
}

bool Corpse::MovePlayerCorpseToGraveyard()
{
	if (IsPlayerCorpse() && zone && zone->HasGraveyard())
	{
		Save();

		glm::vec4 graveyard_point = zone->GetGraveyardPoint();

		uint16_t instance_id = (zone->GetZoneID() == zone->graveyard_zoneid()) ? zone->GetInstanceID() : 0;
		database.SendCharacterCorpseToGraveyard(corpse_db_id, zone->graveyard_zoneid(), instance_id, graveyard_point);
		SendWorldSpawnPlayerCorpseInZone(zone->graveyard_zoneid());

		corpse_db_id = 0;
		player_corpse_depop = true;
		corpse_graveyard_timer.Disable();

		LogDebug("Moved [{}] player corpse to the designated graveyard in zone [{}]", GetName(), ZoneName(zone->graveyard_zoneid()));
		return true;
	}

	return false;
}

bool Corpse::MovePlayerCorpseToNonInstance()
{
	if (IsPlayerCorpse() && zone && zone->GetInstanceID() != 0)
	{
		Save();

		database.SendCharacterCorpseToNonInstance(corpse_db_id);
		SendWorldSpawnPlayerCorpseInZone(zone->GetZoneID());

		corpse_db_id = 0;
		player_corpse_depop = true;
		corpse_graveyard_timer.Disable();

		LogDebug("Moved [{}] player corpse to non-instance version of zone [{}]", GetName(), ZoneName(zone->GetZoneID()));
		return true;
	}

	return false;
}

std::vector<int> Corpse::GetLootList() {
	std::vector<int> corpse_items;
	for (auto current_item  = itemlist.begin(); current_item != itemlist.end(); ++current_item) {
		ServerLootItem_Struct* loot_item = *current_item;
		if (!loot_item) {
			LogError("Corpse::GetLootList() - ItemList error, null item");
			continue;
		}

		if (std::find(corpse_items.begin(), corpse_items.end(), loot_item->item_id) != corpse_items.end()) {
			continue;
		}

		corpse_items.push_back(loot_item->item_id);
	}
	return corpse_items;
}
