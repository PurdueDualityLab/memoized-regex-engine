#!/usr/bin/env perl 
# Quick sanity check on the asymptotic vs. memory bytes reported

use strict;
use warnings;
use JSON::PP;

my @reg = ("a*a*a*\$", "(aa*)*\$", "(a|a)*\$", ".*a.*a.*\$");
my @str = map { "a"x$_ . "!" } (1, 10, 100, 1000, 10000, 20000);
@str = map { "a"x$_ . "!" } (100, 20000);
my @encMode = qw/ none neg rle /;

for my $reg (@reg) {
    for my $str (@str) {
        print "matching $reg against len " . (length $str) . "\n";
        for my $enc (@encMode) {
            my $out = `../re indeg $enc '$reg' '$str' 2>&1 >/dev/null`;
            chomp $out;
            my $obj = decode_json($out);
            print "  $enc  asymptotic: @{$obj->{memoizationInfo}->{results}->{maxObservedAsymptoticCostsPerMemoizedVertex}}\n";
            print "  $enc bytes: @{$obj->{memoizationInfo}->{results}->{maxObservedMemoryBytesPerMemoizedVertex}}\n";
        }
    }
}