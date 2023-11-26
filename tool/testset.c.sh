#!/bin/bash

cat $@ | sed -e ''

echo "bool nf7_test_run(void) {"
echo "  // $@"
echo "  return true;"
echo "}"

