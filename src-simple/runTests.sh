#!/usr/bin/env bash

set -e
set -x

make
for f in test/*json; do
  echo $f
  ./re indeg rle-tuned -f $f
done
