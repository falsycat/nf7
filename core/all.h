#pragma once

#include <stdint.h>

#include "nf7.h"


// A maximum number of modules which nf7core may provide.
extern const uint32_t NF7_CORE_MAX_MODS;


// Initializes all modules and returns their instances.
uint32_t nf7_core_new(const struct nf7* nf7, struct nf7_mod** mods);
// PRECONDS:
//   - `nf7` is a valid pointer
//   - `mods` is a valid pointer
//     to memory large enough to keep `NF7_CORE_MAX_MODS` pointers
// POSTCONDS:
//   - When x is lower than the return value,
//     mods[x] is a valid pointer to an initialized module
