#pragma once
#define MENU_VERSION "1.1.0"

// Joel/Ellie character swap (src/character.cpp), shipped as Experimental:
// in Joel's chapters Ellie's player animation packages are not loaded, so a
// swapped Ellie may be unable to move. 0 = leave it out of the build.
#ifndef W1Z_CHARACTER_SWAP
#define W1Z_CHARACTER_SWAP 1
#endif
