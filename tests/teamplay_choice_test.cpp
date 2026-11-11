// Headless test of the team-play duelist choice (Milestone 2).
// Loads ocgcore.dll at runtime and plays a 2v2 team-play duel with 20-card vanilla Decks for every
// duelist. Three scripted forced triggers on team 0's field mirror real cards:
//   turn 1  "Card Trooper": mills 2 as a cost, then draws 1. The choice must be asked during the
//           cost (once), and both the mill and the draw use the chosen duelist (we pick 1).
//   turn 3  "Pot of Duality": no operation info; reveals the top 3, adds 1 to the hand. The choice
//           must be asked when the Deck is revealed (we pick 0), and the card comes from that Deck.
//   turn 5  "Card Destruction": each player discards their hand and draws that many. Must ask once
//           per team (we pick own 1, opposing 0) and only the chosen duelists discard and draw.
// Build/run: see tests/run_core_test.ps1
#define _CRT_SECURE_NO_WARNINGS
#include <windows.h>
#include <cstdio>
#include <cstring>
#include <vector>
#include "ocgapi_types.h"
#include "ocgapi_constants.h"

using CreateDuel_t = int (*)(OCG_Duel*, const OCG_DuelOptions*);
using DestroyDuel_t = void (*)(OCG_Duel);
using NewCard_t = void (*)(OCG_Duel, const OCG_NewCardInfo*);
using StartDuel_t = void (*)(OCG_Duel);
using Process_t = int (*)(OCG_Duel);
using GetMessage_t = void* (*)(OCG_Duel, uint32_t*);
using SetResponse_t = void (*)(OCG_Duel, const void*, uint32_t);
using QueryCount_t = uint32_t (*)(OCG_Duel, uint8_t, uint32_t);
using QueryLocation_t = void* (*)(OCG_Duel, uint32_t*, const OCG_QueryInfo*);
using LoadScript_t = int (*)(OCG_Duel, const char*, uint32_t, const char*);

static CreateDuel_t pCreateDuel; static DestroyDuel_t pDestroyDuel; static NewCard_t pNewCard;
static StartDuel_t pStartDuel; static Process_t pProcess; static GetMessage_t pGetMessage;
static SetResponse_t pSetResponse; static QueryCount_t pQueryCount; static QueryLocation_t pQueryLocation;
static LoadScript_t pLoadScript;

static int failures = 0;
#define CHECK(cond, ...) do { if(!(cond)) { ++failures; std::printf("  FAIL: " __VA_ARGS__); std::printf("\n"); } } while(0)

// Raw constant values (no constant.lua is loaded): EFFECT_TYPE_FIELD 0x2, EFFECT_TYPE_TRIGGER_F 0x200,
// EVENT_PHASE+PHASE_STANDBY 0x1002, LOCATION_MZONE 0x4, LOCATION_HAND 0x2, CATEGORY_DRAW 0x10000,
// CATEGORY_HANDES 0x80, PLAYER_ALL 3, REASON_EFFECT 0x40, REASON_COST 0x80, REASON_DISCARD 0x4000
#define TRIGGER_HEAD(code, turn) \
	"function c" #code ".initial_effect(c)\n" \
	"	local e=Effect.CreateEffect(c)\n" \
	"	e:SetType(0x2+0x200)\n" \
	"	e:SetCode(0x1002)\n" \
	"	e:SetRange(0x4)\n" \
	"	e:SetCountLimit(1)\n" \
	"	e:SetCondition(function() return Duel.GetTurnCount()==" #turn " end)\n"
static const char TROOPER_SCRIPT[] = TRIGGER_HEAD(3003, 1)
	"	e:SetCategory(0x10000)\n"
	"	e:SetCost(function(e,tp,eg,ep,ev,re,r,rp,chk) if chk==0 then return true end Duel.DiscardDeck(tp,2,0x80) end)\n"
	"	e:SetTarget(function(e,tp,eg,ep,ev,re,r,rp,chk) if chk==0 then return true end Duel.SetOperationInfo(0,0x10000,nil,0,tp,1) end)\n"
	"	e:SetOperation(function(e,tp) Duel.Draw(tp,1,0x40) end)\n"
	"	c:RegisterEffect(e)\n"
	"end\n";
static const char DUALITY_SCRIPT[] = TRIGGER_HEAD(3004, 3)
	"	e:SetOperation(function(e,tp)\n"
	"		Duel.ConfirmDecktop(tp,3)\n"
	"		local g=Duel.GetDecktopGroup(tp,3)\n"
	"		Duel.SendtoHand(g:GetFirst(),nil,0x40)\n"
	"	end)\n"
	"	c:RegisterEffect(e)\n"
	"end\n";
static const char DESTRUCTION_SCRIPT[] = TRIGGER_HEAD(3005, 5)
	"	e:SetCategory(0x10000+0x80)\n"
	"	e:SetTarget(function(e,tp,eg,ep,ev,re,r,rp,chk) if chk==0 then return true end\n"
	"		Duel.SetOperationInfo(0,0x80,nil,0,3,1)\n"
	"		Duel.SetOperationInfo(0,0x10000,nil,0,3,1) end)\n"
	"	e:SetOperation(function(e,tp)\n"
	"		for _,p in ipairs({tp,1-tp}) do\n"
	"			local g=Duel.GetFieldGroup(p,0x2,0)\n"
	"			local ct=#g\n"
	"			Duel.SendtoGrave(g,0x40+0x4000)\n"
	"			Duel.Draw(p,ct,0x40)\n"
	"		end\n"
	"	end)\n"
	"	c:RegisterEffect(e)\n"
	"end\n";

static void card_reader(void*, uint32_t code, OCG_CardData* data) {
	std::memset(data, 0, sizeof(*data));
	static uint16_t no_setcodes[1] = { 0 };
	data->code = code;
	data->setcodes = no_setcodes;
	data->type = code >= 3000 ? (TYPE_MONSTER | TYPE_EFFECT) : (TYPE_MONSTER | TYPE_NORMAL);
	data->level = 4;
	data->attribute = ATTRIBUTE_EARTH;
	data->race = RACE_WARRIOR;
	data->attack = 1000;
	data->defense = 1000;
}
static void card_reader_done(void*, OCG_CardData*) {}
static int script_reader(void*, OCG_Duel duel, const char* name) {
	struct { const char* file; const char* text; size_t len; } scripts[] = {
		{ "c3003.lua", TROOPER_SCRIPT, sizeof(TROOPER_SCRIPT) - 1 },
		{ "c3004.lua", DUALITY_SCRIPT, sizeof(DUALITY_SCRIPT) - 1 },
		{ "c3005.lua", DESTRUCTION_SCRIPT, sizeof(DESTRUCTION_SCRIPT) - 1 },
	};
	for(auto& s : scripts)
		if(std::strstr(name, s.file))
			return pLoadScript(duel, s.text, static_cast<uint32_t>(s.len), name);
	return 0;
}
static void log_handler(void*, const char* str, int type) {
	if(type == OCG_LOG_TYPE_ERROR)
		std::printf("  [core error] %s\n", str);
}

// Count cards in a location per duelist using QUERY_DUELIST
static void loc_by_duelist(OCG_Duel duel, uint8_t team, uint32_t loc, uint32_t out[2]) {
	out[0] = out[1] = 0;
	OCG_QueryInfo qi{};
	qi.flags = QUERY_DUELIST;
	qi.con = team;
	qi.loc = loc;
	uint32_t len = 0;
	auto* buf = static_cast<const uint8_t*>(pQueryLocation(duel, &len, &qi));
	const uint8_t* p = buf + 4;
	const uint8_t* end = buf + len;
	while(p < end) {
		uint16_t size; std::memcpy(&size, p, 2); p += 2;
		if(size == 0) continue;
		uint32_t flag; std::memcpy(&flag, p, 4);
		if(flag == QUERY_DUELIST && p[4] < 2)
			++out[p[4]];
		p += size;
	}
}

int main(int argc, char** argv) {
	const char* dll = argc > 1 ? argv[1] : "ocgcore.dll";
	HMODULE lib = LoadLibraryA(dll);
	if(!lib) { std::printf("cannot load %s\n", dll); return 2; }
#define LOAD(var, name) var = reinterpret_cast<decltype(var)>(GetProcAddress(lib, name)); if(!var) { std::printf("missing %s\n", name); return 2; }
	LOAD(pCreateDuel, "OCG_CreateDuel"); LOAD(pDestroyDuel, "OCG_DestroyDuel"); LOAD(pNewCard, "OCG_DuelNewCard");
	LOAD(pStartDuel, "OCG_StartDuel"); LOAD(pProcess, "OCG_DuelProcess"); LOAD(pGetMessage, "OCG_DuelGetMessage");
	LOAD(pSetResponse, "OCG_DuelSetResponse"); LOAD(pQueryCount, "OCG_DuelQueryCount"); LOAD(pQueryLocation, "OCG_DuelQueryLocation");
	LOAD(pLoadScript, "OCG_LoadScript");

	OCG_DuelOptions opts{};
	opts.seed[0] = 5; opts.seed[1] = 6; opts.seed[2] = 7; opts.seed[3] = 8;
	opts.flags = DUEL_MODE_MR5 | DUEL_TEAM_PLAY;
	opts.team1 = { 8000, 5, 1 };
	opts.team2 = { 8000, 5, 1 };
	opts.cardReader = card_reader; opts.cardReaderDone = card_reader_done;
	opts.scriptReader = script_reader; opts.logHandler = log_handler;
	OCG_Duel duel = nullptr;
	if(pCreateDuel(&duel, &opts) != OCG_DUEL_CREATION_SUCCESS) { std::printf("create failed\n"); return 2; }
	for(uint8_t t = 0; t < 2; ++t) {
		for(uint8_t d = 0; d < 2; ++d) {
			OCG_NewCardInfo ci{};
			ci.team = t; ci.con = t; ci.duelist = d; ci.loc = LOCATION_DECK; ci.pos = POS_FACEDOWN_DEFENSE;
			ci.code = 1000 * (t + 1) + 1 + d;
			for(int i = 0; i < 20; ++i)
				pNewCard(duel, &ci);
		}
	}
	for(uint8_t i = 0; i < 3; ++i) {
		OCG_NewCardInfo ci{};
		ci.team = 0; ci.con = 0; ci.duelist = 0; ci.loc = LOCATION_MZONE; ci.seq = i; ci.pos = POS_FACEUP_ATTACK;
		ci.code = 3003 + i;
		pNewCard(duel, &ci);
	}
	pStartDuel(duel);

	int turn = 0;
	int prompts = 0;                    // duelist-choice prompts answered
	std::vector<uint64_t> announced;    // HINT_OPSELECTED duelist descriptions
	bool checked[3] = {};
	bool done = false;
	int guard = 0;
	// Our answers: { turn, team asked about, duelist we pick }
	auto pick = [&](uint32_t team) -> int32_t {
		if(turn == 1) return 1;            // "Card Trooper": own duelist 1
		if(turn == 3) return 0;            // "Pot of Duality": own duelist 0
		return team == 0 ? 1 : 0;          // "Card Destruction": own duelist 1, opposing duelist 0
	};
	auto counts = [&](uint8_t team, uint32_t loc, uint32_t out[2]) { loc_by_duelist(duel, team, loc, out); };

	while(!done && ++guard < 10000) {
		int status = pProcess(duel);
		uint32_t len = 0;
		auto* buf = static_cast<const uint8_t*>(pGetMessage(duel, &len));
		const uint8_t* p = buf;
		const uint8_t* end = buf + len;
		bool responded = false;
		while(p < end && !done) {
			uint32_t mlen; std::memcpy(&mlen, p, 4); p += 4;
			const uint8_t* m = p; p += mlen;
			const uint8_t msg = m[0];
			const uint8_t* d = m + 1;
			switch(msg) {
			case MSG_NEW_TURN: ++turn; break;
			case MSG_HINT: {
				uint64_t desc; std::memcpy(&desc, d + 2, 8);
				if(d[0] == HINT_OPSELECTED && desc >= TEAM_DUELIST_DESC && desc < TEAM_DUELIST_DESC + 4)
					announced.push_back(desc);
				break;
			}
			case MSG_SELECT_OPTION: {
				uint8_t cnt = d[1];
				uint64_t o0 = 0; std::memcpy(&o0, d + 2, 8);
				const bool is_choice = cnt == 2 && o0 >= TEAM_DUELIST_DESC && o0 < TEAM_DUELIST_DESC + 4;
				CHECK(is_choice, "unexpected option prompt on turn %d", turn);
				const uint32_t team = static_cast<uint32_t>((o0 - TEAM_DUELIST_DESC) / 2);
				const int32_t resp = pick(team);
				std::printf("  turn %d: asked to choose a duelist of team %u -> %d\n", turn, team, resp);
				++prompts;
				pSetResponse(duel, &resp, sizeof(resp));
				responded = true;
				break;
			}
			case MSG_SELECT_IDLECMD: {
				uint32_t h0[2], k0[2], h1[2], k1[2];
				counts(0, LOCATION_HAND, h0); counts(0, LOCATION_DECK, k0);
				counts(1, LOCATION_HAND, h1); counts(1, LOCATION_DECK, k1);
				if(turn == 1) {
					// Opening 5/5, Decks 15/15. Cost milled 2 from duelist 1, then duelist 1 drew 1.
					std::printf("  turn 1 (Card Trooper): prompts %d, hand %u/%u, Deck %u/%u\n", prompts, h0[0], h0[1], k0[0], k0[1]);
					CHECK(prompts == 1, "the mill cost should ask once, and the draw should reuse that choice (prompts %d)", prompts);
					CHECK(h0[0] == 5 && h0[1] == 6 && k0[0] == 15 && k0[1] == 12, "mill 2 + draw 1 should both use duelist 1 (want hand 5/6, Deck 15/12)");
					checked[0] = true;
				} else if(turn == 3) {
					// Draw Phase: hand 6/7, Deck 14/11. Revealed duelist 0's top 3, added 1.
					std::printf("  turn 3 (Pot of Duality): prompts %d, hand %u/%u, Deck %u/%u\n", prompts, h0[0], h0[1], k0[0], k0[1]);
					CHECK(prompts == 2, "revealing the Deck top should ask once (prompts %d)", prompts);
					CHECK(h0[0] == 7 && h0[1] == 7 && k0[0] == 13 && k0[1] == 11, "the card should come from duelist 0's Deck (want hand 7/7, Deck 13/11)");
					checked[1] = true;
				} else if(turn == 5) {
					// Before: team 0 hand 7/7, Deck 12/10; team 1 hand 6/6, Deck 13/13.
					// Team 0 duelist 1 discards 7, draws 7; team 1 duelist 0 discards 6, draws 6.
					const uint32_t g0 = pQueryCount(duel, 0, LOCATION_GRAVE), g1 = pQueryCount(duel, 1, LOCATION_GRAVE);
					std::printf("  turn 5 (Card Destruction): prompts %d, team 0 hand %u/%u Deck %u/%u GY %u, team 1 hand %u/%u Deck %u/%u GY %u\n",
					            prompts, h0[0], h0[1], k0[0], k0[1], g0, h1[0], h1[1], k1[0], k1[1], g1);
					CHECK(prompts == 4, "should ask once for each team (prompts %d)", prompts);
					CHECK(h0[0] == 7 && h0[1] == 7 && k0[0] == 12 && k0[1] == 3, "team 0: only duelist 1 should discard and redraw (want hand 7/7, Deck 12/3)");
					CHECK(g0 == 11, "team 0 GY: 2 milled + 2 hand-limit + 7 discarded (want 11, got %u)", g0);
					CHECK(h1[0] == 6 && h1[1] == 6 && k1[0] == 7 && k1[1] == 13, "team 1: only duelist 0 should discard and redraw (want hand 6/6, Deck 7/13)");
					CHECK(g1 == 8, "team 1 GY: 2 hand-limit + 6 discarded (want 8, got %u)", g1);
					checked[2] = true;
					done = true;
					break;
				}
				int32_t resp = 7; // go to End Phase
				pSetResponse(duel, &resp, sizeof(resp));
				responded = true;
				break;
			}
			case MSG_SELECT_BATTLECMD: { int32_t resp = 3; pSetResponse(duel, &resp, sizeof(resp)); responded = true; break; }
			case MSG_SELECT_CHAIN: { int32_t resp = d[2] ? 0 : -1; pSetResponse(duel, &resp, sizeof(resp)); responded = true; break; }
			case MSG_SELECT_CARD: {
				// End Phase hand-limit discard: pick the first cards offered
				uint32_t mn; std::memcpy(&mn, d + 2, 4);
				std::vector<uint32_t> resp = { 0u, mn };
				for(uint32_t i = 0; i < mn; ++i) resp.push_back(i);
				pSetResponse(duel, resp.data(), static_cast<uint32_t>(resp.size() * 4));
				responded = true;
				break;
			}
			case MSG_RETRY: std::printf("  core asked for a retry (invalid response) on turn %d\n", turn); ++failures; done = true; break;
			case MSG_WIN: std::printf("  duel ended unexpectedly on turn %d\n", turn); ++failures; done = true; break;
			default:
				if((msg >= MSG_SELECT_EFFECTYN && msg <= MSG_SELECT_UNSELECT_CARD) || msg == MSG_ROCK_PAPER_SCISSORS ||
				   (msg >= MSG_ANNOUNCE_RACE && msg <= MSG_ANNOUNCE_NUMBER)) {
					std::printf("  unhandled prompt %u on turn %d\n", msg, turn);
					++failures; done = true;
				}
				break;
			}
			if(responded) break;
		}
		if(status == OCG_DUEL_STATUS_END) done = true;
	}
	CHECK(checked[0] && checked[1] && checked[2], "not every scenario ran (turns 1, 3, 5)");
	CHECK(announced.size() == static_cast<size_t>(prompts), "every choice should be announced (%zu of %d)", announced.size(), prompts);
	pDestroyDuel(duel);
	std::printf(failures ? "FAILED (%d)\n" : "PASSED\n", failures);
	return failures ? 1 : 0;
}
