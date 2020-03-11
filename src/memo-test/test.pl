#!/usr/bin/env perl

#use re 'debug';
use strict;
use warnings;

#use re ‘Debug’ => ‘ALL’;

#/(.\d+){6}$/
#    {'suffix': 'a', 'couldParse': True, 'pumpPairs': [{'prefix': 'a0', 'pump': 'a0'}, {'prefix': '0', 'pump': 'a0'}, {'prefix': '0', 'pump': 'a0'}, {'prefix': '0', 'pump': 'a0'}, {'prefix': '0', 'pump': 'a0'}]},
#    {'suffix': '475353475353718657546955.', 'couldParse': True, 'pumpPairs': [{'prefix': '..', 'pump': '475353'}]},
#    {'suffix': '08262008262082639826208f2$', 'couldParse': True, 'pumpPairs': [{'prefix': 'A7b', 'pump': '082620'}]}


#if ("a" x 500 . "!" =~ m/<a.*href="(.*)".*>(.*)<\/a>/) {
my $input = "'" . "0" x 1000 . ".";
print "$input\n";
#if ($input =~ m/\d\d*\d*\d*\d*\d*\d*$/) { # Not protected on 0000 -- Only STAR, no CURLYX
#if ($input =~ m/\d\d+\d+\d+\d+\d+\d+$/) { # Not protected on 0000 -- Only PLUS, no CURLYX
#if ($input =~ m/(.\d+){6}$/) { # Not protected on 0000 -- Wrong kind of CURLYX
#if ($input =~ m/(.\d+)+$/) { # Protected by SL cache on 0000.
#if ($input =~ m/(\d+)+$/) { # Protected by SL cache on 0000.
#if ($input =~ m/([\d]+)*$/) { # Protected by SL cache on 0000.
#if ($input =~ m/(\d+)*\1$/) { # Cache cannot help; we wipe it whenever we test the backref
#if ($input =~ m/('|")(\d+)*\1$/) { # Cache cannot help; we wipe it whenever we test the backref
#if ($input =~ m/\d(\d?)*(\d?)*(\d?)*(\d?)*(\d?)*(\d?)*$/) { # Protected by SL cache on 0000. -- For A* = (\d?)* , the A is not fixed-width, so Perl expands this into a CURLYX{0,INFTY}. It caches those!
#if ($input =~ m/(\d{1,500}){1,500}$/) { # Not protected, because not a CURLYX -- bounded quantifiers
if ($input =~ m/(\d*){1,100}$/) { # Not protected, because not a CURLYX -- bounded quantifiers
#if ($input =~ m/(\d|\d)+$/) { # Not protected, because not a CURLYX -- fixed-width A
#if ($input =~ m/(\d*)+(\d*)+(\d*)+(\d*)+(\d*)+(\d*)+(\d*)+(\d*)+(\d*)+(\d*)+(\d*)+(\d*)+(\d*)+(\d*)+(\d*)+$/) { # Protected -- 16 CURLYX
#if ($input =~ m/(\d?)+(\d?)+(\d?)+(\d?)+(\d?)+(\d?)+(\d?)+(\d?)+(\d?)+(\d?)+(\d?)+(\d?)+(\d?)+(\d?)+(\d?)+(\d*)+$/) { # Not protected -- 17 CURLYX
  print "Match\n";
} else {
  print "No match\n";
}
