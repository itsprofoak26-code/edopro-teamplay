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
- When the effect **puts cards into, or draws from,** a hand or Deck and the card's
  current zone does not already say which duelist it belongs to, **choose a duelist** to apply it to.
  E.g. "draw 1", "add 1 card from your GY to your hand", "shuffle into your Deck".

### 3. Effects on the opposing team
Whenever an effect will affect the **opponent's** hand, Deck or Extra Deck, the activating
player **chooses one opposing duelist** to apply it to.
E.g. "your opponent discards 1", "look at your opponent's hand", "your opponent draws 1".

### When the choice is made
A duelist is chosen **when the effect is activated** and shown to everyone, so players know
who is affected before they respond. Choosing a duelist is a team-play procedure, **not
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

### Known gaps (planned for Milestone 2: effect scope)
- **Effect draws:** "draw" from a card effect uses that card's duelist. Otherwise it uses the duelist with more cards left. Nobody chooses a duelist yet.
- **Other hand/Deck effects:** searches, milling, looking at the top of the Deck and card counts still see the whole team's hand and Deck.
- **Shuffling:** shuffling the Deck mixes both duelists' cards together. Each card still keeps its owner tag.
- **Client:** shows one Deck pile and one Extra Deck pile with combined counts.
- **Replays:** not tested.
