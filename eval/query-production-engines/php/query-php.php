#!/usr/bin/env php
<?php
// Author: Jamie Davis <davisjam@vt.edu>
// Description: Try REDOS attack on PHP

// Taken from
//   http://php.net/manual/en/function.preg-last-error.php#112449
// The array_flip version throws an error on PHP 7.4.0-dev (sushi)
//   http://php.net/manual/en/function.preg-last-error.php#114105
function preg_errtxt($errcode)
{
  static $errtext;

  if (!isset($errtxt))
  {
    $errtext = array();
    $constants = get_defined_constants(true);
    foreach ($constants['pcre'] as $c => $n) if (preg_match('/_ERROR$/', $c)) $errtext[$n] = $c;
  }

  return array_key_exists($errcode, $errtext)? $errtext[$errcode] : NULL;
}

function my_log($msg) {
  fwrite(STDERR, $msg . "\n");
}

function main() {
  // Assume args are correct, this is a horrible language.
  global $argc, $argv;
  $FH = fopen($argv[1], "r") or die("Unable to open file!");
  $cont = fread($FH, filesize($argv[1]));
  fclose($FH);

  $obj = json_decode($cont);
  my_log('obj');

  // Compose evil input.
  $queryString = '';
  foreach ($obj->{'evilInput'}->{'pumpPairs'} as $pumpPair) {
    $queryString .= $pumpPair->{'prefix'};
    for ($i = 0; $i < $obj->{'nPumps'}; $i++) {
      $queryString .= $pumpPair->{'pump'};
    }
  }
  $queryString .= $obj->{'evilInput'}->{'suffix'};

  // Query regexp.
  my_log('matching: Pattern /' . $obj->{'pattern'} . '/, nPumps ' . $obj->{'nPumps'} . ', queryString ' . $queryString); 
  //$matched = preg_match('/' . $obj->{'pattern'} . '/', 'aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa!');
  
  $matched = @preg_match('/' . $obj->{'pattern'} . '/', $queryString);

  // check for errors
  $except = "";

  $compilation_failed_message = 'preg_match(): Compilation failed:';
  $last_error = error_get_last();
  // Compilation
  if(strpos($last_error['message'], $compilation_failed_message) !== false) {
    my_log("caught the invalid input");
    $except = "INVALID_INPUT";
  }
  // preg internal errors
  elseif (preg_last_error() != PREG_NO_ERROR) {
    $except = preg_errtxt(preg_last_error());
  }

  // Compose output.
  $obj->{'matched'} = $matched;
  $obj->{'inputLength'} = strlen($queryString);
  $obj->{'exceptionString'} = $except;
  fwrite(STDOUT, json_encode($obj) . "\n");

  // Whew.
  exit(0);
}

main();
?>
