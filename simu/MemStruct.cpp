// Contributed by Jose Renau, Edward Kuang
//
// The ESESC/BSD License
//
// Copyright (c) 2005-2013, Regents of the University of California and
// the ESESC Project.
// All rights reserved.
//
// Redistribution and use in source and binary forms, with or without
// modification, are permitted provided that the following conditions are met:
//
//   - Redistributions of source code must retain the above copyright notice,
//   this list of conditions and the following disclaimer.
//
//   - Redistributions in binary form must reproduce the above copyright
//   notice, this list of conditions and the following disclaimer in the
//   documentation and/or other materials provided with the distribution.
//
//   - Neither the name of the University of California, Santa Cruz nor the
//   names of its contributors may be used to endorse or promote products
//   derived from this software without specific prior written permission.
//
// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
// AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
// IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
// ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE
// LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
// CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
// SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
// INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
// CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
// ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
// POSSIBILITY OF SUCH DAMAGE.

#include "memstruct.hpp"

#include <stdio.h>
#include <string.h>
/* }}} */

CacheDebugAccess *CacheDebugAccess::getInstance() {
  static CacheDebugAccess *c = new CacheDebugAccess();
  return c;
}

void CacheDebugAccess::setCacheAccess(char *cacheLevel) {
  string s = string(cacheLevel);
  if (s.compare("DL10") == 0 || s.compare("niceCache0") == 0 || s.compare("L3") == 0 || s.compare("L20") == 0) {
    debugMap[s] = true;
  }
}

// Careful! Returns false for non-existing entry in map.
bool CacheDebugAccess::readCacheAccess(string cacheLevel) { return debugMap[cacheLevel]; }

void CacheDebugAccess::setAddrsAccessed(int a) { cacheAccesses = a; }

int CacheDebugAccess::readAddrsAccessed(void) { return cacheAccesses; }

int CacheDebugAccess::cacheAccessSum(void) {
  int                         sum = 0;
  map<string, bool>::iterator it;
  for (it = debugMap.begin(); it != debugMap.end(); it++) {
    if (it->second) {
      sum++;
    }
  }
  return sum;
}

void CacheDebugAccess::mapReset(void) { debugMap.clear(); }
