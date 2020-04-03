#!/usr/bin/env bash

set -x

REALPATH=`realpath $0`
QUERY_CSHARP_DIR=`dirname $REALPATH`

mono $QUERY_CSHARP_DIR/QueryCSharp.exe $@
