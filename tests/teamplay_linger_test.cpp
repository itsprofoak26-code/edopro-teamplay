// Headless test of lingering effects created by a hand effect (Mulcharmy Fuwalos-style), team play.
// Team 0 duelist 0 holds a card that triggers from the hand on turn 1 and, like Fuwalos, discards
// itself as the cost, so it is in the GY when its lingering effects run. It registers:
//   - a lingering End Phase effect that draws 1 card twice (like Fuwalos's "draw 1 each time…").
//     Both draws must come from duelist 0's Deck: the effect stays bound to the duelist who
//     activated the card (rule 1), even though it runs later, outside the chain. Duelist 1's
//     Deck is larger, so the old "duelist with more cards" fallback would pick duelist 1.
//   - an activation limit (EFFECT_FLAG_OATH): "cards cannot be added from your Deck to the hand
//     this turn". Activation limits stay team-wide, so neither duelist's Deck may be added.
// A team 0 field card probes, in the End Phase, how many of the team's Deck cards could be added.
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
using QueryLocation_t = void* (*)(OCG_Duel, uint32_t*, const OCG_QueryInfo*);
using LoadScript_t = int (*)(OCG_Duel, const char*, uint32_t, const char*);

static CreateDuel_t pCreateDuel; static DestroyDuel_t pDestroyDuel; static NewCard_t pNewCard;
static StartDuel_t pStartDuel; static Process_t pProcess; static GetMessage_t pGetMessage;
static SetResponse_t pSetResponse; static QueryLocation_t pQueryLocation; static LoadScript_t pLoadScript;

static int failures = 0;
#define CHECK(cond, ...) do { if(!(cond)) { ++failures; std::printf("  FAIL: " __VA_ARGS__); std::printf("\n"); } } while(0)

// Raw constant values: EFFECT_TYPE_FIELD 0x2, EFFECT_TYPE_TRIGGER_F 0x200, EFFECT_TYPE_CONTINUOUS 0x800,
// EVENT_PHASE+PHASE_STANDBY 0x1002, EVENT_PHASE+PHASE_END 0x1200, LOCATION_HAND 0x2, LOCATION_DECK 0x1,
// LOCATION_MZONE 0x4, EFFECT_FLAG_OATH 0x80000, EFFECT_CANNOT_TO_HAND 65, RESET_PHASE+PHASE_END 0x40000200,
// REASON_EFFECT 0x40
static const char LINGER_SCRIPT[] =
	"function c3010.initial_effect(c)\n"
	"	local e=Effect.CreateEffect(c)\n"
	"	e:SetType(0x2+0x200)\n"
	"	e:SetCode(0x1002)\n"
	"	e:SetRange(0x2)\n"
	"	e:SetCountLimit(1)\n"
	"	e:SetCondition(function() return Duel.GetTurnCount()==1 end)\n"
	/* Like Fuwalos: discard itself as the cost, and register the activation limit there */
	"	e:SetCost(function(e,tp,eg,ep,ev,re,r,rp,chk)\n"
	"		if chk==0 then return true end\n"
	"		Duel.SendtoGrave(e:GetHandler(),0x80+0x4000)\n"
	"		local e2=Effect.CreateEffect(e:GetHandler())\n"
	"		e2:SetType(0x2)\n"
	"		e2:SetProperty(0x80000)\n"
	"		e2:SetCode(65)\n"
	"		e2:SetTargetRange(0x1,0)\n"
	"		e2:SetReset(0x40000200)\n"
	"		Duel.RegisterEffect(e2,tp)\n"
	"	end)\n"
	"	e:SetOperation(function(e,tp)\n"
	"		local e1=Effect.CreateEffect(e:GetHandler())\n"
	"		e1:SetType(0x2+0x800)\n"
	"		e1:SetCode(0x1200)\n"
	"		e1:SetCountLimit(1)\n"
	"		e1:SetOperation(function(e,tp) Duel.Draw(tp,1,0x40) Duel.Draw(tp,1,0x40) end)\n"
	"		e1:SetReset(0x40000200)\n"
	"		Duel.RegisterEffect(e1,tp)\n"
	"	end)\n"
	"	c:RegisterEffect(e)\n"
	"end\n";
static const char PROBE_SCRIPT[] =
	"function c3011.initial_effect(c)\n"
	"	local e=Effect.CreateEffect(c)\n"
	"	e:SetType(0x2+0x800)\n"
	"	e:SetCode(0x1200)\n"
	"	e:SetRange(0x4)\n"
	"	e:SetCountLimit(1)\n"
	"	e:SetCondition(function() return Duel.GetTurnCount()==1 end)\n"
	"	e:SetOperation(function(e,tp)\n"
	"		local deck=Duel.GetFieldGroup(tp,0x1,0)\n"
	"		Debug.Message('probe '..deck:FilterCount(Card.IsAbleToHand,nil)..' '..#deck)\n"
	"	end)\n"
	"	c:RegisterEffect(e)\n"
	"end\n";
static int probe[2] = { -1, -1 };

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
	if(std::strstr(name, "c3010.lua"))
		return pLoadScript(duel, LINGER_SCRIPT, sizeof(LINGER_SCRIPT) - 1, name);
	if(std::strstr(name, "c3011.lua"))
		return pLoadScript(duel, PROBE_SCRIPT, sizeof(PROBE_SCRIPT) - 1, name);
	return 0;
}
static void log_handler(void*, const char* str, int type) {
	if(type == OCG_LOG_TYPE_ERROR)
		std::printf("  [core error] %s\n", str);
	if(std::strncmp(str, "probe ", 6) == 0)
		std::sscanf(str + 6, "%d %d", &probe[0], &probe[1]);
}

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
	LOAD(pSetResponse, "OCG_DuelSetResponse"); LOAD(pQueryLocation, "OCG_DuelQueryLocation");
	LOAD(pLoadScript, "OCG_LoadScript");

	OCG_DuelOptions opts{};
	opts.seed[0] = 9; opts.seed[1] = 10; opts.seed[2] = 11; opts.seed[3] = 12;
	opts.flags = DUEL_MODE_MR5 | DUEL_TEAM_PLAY;
	opts.team1 = { 8000, 5, 1 };
	opts.team2 = { 8000, 5, 1 };
	opts.cardReader = card_reader; opts.cardReaderDone = card_reader_done;
	opts.scriptReader = script_reader; opts.logHandler = log_handler;
	OCG_Duel duel = nullptr;
	if(pCreateDuel(&duel, &opts) != OCG_DUEL_CREATION_SUCCESS) { std::printf("create failed\n"); return 2; }
	// Team 0: duelist 0 has 20 cards, duelist 1 has 25 (so "more cards" would pick duelist 1)
	const int deck_size[2][2] = { { 20, 25 }, { 20, 20 } };
	for(uint8_t t = 0; t < 2; ++t) {
		for(uint8_t d = 0; d < 2; ++d) {
			OCG_NewCardInfo ci{};
			ci.team = t; ci.con = t; ci.duelist = d; ci.loc = LOCATION_DECK; ci.pos = POS_FACEDOWN_DEFENSE;
			ci.code = 1000 * (t + 1) + 1 + d;
			for(int i = 0; i < deck_size[t][d]; ++i)
				pNewCard(duel, &ci);
		}
	}
	{
		OCG_NewCardInfo ci{};
		ci.team = 0; ci.con = 0; ci.duelist = 0; ci.loc = LOCATION_HAND; ci.pos = POS_FACEDOWN_DEFENSE;
		ci.code = 3010;                       // the Fuwalos-like card, in duelist 0's hand
		pNewCard(duel, &ci);
		ci.loc = LOCATION_MZONE; ci.seq = 0; ci.pos = POS_FACEUP_ATTACK;
		ci.code = 3011;                       // the End Phase probe, on team 0's field
		pNewCard(duel, &ci);
	}
	pStartDuel(duel);

	int turn = 0;
	bool done = false, checked = false;
	int guard = 0;
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
			case MSG_NEW_TURN: {
				++turn;
				if(turn == 2) {
					// Turn 1: no Draw Phase draw for the first team. The lingering effect drew 2.
					uint32_t dk[2]; loc_by_duelist(duel, 0, LOCATION_DECK, dk);
					std::printf("  turn 1 End Phase: team 0 Deck %u/%u (want 13/20), probe: %d of %d Deck cards addable (want 0)\n",
					            dk[0], dk[1], probe[0], probe[1]);
					CHECK(dk[0] == 13 && dk[1] == 20, "both lingering draws must come from the activating duelist's Deck");
					CHECK(probe[0] == 0 && probe[1] > 0, "the activation limit (oath) must stay team-wide: no Deck card of either duelist can be added");
					checked = true;
					done = true;
				}
				break;
			}
			case MSG_SELECT_IDLECMD: { int32_t resp = 7; pSetResponse(duel, &resp, sizeof(resp)); responded = true; break; }
			case MSG_SELECT_BATTLECMD: { int32_t resp = 3; pSetResponse(duel, &resp, sizeof(resp)); responded = true; break; }
			case MSG_SELECT_CHAIN: { int32_t resp = d[2] ? 0 : -1; pSetResponse(duel, &resp, sizeof(resp)); responded = true; break; }
			case MSG_SELECT_CARD: {
				uint32_t mn; std::memcpy(&mn, d + 2, 4);
				std::vector<uint32_t> resp = { 0u, mn };
				for(uint32_t i = 0; i < mn; ++i) resp.push_back(i);
				pSetResponse(duel, resp.data(), static_cast<uint32_t>(resp.size() * 4));
				responded = true;
				break;
			}
			case MSG_RETRY: std::printf("  core asked for a retry on turn %d\n", turn); ++failures; done = true; break;
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
	CHECK(checked, "the End Phase checks did not run");
	pDestroyDuel(duel);
	std::printf(failures ? "FAILED (%d)\n" : "PASSED\n", failures);
	return failures ? 1 : 0;
}
