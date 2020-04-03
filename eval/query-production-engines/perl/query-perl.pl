#!/usr/bin/env perl
# Author: Jamie Davis <davisjam@vt.edu>
# Description: Try REDOS attack on Perl
# This is copied from the vuln-regex-detector project, with some extensions.

use strict;
use warnings;

use JSON::PP; # I/O
use Carp;

$| = 1; # Auto-flush stdout for IPC -- You give me hot pipes!

# Arg parsing.
my $queryFile = $ARGV[0];
if (not defined($queryFile)) {
  print "Error, usage: $0 query-file.json\n";
  exit 1;
}

# Load query from file.
&log("Loading query from $queryFile");
my $query = decode_json(&readFile("file"=>$queryFile));

# Check query is valid.
my $validQuery = 1;
my @requiredQueryKeys = ('pattern', 'evilInput', 'nPumps');
for my $k (@requiredQueryKeys) {
  if (not defined($query->{$k})) {
    $validQuery = 0;
  }
};
if (not $validQuery) {
  &log("Error, invalid query. Need keys <@requiredQueryKeys>. Got " . encode_json($query));
  exit 1;
}

# Compose evil input
my $inputString = "";
for my $pumpPair (@{$query->{evilInput}->{pumpPairs}}) {
  $inputString .= $pumpPair->{prefix};
  $inputString .= $pumpPair->{pump} x int($query->{nPumps});
}
$inputString .= $query->{evilInput}->{suffix};

#print("<$inputString>\n");
&log("Query is valid. Gentlemen, start your timers!");

# Try to match string against pattern.
my $len = length($inputString);
&log("matching: pattern /$query->{pattern}/ inputStr: len $len");

my $NO_REDOS_EXCEPT = "NO_REDOS_EXCEPT";
my $RECURSION_EXCEPT = "RECURSION_LIMIT";

my $matched = 0;
my $matchContents = {
  "matchedString" => "",
  "captureGroups" => []
};
my $except = $NO_REDOS_EXCEPT;
eval {

  # Exception handler
  local $SIG{__WARN__} = sub {
    my $recursionSubStr = "Complex regular subexpression recursion limit";
    my $message = shift;
    
    # if we got a recursion limit warning
    if (index($message, $recursionSubStr) != -1) {
      $except = $RECURSION_EXCEPT;
    }
    else {
      &log("warning: $message");
    }
  };

  # Perform the match
  if ($inputString =~ /$query->{pattern}/) { # NB: with m//, ^ and $ are interpreted on a per-line basis
    $matched = 1;
    $matchContents->{matchedString} = $&; # I love perl

    if (defined $1) { # Were there any capture groups?
      my @matches = ($inputString =~ m/$query->{pattern}/);
      @matches = map { if (defined $_) { $_ } else { ""; } } @matches;
      $matchContents->{captureGroups} = \@matches;
    } else {
      $matchContents->{captureGroups} = [];
    }
  }
};

# this just catches all warnings -- can we specify by anything other than string text?
my $result = $query;
if ($@) {
  &log("Caught input exception: $@");
  &log("\$except: $except");
  if ($except eq $NO_REDOS_EXCEPT) {
    # An exception that wasn't ReDoS-related -- invalid pattern
    $result->{validPattern} = 0;
  } else {
    # ReDoS-related exception -- valid pattern
    $result->{validPattern} = 1;
  }
  $except = "INVALID_INPUT";
} else {
  # No exceptions -- valid pattern
  $result->{validPattern} = 1;
}

delete $result->{input}; # Might take a long time to print
$result->{inputLength} = $len;
$result->{matched} = $matched ? 1 : 0;
#$result->{matchContents} = $matchContents; # Don't need this in timing experiments
$result->{exceptionString} = $except;

print encode_json($result) . "\n";
exit 0;

##################

sub log {
  my ($msg) = @_;
  my $now = localtime;
  print STDERR "$now: $msg\n";
}

# input: %args: keys: file
# output: $contents
sub readFile {
  my %args = @_;

	open(my $FH, '<', $args{file}) or confess "Error, could not read $args{file}: $!";
	my $contents = do { local $/; <$FH> }; # localizing $? wipes the line separator char, so <> gets it all at once.
	close $FH;

  return $contents;
}
