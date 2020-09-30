#!/usr/bin/env perl

use strict;
use warnings;

my $firstCommit = "25b9c9125928eb";

my @coreFiles = map { "../src-simple/$_" } qw / regexp.h main.c parse.y compile.c sub.c backtrack.c /;
print &cmd("./gcount.sh ${firstCommit}..HEAD @coreFiles");


sub cmd {
  my ($c) = @_;
  print "CMD: $c\n";
  return `$c`;
}