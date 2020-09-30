#!/usr/bin/env perl

use strict;
use warnings;

my $srcPath = "../src-simple";
my $suff = "[ch]";

my @memoFiles = (
  glob("$srcPath/memoize.$suff"),
  glob("$srcPath/rle.$suff"),
);

print "memo files:\n";
print &cmd("cloc @memoFiles");

my @testFiles = (
  glob("$srcPath/log.$suff"),
  glob("$srcPath/rle-test.$suff"),
  glob("$srcPath/../eval/unittest-prototype.py"),
);
my @testTxtFiles = (
  glob("$srcPath/test/*txt")
);

print "test files:\n";
print &cmd("cloc @testFiles");
print &cmd("wc -l @testTxtFiles");

######

sub cmd {
  my ($cmd) = @_;
  print "CMD: $cmd\n";
  return `$cmd`;
}
