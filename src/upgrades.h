// Weapon upgrades: add one specific upgrade to a gun (a scope on the hunting
// rifle, a bigger magazine on the 9mm, ...) or take a gun's upgrades off,
// through the game's own player-weapon-upgrade-apply-bundle / -remove-bundle
// script functions. Changes the live game; the game saves them at checkpoints.
#pragma once

namespace upgrades {
void Register();
// After engine::Init (needs the game-loop hook and the player lookup).
void Init();
} // namespace upgrades
