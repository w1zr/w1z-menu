// Character swap: play as Ellie in Joel's chapters (or as Joel in Ellie's),
// switchable at any time. Every player spawn goes through one engine function
// that receives the character to spawn (t1x-joel / t1x-ellie); the menu swaps
// that id. "Swap Now" respawns the player on the spot with the developer
// menu's own Spawn Player, so the change is immediate; checkpoints, deaths
// and chapter loads also use the chosen character.
#pragma once

namespace character {
void Register();
// After engine::Init (needs the game-loop hook).
void Init();
} // namespace character
