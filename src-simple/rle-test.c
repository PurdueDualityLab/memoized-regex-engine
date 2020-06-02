/*
Copyright (c) 2020, James Davis http://people.cs.vt.edu/davisjam/
All rights reserved.

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions are met:

    * Redistributions of source code must retain the above copyright
      notice, this list of conditions and the following disclaimer.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS
IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED
TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A
PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER
OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
*/

#include "rle.h"
#include "log.h"
#include "vendor/avl_tree.h"

#include <assert.h>
#include <stdlib.h>
#include <stdio.h>

#include "rle.h"

void testSetGet() {
  logMsg(LOG_INFO, "Test begins: testSetGet");

  RLEVector *vec = RLEVector_create(1, 1);

  logMsg(LOG_INFO, "get from empty");
  assert(RLEVector_get(vec, 5) == 0);
  assert(RLEVector_get(vec, 1) == 0);

  logMsg(LOG_INFO, "  set 5");
  RLEVector_set(vec, 5);

  logMsg(LOG_INFO, "  set 7");
  RLEVector_set(vec, 7);
  logMsg(LOG_INFO, "  set 4");
  RLEVector_set(vec, 4);
  logMsg(LOG_INFO, "  set 6");
  RLEVector_set(vec, 6);

  logMsg(LOG_INFO, "  get 4-8");
  assert(RLEVector_get(vec, 4) == 1);
  assert(RLEVector_get(vec, 5) == 1);
  assert(RLEVector_get(vec, 6) == 1);
  assert(RLEVector_get(vec, 7) == 1);
  assert(RLEVector_get(vec, 8) == 0);
  logMsg(LOG_INFO, "...test passed");
}

void testRuns() {
  logMsg(LOG_INFO, "Test begins: testRuns");
  int i, j;
  RLEVector *vec;

  /* Runs of length 1 are compressible */
  logMsg(LOG_INFO, "  1-length runs work");
  vec = RLEVector_create(1, 1);
  for (i = 0; i < 100; i++) {
    RLEVector_set(vec, i);
    assert(RLEVector_currSize(vec) == 1);
  }

  /* Runs of length 1 can't compress 10101010... */
  logMsg(LOG_INFO, "  1-length runs work but fail to compress");
  vec = RLEVector_create(1, 1);
  j = 0;
  for (i = 0; i < 100; i += 2) {
    j++;
    RLEVector_set(vec, i);
    assert(RLEVector_currSize(vec) == j);
    assert(RLEVector_currSize(vec) == RLEVector_maxObservedSize(vec));
  }
  /* But merging works: 10101... -> 11101... -> 1111... */
  logMsg(LOG_INFO, "  ...merging works");
  for (i = 1; i < 100; i += 2) {
    j--;
    if (j == 0) j++; // Cannot go to 0
    RLEVector_set(vec, i);
    assert(RLEVector_currSize(vec) == j);
    assert(RLEVector_currSize(vec) < RLEVector_maxObservedSize(vec));
  }
  assert(RLEVector_currSize(vec) == 1);

  /* Runs of length 3 can compress 011011... */
  logMsg(LOG_INFO, "  runs of length 3 work");
  vec = RLEVector_create(3, 1);
  j = 0;
  for (i = 0; i < 100; i += 3) {
    j++;
    RLEVector_set(vec, i);
    RLEVector_set(vec, i+1);
    assert(RLEVector_currSize(vec) == 1);
  }

  logMsg(LOG_INFO, "...test passed");
}

int main(int argc, char** argv) {
  logMsg(LOG_INFO, "Running the RLE unit test suite...");

  testSetGet();
  testRuns();

  return 0;
}