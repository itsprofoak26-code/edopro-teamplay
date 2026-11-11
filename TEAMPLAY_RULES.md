# Team Play Mode — Rules (draft v0.3)

A 2v2 mode where **both teammates are active at the same time**. The rules engine still
sees two players (team 0 and team 1); each team has two **duelists** whose private zones are
live at once. ("Option B" — see Architecture below.)

## Zones

| Zone | Shared by the team? |
|---|---|
| Field (Monster / Spell & Trap / Field / Extra Monster Zones) | Shared — same size as a normal duel |
| Graveyard | Shared |
| Banished | Shared |
| Life Points | Shared |
| Hand | **Private** (per duelist) |
| Main Deck | **Private** (per duelist) |
| Extra Deck | **Private** (per duelist) |

## Setup and turn structure

- **Opening hand:** each duelist draws 5.
- **Turn 1:** both duelists on the team going first skip their draw.
- **Draw Phase:** both teammates draw 1.
- **Normal Summon / Set:** once per **team** per turn, unless an effect grants more.
- **Hand size limit:** per duelist. In the End Phase, only cards from a hand that is over the
  limit can be discarded, but **either teammate** may make the choice. E.g. duelist 1 holds 8
  and duelist 2 holds 6: either player picks the discard, and only duelist 1's cards are valid.
- **Who acts:** either teammate, whoever responds first. This covers your own turn's open
  game state, chain responses on either turn, and every prompt from an effect, including
  choices for an effect activated from your teammate's hand.
- **Simultaneous answers:** the first answer the server receives is used. The other teammate's
  prompt closes.
- **Time limit:** one clock per **team**.

## Extra Deck

- Either teammate can Special Summon from **either** Extra Deck (summon procedures and
  effects of cards in shared zones are team-wide).
- An effect activated from a duelist's hand still reaches only that duelist's Extra Deck
  (rule 1 below).
- Face-up Pendulum Monsters sent to the Extra Deck go to their **owner duelist's** Extra Deck.

## Effect scope (team-play rule)

Team play does not change any card's text. It adds a rule for **which hands, Decks and
Extra Decks an effect can reach**. That depends on **where the effect is activated**:

- **The card goes onto the field to activate** → team-wide (rule 2). This covers Spells and
  Traps played from the hand, Field Spells, and Pendulum Scales.
- **The effect activates while the card is still in the hand** → bound to that duelist
  (rule 1). This holds even if the card leaves the hand as a cost (e.g. a hand trap that
  discards itself).

### 1. Effects activated from a private zone (hand, Deck, Extra Deck)
The effect is **bound to the duelist who holds the card**.
- It can see and affect only **that duelist's** hand, Deck and Extra Deck, plus the
  shared zones (field, GY, banished).
- It cannot see or affect the teammate's hand, Deck or Extra Deck.
- "Your hand" / "your Deck" means that duelist's. No choice is needed on your own side.

### 2. Effects of cards in a shared zone (field, GY, banished)
The effect has **team-wide access**.
- **Seeing, counting and selecting** cover all teammates' hands, Decks and Extra Decks
  together. E.g. "+100 ATK for each card in your hand" counts both hands, and
  "discard 1 card" can discard from either hand.
- When the effect **draws, mills, excavates or uses the top of a Deck**, or **puts cards into a
  hand or Deck** from the GY or banished, **choose a duelist** to apply it to.
  E.g. "draw 1", "send the top 3 cards of your Deck to the GY", "excavate the top 3 cards",
  "add 1 card from your GY to your hand", "shuffle into your Deck".
- **Once a duelist is chosen for a team, the whole effect applies to that duelist only** for
  that team: their hand, Deck and Extra Deck. E.g. Graceful Charity: the chosen duelist draws 3
  and discards 2 from their own hand. Card Destruction: one chosen duelist on each team discards
  their hand and draws.
- Effects with no choice keep team-wide access (e.g. a searcher lists both Decks).

### 3. Effects on the opposing team
Whenever an effect will affect the **opponent's** hand, Deck or Extra Deck, the activating
player **chooses one opposing duelist** to apply it to.
E.g. "your opponent discards 1", "look at your opponent's hand", "your opponent draws 1".

### When the choice is made
A duelist is chosen **when the effect is activated** and shown to everyone, so players know
who is affected before they respond. If a cost already needs it (e.g. Card Trooper milling as
a cost), the choice is made then and used for the whole effect. Exception: an effect that does
not declare what it will do (no operation info, e.g. Pot of Duality) is asked the first time it
reaches a Deck or hand, which can be while it resolves. Choosing a duelist is a team-play procedure, **not
targeting**. It does not interact with "cannot be targeted" or "change the target player".

### Returning to the owner
"Return to its owner's hand/Deck/Extra Deck" sends the card back to the duelist whose Deck it
started in. No duelist is chosen.

### Continuous effects
Effects that are never activated (e.g. a field card whose ATK = cards in your hand) follow
where the card is. On the field, GY or banished: team-wide (rule 2).

## Viewing

- You can always see your partner's hand.
- The client has a hand-view toggle: **both hands**, **my hand**, or **partner's hand**.
- Each side shows **two Deck piles and two Extra Deck piles**, one per duelist, each with its
  own count. Duelist 1 is highlighted **red** and duelist 2 **blue** (placeholder colours).
- The duel log puts the **team and player name** before each action, so everyone can see
  which teammate activated or answered.

## Compatibility and updates

- Players install the team-play build. Card scripts and databases keep updating from the
  official repositories as usual.
- The official engine (ocgcore) is **not** taken from the repositories, because it lacks team
  play. New official engine changes must be merged into our engine by hand, so newly
  released cards that need new engine features keep working.
- Later: explore letting official clients join a hosted team-play room.

## Winning and losing

- A team loses when its shared LP reaches 0.
- A team loses when **either** duelist must draw from an empty Deck.

## Architecture (Option B)

- `ocgcore` keeps 2 players. Hand, Deck and Extra Deck cards carry a **duelist index**.
  Each card also remembers which duelist's Deck it started in (its **owner duelist**).
  Today's `field::tag_swap` (stored-lists swap at turn start) is disabled for this mode.
- Each chain link stores a **scope** for each team: *one duelist* or *team-wide*.
  - The scope comes from where the effect was activated (`chain::triggering_location`) and
    from the duelist choices made at activation.
  - The core already records activation location the way rule 1 vs 2 needs:
    - Spells and Traps from the hand are moved to the field first. `set_triggering_state` is
      then called again, so the recorded location is the field (`processor.cpp` ~3689–3698).
    - Hand effects record `LOCATION_HAND` before any cost is paid.
  - Every core lookup of a hand, Deck or Extra Deck for a team (about 160 uses of
    `list_hand/list_main/list_extra`, mostly in `field.cpp`) is filtered by that scope.
  - Existing card scripts work unchanged.
- Server (`edopro/gframe/generic_duel.cpp`) sends prompts to both teammates and accepts
  the first valid response.
- Client shows two hands plus two Deck/Extra Deck piles per side, and the hand-view toggle.

## Implementation status

### Milestone 1: both hands live (done, waiting on a LAN test)
- **Host:** press **Team** next to Relay on the host screen. It only applies to 2 vs 2 games; any other team size ignores it.
- **Core** (`DUEL_TEAM_PLAY`):
  - Each card is tagged with its duelist.
  - Each duelist draws a 5-card opening hand. The first team skips the turn-1 draw. After that, both duelists on the turn team draw.
  - The hand size limit applies to each duelist separately.
  - A team loses when either duelist must draw from an empty Deck.
  - `tag_swap` is off.
- **Server:**
  - Every prompt goes to both teammates. The first answer is used, and the teammate's prompt is closed.
  - A late answer that was already in flight gets dropped (`CTOS_TEAM_SYNC` handshake).
- **Client:** each duelist sees their own hand by default. The **Hand** button cycles *Mine → Partner → Both*; with Both, each hand is shown as its own group. You can only hover or click the cards on display. Observers and replays always show the whole team hand.
- **Test:** `tests/run_core_test.ps1` runs a headless team-play duel and checks draws, the hand size limit and deck-out.

### Milestone 2: effect scope (in progress)
- **Owner duelist (done):** each card remembers which duelist's Deck it started in. When a card
  enters a hand, Deck or Extra Deck:
  - if a duelist was chosen for the move, it goes to that duelist;
  - if it moves between one team's private zones (draw, search, hand to Deck), it keeps its duelist;
  - otherwise (from the field, GY or banished) it returns to its owner duelist.
  The choice is a per-team override in the core (`team_dest_duelist`). The duelist-choice prompt
  will set it. For now only the test hook `Debug.SetTeamDestDuelist(team, duelist)` does.
- **Shuffling (no change needed):** each duelist's Deck is the cards carrying their tag, in list
  order. Shuffling the whole list gives each duelist an independently shuffled Deck, and
  "top of Deck" for a duelist is their last tagged card.
- **Effect scope, rule 1 (done):** `field::team_scope(team)` gives the duelist the current effect
  is bound to, from `core.reason_effect`:
  - a chain link being activated or resolved uses where it was activated and the card's duelist
    at that moment (`chain::triggering_duelist`), so a hand trap that discards itself as a cost
    stays bound;
  - before a chain link exists (activation checks) and for continuous effects, it uses where the
    card is now; activating a Spell/Trap card itself is always team-wide.
  Searches, counts and selections (`filter_matching_card`, `filter_field_card`: e.g.
  `GetMatchingGroup`, `IsExistingMatchingCard`, `GetFieldGroupCount`, `SelectMatchingCard`,
  `DiscardHand`) skip the other duelist's hand, Deck and Extra Deck.
- **Top/bottom of the Deck (done):** milling, `GetDecktopGroup`/`GetDeckbottomGroup`,
  `ConfirmDecktop`, Extra Deck top, Sort Decktop, `IsPlayerCanDraw(count)`, the discard-Deck
  checks and effect draws act on **one duelist's Deck**: the scoped duelist; else the chosen
  duelist; else (until the choice prompt exists) the duelist with more cards. The core moves that
  duelist's cards to the top (or bottom) of the shared list first. Each duelist's own order is
  unchanged, so this does not change the game.
- **Test:** the core test checks a duelist choice, return to owner, and a hand effect's scope
  (sees 7 of 13 hand cards and 14 of 15 Deck cards, mills from its own duelist's Deck).

- **Duelist choice (done):** when a chain link is activated (after costs and targets), the core
  reads the effect's operation info and asks the activating team, either teammate, which duelist
  it applies to:
  - **own team** (rules 1 and 2): only for team-wide effects (not activated from a hand) that draw
    (`CATEGORY_DRAW`), mill (`CATEGORY_DECKDES`), or add/return cards to the hand or Deck
    (`CATEGORY_TOHAND`/`CATEGORY_TODECK`) from the GY or banished;
  - **opposing team** (rule 3): for any effect that makes that team discard (`CATEGORY_HANDES`),
    draw or mill.
  The prompt is an option window ("Red: name" / "Blue: name"). The choice is announced to everyone
  ("Your choice / Your opponent's choice: [Blue: name]") and stored on the chain link
  (`chain::team_choice`). For the rest of that link (cost, target and resolution), for that team:
  - its hand, Deck and Extra Deck are limited to the chosen duelist (either team);
  - draws, mills and Deck-top effects use the chosen duelist's Deck;
  - cards moving into its hand or Deck go to the chosen duelist.
  The server sends every duelist's name to all clients and observers at duel start
  (`MSG_TEAM_INFO`); the client turns them into the option strings (`TEAM_DUELIST_DESC`).
- **Asked when first needed (done):** if a team-wide chain link has no choice yet, the core asks
  (`Processors::TeamChooseDuelist`) just before `Duel.Draw`, `Duel.DiscardDeck` (also as a cost),
  `Duel.SortDecktop/SortDeckbottom`, `Duel.ConfirmDecktop`, and `Duel.SendtoHand/SendtoDeck` of
  cards from the GY or banished. A choice made during the cost is reused at activation, so there
  is never a second prompt for the same team.
- **Tests:** `teamplay_core_test` (a both-players draw: both prompts, both announcements, draws
  from the chosen Decks) and `teamplay_choice_test` (a mill cost like Card Trooper asks once;
  a reveal-top effect with no operation info like Pot of Duality asks at the reveal; a
  Card Destruction-like effect makes only one chosen duelist per team discard and redraw).

### Known gaps (still to do in Milestone 2)
- **Some reads have no choice point.** Effects that only *look* (e.g. at the opponent's hand)
  or read the Deck top through `GetDecktopGroup` before anything above runs, and effects that
  are not chain links (continuous effects), never prompt. They fall back to team-wide access,
  the owner duelist, or the duelist with more cards.
- **Choice is per team, per chain link.** An effect that adds two cards to the hand puts both
  with the same duelist.
- **Replays:** the "Red/Blue: name" strings come from `MSG_TEAM_INFO`, which is not in the replay
  stream, so replays show unknown strings for the choice.
- **Core code that reads the hands directly, not through the search functions:** Fusion and
  Ritual material lists, Tribute lists from the hand, random hand discards. These still see the
  whole team hand.
- **Reversed Deck display:** the face-up top card of a reversed Deck is shown for the combined list.
- **Client:**
  - shows one Deck pile and one Extra Deck pile with combined counts;
  - is not told when the core reorders a team's Deck list. This is harmless today because Deck
    cards are anonymous, but the red/blue per-duelist piles will need it.
- **Replays:** not tested.
