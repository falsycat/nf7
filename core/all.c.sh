#!/bin/bash

echo "#include \"core/all.h\""
echo
echo "#include <assert.h>"
echo
echo "const uint32_t NF7_CORE_MAX_MODS = UINT32_C($#);"
echo
echo "uint32_t nf7_core_new(const struct nf7* nf7, struct nf7_mod** mods) {"
echo "  assert(nullptr != nf7);"
echo "  assert(nullptr != mods);"
echo
echo "  uint32_t i = 0;"
echo
for name in $@; do
  echo "  extern struct nf7_mod* nf7_core_${name}_new(const struct nf7*);"
  echo "  mods[i] = nf7_core_${name}_new(nf7);"
  echo "  if (nullptr != mods[i]) {"
  echo "    assert(nullptr != mods[i]->meta);"
  echo "    ++i;"
  echo "  }"
  echo
done
echo "  return i;"
echo "}"
