// Skins / outfits, carried over from the TLOU Outfit Changer: opens Extras >
// Skins and the in-game Pause > Skins page without finishing the story, and
// makes every skin owned and equippable, so outfits can be changed in-game
// through the game's own Skins page. Driven by one byte the menu toggles.
#pragma once
#include <cstddef>
#include <vector>

namespace skins {
void Register();
// Locates and patches the skin checks. Returns the disp32 offsets of the two
// menu reads of the "Unlock All Extras Content" flag it took over, so the
// Extras unlock leaves them alone. Empty when skins are unavailable.
std::vector<size_t> Install();
} // namespace skins
