#!/bin/bash

echo "#include \"core/all.h\""
echo
echo "#include <assert.h>"
echo
echo "#include \"util/log.h\""
echo
echo "const uint32_t NF7CORE_MAX_MODS = UINT32_C($#);"
echo
echo "uint32_t nf7core_new(const struct nf7* nf7, struct nf7_mod** mods) {"
echo "  assert(nullptr != nf7);"
echo "  assert(nullptr != mods);"
echo
echo "  uint32_t i = 0;"
echo
for name in $@; do
  echo "  extern const struct nf7_mod_meta nf7core_${name};"
  echo "  extern struct nf7_mod* nf7core_${name}_new(const struct nf7*);"
  echo "  nf7util_log_debug(\"loading module: %s\", nf7core_${name}.name);"
  echo "  mods[i] = nf7core_${name}_new(nf7);"
  echo "  if (nullptr != mods[i]) {"
  echo "    assert(nullptr != mods[i]->nf7);"
  echo "    assert(nullptr != mods[i]->meta);"
  echo "    ++i;"
  echo "    nf7util_log_info(\"loaded module: %s\", nf7core_${name}.name);"
  echo "  } else {"
  echo "    nf7util_log_warn(\"failed to load module: %s\", nf7core_${name}.name);"
  echo "  }"
  echo
done
echo "  return i;"
echo "}"
