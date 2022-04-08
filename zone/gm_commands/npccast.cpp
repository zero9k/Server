#include "../client.h"

void command_npccast(Client *c, const Seperator *sep)
{
	if (c->GetTarget() && c->GetTarget()->IsNPC()) {
		NPC *target = c->GetTarget()->CastToNPC();
		if (!sep->IsNumber(1) && sep->arg[1] && sep->IsNumber(2)) {
			const char *entity_name  = sep->arg[1] ? sep->arg[1] : 0;
			auto       spell_id      = sep->arg[2] ? std::stoul(sep->arg[2]) : 0;
			Mob        *spell_target = entity_list.GetMob(entity_name);
			if (spell_target && IsValidSpell(spell_id) && spell_id < SPDAT_RECORDS) {
				c->Message(
					Chat::White,
					fmt::format(
						"{} ({}) casting {} ({}) on {} ({}).",
						target->GetCleanName(),
						target->GetID(),
						GetSpellName(static_cast<uint16>(spell_id)),
						spell_id,
						spell_target->GetCleanName(),
						spell_target->GetID()
					).c_str()
				);

				target->CastSpell(spell_id, spell_target->GetID());
			}
			else {
				if (!spell_target) {
					c->Message(
						Chat::White,
						fmt::format(
							"Entity {} was not found",
							entity_name
						).c_str()
					);
				}
				else if (!spell_id || !IsValidSpell(spell_id)) {
					c->Message(
						Chat::White,
						fmt::format(
							"Spell ID {} was not found",
							spell_id
						).c_str()
					);
				}
			}
		}
		else if (sep->IsNumber(1) && sep->IsNumber(2)) {
			uint16 entity_id     = sep->arg[1] ? std::stoul(sep->arg[1]) : 0;
			auto   spell_id      = sep->arg[2] ? std::stoul(sep->arg[2]) : 0;
			Mob    *spell_target = entity_list.GetMob(entity_id);
			if (spell_target && IsValidSpell(spell_id) && spell_id < SPDAT_RECORDS) {
				c->Message(
					Chat::White,
					fmt::format(
						"{} ({}) casting {} ({}) on {} ({}).",
						target->GetCleanName(),
						target->GetID(),
						GetSpellName(static_cast<uint16>(spell_id)),
						spell_id,
						spell_target->GetCleanName(),
						spell_target->GetID()
					).c_str()
				);

				target->CastSpell(spell_id, spell_target->GetID());
			}
			else {
				if (!spell_target) {
					c->Message(
						Chat::White,
						fmt::format(
							"Entity ID {} was not found",
							entity_id
						).c_str()
					);
				}
				else if (!spell_id || !IsValidSpell(spell_id)) {
					c->Message(
						Chat::White,
						fmt::format(
							"Spell ID {} was not found",
							spell_id
						).c_str()
					);
				}
			}
		}
	}
	else {
		c->Message(Chat::White, "You must target an NPC to use this command.");
	}
}

