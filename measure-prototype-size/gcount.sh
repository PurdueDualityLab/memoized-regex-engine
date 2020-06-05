#!/usr/bin/env bash
git log --numstat --format="" "$@" | awk '{files += 1}{ins += $1}{del += $2} END{print "total: "files" files, "ins" insertions(+) "del" deletions(-)"}'
