#!/usr/bin/env bash

set -e
set -x

make
for f in test/*json; do
  echo $f
  ./re indeg rle-tuned -f $f
done

echo "Use this command to search the output:"
echo "egrep '(Reading)|(regex:)|(max observed)'"
