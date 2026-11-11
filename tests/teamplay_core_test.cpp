// Headless test of the Team Play rules in ocgcore (Milestone 1).
// Loads ocgcore.dll at runtime, runs a 2v2 team-play duel with vanilla Normal Monsters,
// passes every turn, and checks:
//   - each duelist draws a 5-card opening hand
//   - the team going first skips the turn-1 draw; afterwards both duelists draw 1
//   - the hand size limit is per duelist (only the over-limit duelist discards)
//   - a team loses when one duelist must draw from an empty Deck
//   - (Milestone 2) which duelist a card joins when it moves. A scripted monster of team 1:
//     turn 4: sends the hand to the GY, then adds the GY to the hand with duelist 0 chosen
//             (Debug.SetTeamDestDuelist) -> every card goes to duelist 0's hand
//     turn 6: sends the hand to the GY, then adds the GY to the hand with no choice
//             -> every card returns to its owner duelist's hand
// Build/run: see tests/run_core_test.ps1
#include <windows.h>
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <string>
#include <vector>
#include "ocgapi_types.h"
#include "ocgapi_constants.h"

static constexpr int WIN_REASON_DECK_OUT = 2; // see field::process(Processors::Adjust) in processor.cpp

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

// Codes: team 0 duelist 0 / duelist 1, team 1 duelist 0 / duelist 1
static constexpr uint32_t CODE[2][2] = { { 1001, 1002 }, { 2001, 2002 } };
static constexpr uint32_t DECK_SIZE[2][2] = { { 20, 7 }, { 20, 20 } };
// Scripted effect monster on team 1's field (raw constant values: no constant.lua is loaded)
static constexpr uint32_t MOVER_CODE = 3000;
static const char MOVER_SCRIPT[] = R"(
-- the core creates the c3000 table before loading this script
function c3000.initial_effect(c)
	local e=Effect.CreateEffect(c)
	e:SetType(0x2+0x800)      -- EFFECT_TYPE_FIELD+EFFECT_TYPE_CONTINUOUS
	e:SetCode(0x1000+0x2)     -- EVENT_PHASE+PHASE_STANDBY
	e:SetRange(0x4)           -- LOCATION_MZONE
	e:SetCountLimit(1)        -- phase effects need a limit, or the core keeps re-running the phase
	e:SetOperation(c3000.op)
	c:RegisterEffect(e)
end
function c3000.op(e,tp)
	local t=Duel.GetTurnCount()
	if t==4 then
		Duel.SendtoGrave(Duel.GetFieldGroup(tp,0x2,0),0x40)        -- hand to GY (REASON_EFFECT)
		Debug.SetTeamDestDuelist(tp,0)                             -- duelist 0 chosen
		Duel.SendtoHand(Duel.GetFieldGroup(tp,0x10,0),nil,0x40)    -- GY to hand
		Debug.SetTeamDestDuelist(tp,255)                           -- clear the choice
	elseif t==6 then
		Duel.SendtoGrave(Duel.GetFieldGroup(tp,0x2,0),0x40)
		Duel.SendtoHand(Duel.GetFieldGroup(tp,0x10,0),nil,0x40)    -- no choice: back to owners
	end
end
)";

static void card_reader(void*, uint32_t code, OCG_CardData* data) {
	std::memset(data, 0, sizeof(*data));
	static uint16_t no_setcodes[1] = { 0 };
	data->code = code;
	data->setcodes = no_setcodes;
	data->type = TYPE_MONSTER | TYPE_NORMAL;
	data->level = 4;
	data->attribute = ATTRIBUTE_EARTH;
	data->race = RACE_WARRIOR;
	data->attack = 1000;
	data->defense = 1000;
	if(code == MOVER_CODE)
		data->type = TYPE_MONSTER | TYPE_EFFECT;
}
static void card_reader_done(void*, OCG_CardData*) {}
static int script_reader(void*, OCG_Duel duel, const char* name) {
	if(std::strstr(name, "c3000.lua"))
		return pLoadScript(duel, MOVER_SCRIPT, sizeof(MOVER_SCRIPT) - 1, name);
	return 0; // vanilla cards need no scripts
}
static void log_handler(void*, const char* str, int type) {
	if(type == OCG_LOG_TYPE_ERROR)
		std::printf("  [core error] %s\n", str);
}

// Count cards in a location per duelist using QUERY_DUELIST
static void loc_by_duelist(OCG_Duel duel, uint8_t team, uint32_t loc, uint32_t out[2], std::vector<uint8_t>* order = nullptr) {
	out[0] = out[1] = 0;
	OCG_QueryInfo qi{};
	qi.flags = QUERY_DUELIST;
	qi.con = team;
	qi.loc = loc;
	uint32_t len = 0;
	auto* buf = static_cast<const uint8_t*>(pQueryLocation(duel, &len, &qi));
	const uint8_t* p = buf + 4; // total size
	const uint8_t* end = buf + len;
	while(p < end) {
		uint16_t size; std::memcpy(&size, p, 2); p += 2;
		if(size == 0) continue;
		uint32_t flag; std::memcpy(&flag, p, 4);
		if(flag == QUERY_DUELIST) {
			uint8_t d = p[4];
			if(d < 2) ++out[d];
			if(order) order->push_back(d);
		}
		p += size;
	}
}
static void hand_by_duelist(OCG_Duel duel, uint8_t team, uint32_t out[2]) { loc_by_duelist(duel, team, LOCATION_HAND, out); }

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
	opts.seed[0] = 1; opts.seed[1] = 2; opts.seed[2] = 3; opts.seed[3] = 4;
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
			ci.code = CODE[t][d];
			for(uint32_t i = 0; i < DECK_SIZE[t][d]; ++i)
				pNewCard(duel, &ci);
		}
	}
	{
		OCG_NewCardInfo ci{};
		ci.team = 1; ci.con = 1; ci.duelist = 0; ci.loc = LOCATION_MZONE; ci.seq = 0; ci.pos = POS_FACEUP_ATTACK;
		ci.code = MOVER_CODE;
		pNewCard(duel, &ci);
	}
	CHECK(pQueryCount(duel, 0, LOCATION_DECK) == 27, "team 0 deck should hold both decks (27), got %u", pQueryCount(duel, 0, LOCATION_DECK));
	pStartDuel(duel);

	int turn = 0;
	uint8_t turn_player = 0;
	int draws_this_turn[2][2] = {};
	bool opening_checked = false;
	bool saw_hand_limit_discard = false;
	bool checked_moves[2] = {};
	int winner = -1, win_reason = -1;
	bool done = false;
	int guard = 0;

	auto check_opening = [&] {
		for(uint8_t t = 0; t < 2; ++t) {
			uint32_t h[2]; hand_by_duelist(duel, t, h);
			CHECK(h[0] == 5 && h[1] == 5, "team %u opening hands should be 5/5, got %u/%u", t, h[0], h[1]);
		}
		opening_checked = true;
	};

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
			uint8_t msg = m[0];
			const uint8_t* d = m + 1;
			switch(msg) {
			case MSG_NEW_TURN: {
				turn_player = d[0];
				++turn;
				std::memset(draws_this_turn, 0, sizeof(draws_this_turn));
				break;
			}
			case MSG_DRAW: {
				uint8_t pl = d[0]; uint32_t cnt; std::memcpy(&cnt, d + 1, 4);
				// Each drawn card is code(4)+position(4): the code tells us whose Deck it came from
				for(uint32_t i = 0; i < cnt; ++i) {
					uint32_t code; std::memcpy(&code, d + 5 + i * 8, 4);
					int du = (code == CODE[pl][0]) ? 0 : (code == CODE[pl][1]) ? 1 : -1;
					CHECK(du >= 0, "drew a card (%u) that is not from team %u's decks", code, pl);
					if(du >= 0) ++draws_this_turn[pl][du];
				}
				break;
			}
			case MSG_SELECT_IDLECMD: {
				if(!opening_checked)
					check_opening();
				// Draw checks for this turn
				if(turn == 1) {
					CHECK(draws_this_turn[0][0] == 0 && draws_this_turn[0][1] == 0, "turn 1: the first team must not draw");
				} else {
					uint8_t tp = turn_player;
					uint32_t left1 = pQueryCount(duel, tp, LOCATION_DECK);
					(void)left1;
					CHECK(draws_this_turn[tp][0] == 1, "turn %d: duelist 0 of team %u should draw 1, drew %d", turn, tp, draws_this_turn[tp][0]);
					CHECK(draws_this_turn[tp][1] == 1, "turn %d: duelist 1 of team %u should draw 1, drew %d", turn, tp, draws_this_turn[tp][1]);
				}
				if(turn == 4) {
					// 7/7 in hand (5 opening + draws on turns 2 and 4) went to the GY and back with duelist 0 chosen
					uint32_t h[2]; hand_by_duelist(duel, 1, h);
					std::printf("  turn 4: team 1 hand after hand -> GY -> hand (duelist 0 chosen): %u/%u\n", h[0], h[1]);
					CHECK(h[0] == 14 && h[1] == 0, "with duelist 0 chosen, every card should join duelist 0's hand (want 14/0)");
					checked_moves[0] = true;
				}
				if(turn == 6) {
					// Turn 4 End Phase discarded 8 from duelist 0's 14, turn 6 drew 1 each (hand 7/1).
					// Hand + GY (16 cards: 8 owned by each duelist) then went back to the hand with no choice.
					uint32_t h[2]; hand_by_duelist(duel, 1, h);
					std::printf("  turn 6: team 1 hand after hand -> GY -> hand (no choice): %u/%u\n", h[0], h[1]);
					CHECK(h[0] == 8 && h[1] == 8, "with no choice, every card should return to its owner duelist's hand (want 8/8)");
					checked_moves[1] = true;
				}
				int32_t resp = 7; // go to End Phase
				pSetResponse(duel, &resp, sizeof(resp));
				responded = true;
				break;
			}
			case MSG_SELECT_BATTLECMD: {
				int32_t resp = 3; // go to End Phase
				pSetResponse(duel, &resp, sizeof(resp));
				responded = true;
				break;
			}
			case MSG_SELECT_CHAIN: {
				int32_t resp = -1;
				pSetResponse(duel, &resp, sizeof(resp));
				responded = true;
				break;
			}
			case MSG_SELECT_CARD: {
				// End Phase hand-limit discard. Verify only one duelist's cards are offered.
				uint8_t pl = d[0];
				uint32_t mn, mx, cnt;
				std::memcpy(&mn, d + 2, 4); std::memcpy(&mx, d + 6, 4); std::memcpy(&cnt, d + 10, 4);
				// Each offered card: code(4) con(1) loc(1) seq(4) pos(4). Its duelist tag comes from
				// the hand query at that sequence (card codes no longer say whose hand a card is in).
				std::vector<uint8_t> hand_tags;
				{ uint32_t tmp[2]; loc_by_duelist(duel, pl, LOCATION_HAND, tmp, &hand_tags); }
				const uint8_t* c = d + 14;
				int du_seen[2] = {};
				for(uint32_t i = 0; i < cnt; ++i) {
					uint32_t seq; std::memcpy(&seq, c + 6, 4);
					if(seq < hand_tags.size() && hand_tags[seq] < 2) ++du_seen[hand_tags[seq]];
					c += 4 + 1 + 1 + 4 + 4;
				}
				uint32_t h[2]; hand_by_duelist(duel, pl, h);
				std::printf("  turn %d: hand-limit discard for team %u: hands %u/%u, offered d0=%d d1=%d, must pick %u\n",
				            turn, pl, h[0], h[1], du_seen[0], du_seen[1], mn);
				CHECK((du_seen[0] == 0) != (du_seen[1] == 0), "hand-limit discard must offer only one duelist's hand");
				int over = du_seen[0] ? 0 : 1;
				CHECK(h[over] > 6 && mn == h[over] - 6, "discard count should bring that duelist to 6");
				saw_hand_limit_discard = true;
				std::vector<uint32_t> resp = { 0u, mn };
				for(uint32_t i = 0; i < mn; ++i) resp.push_back(i);
				pSetResponse(duel, resp.data(), static_cast<uint32_t>(resp.size() * 4));
				responded = true;
				break;
			}
			case MSG_WIN: {
				winner = d[0]; win_reason = d[1];
				done = true;
				break;
			}
			case MSG_RETRY: {
				std::printf("  core asked for a retry (invalid response)\n");
				++failures; done = true;
				break;
			}
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

	// Team 0 duelist 1 has 7 cards: 5 opening + 1 on turns 3, 5 → empty on turn 7 → team 1 wins by deck-out.
	std::printf("  duel ended on turn %d, winner team %d, reason %d\n", turn, winner, win_reason);
	CHECK(opening_checked, "never reached the first main phase");
	CHECK(winner == 1, "team 1 should win when team 0's duelist 1 runs out of cards");
	CHECK(win_reason == WIN_REASON_DECK_OUT, "win reason should be deck-out (%d), got %d", WIN_REASON_DECK_OUT, win_reason);
	CHECK(turn == 7, "deck-out should happen on turn 7, happened on turn %d", turn);
	CHECK(saw_hand_limit_discard, "expected a per-duelist hand-limit discard");
	CHECK(checked_moves[0] && checked_moves[1], "the scripted move checks on turns 4 and 6 did not run");
	pDestroyDuel(duel);
	std::printf(failures ? "FAILED (%d)\n" : "PASSED\n", failures);
	return failures ? 1 : 0;
}
