#pragma once

#include <stdint.h>

#include "nf7.h"


// A maximum number of modules which nf7core may provide.
extern const uint32_t NF7CORE_MAX_MODS;


// Initializes all modules and returns their instances.
// sizeof(mods)/sizeof(mods[0]) should be equal to NF7CORE_MAX_MODS
uint32_t nf7core_new(struct nf7* nf7, struct nf7_mod** mods);
