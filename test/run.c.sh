#!/bin/bash

tests=$(cat $@ | sed -e '/^NF7_TEST(/!d; s|^NF7_TEST(\([^)]*\)).*$|\1|' | xargs echo)

echo "#include \"test/common.h\""
echo "#include \"test/run.h\""
echo
echo
echo "bool nf7_test_run(struct nf7_test* test) {"
echo "  // $tests"
for test in $tests; do
  echo "  extern bool $test(struct nf7_test*);"
  echo "  test->run(test, \"$test\", $test);"
done
echo "  return true;"
echo "}"

