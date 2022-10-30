/*
   ESESC: Super ESCalar simulator
   Copyright (C) 2003 University of Illinois.

   Code based on Jose Martinez pool code (Thanks)

   Contributed by Jose Renau
                  Milos Prvulovic
                  James Tuck

This file is part of ESESC.

ESESC is free software; you can redistribute it and/or modify it under the terms
of the GNU General Public License as published by the Free Software Foundation;
either version 2, or (at your option) any later version.

ESESC is    distributed in the  hope that  it will  be  useful, but  WITHOUT ANY
WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A
PARTICULAR PURPOSE.  See the GNU General Public License for more details.

You should  have received a copy of  the GNU General  Public License along with
ESESC; see the file COPYING.  If not, write to the  Free Software Foundation, 59
Temple Place - Suite 330, Boston, MA 02111-1307, USA.
*/

#pragma once

#include <pthread.h>
#include <string.h>
#include <strings.h>

#include "fmt/format.h"
#include "iassert.hpp"

// Recycle memory allocated from time to time. This is useful for adapting to
// the phases of the application

#include "Snippets.h"

#ifndef NDEBUG
//#define POOL_TIMEOUT 1
#define POOL_SIZE_CHECK 1
//#define CLEAR_ON_INSERT 1
#endif

#ifdef POOL_TIMEOUT
#define POOL_CHECK_CYCLE 12000
#endif

template <class Ttype, class Parameter1, bool noTimeCheck = false> class pool1 {
protected:
  class Holder : public Ttype {
  public:
    Holder(Parameter1 p1)
        : Ttype(p1) {
    }
    Holder *holderNext;
#ifndef NDEBUG
    Holder *allNext; // List of Holders when active
    bool inPool;
#endif
#ifdef POOL_TIMEOUT
    Time_t outCycle; // Only valid if inPool is false
#endif
  };

#ifdef POOL_SIZE_CHECK
  unsigned long psize;
  unsigned long warn_psize;
#endif

#ifdef POOL_TIMEOUT
  Time_t need2cycle;
#endif

#ifndef NDEBUG
  Holder *  allFirst; // List of Holders when active
  pthread_t thid;
  bool deleted;
#endif

  const int32_t Size; // Reproduction size

  Parameter1 p1;

  Holder *first; // List of free nodes

  void reproduce() {
    I(first == 0);

    for(int32_t i = 0; i < Size; i++) {
      Holder *h     = ::new Holder(p1);
      h->holderNext = first;
#ifndef NDEBUG
      h->inPool = true;
      h->allNext = allFirst;
      allFirst   = h;
#endif
      first = h;
    }
  }

public:
  pool1(Parameter1 a1, int32_t s = 32)
      : Size(s)
      , p1(a1) {
    I(Size > 0);
#ifndef NDEBUG
    deleted = false;
#endif

#ifdef POOL_SIZE_CHECK
    psize      = 0;
    warn_psize = s * 8;
#endif

    first = 0;

#ifdef POOL_TIMEOUT
    need2cycle = globalClock + POOL_CHECK_CYCLE;
#endif
#ifndef NDEBUG
    allFirst = 0;
    thid     = 0;
#endif

    if(first == 0)
      reproduce();
  }

  ~pool1() {
    // The last pool whould delete all the crap
    while(first) {
      Holder *h = first;
      first     = first->holderNext;
      ::delete h;
    }
    first = 0;
#ifndef NDEBUG
    deleted = true;
#endif
  }

  void doChecks() {
#ifdef POOL_TIMEOUT
    if(noTimeCheck)
      return;
    if(need2cycle < globalClock) {
      Holder *tmp = allFirst;
      while(tmp) {
        GI(!tmp->inPool, (tmp->outCycle + POOL_CHECK_CYCLE) > need2cycle);
        tmp = tmp->allNext;
      }
      need2cycle = globalClock + POOL_CHECK_CYCLE;
    }
#endif // POOL_TIMEOUT
  }

  void in(Ttype *data) {
#ifndef NDEBUG
    if(thid == 0)
      thid = pthread_self();
    I(thid == pthread_self());
    I(!deleted);
#endif
    Holder *h = static_cast<Holder *>(data);

    I(!h->inPool);
#ifndef NDEBUG
    h->inPool = true;
#endif

    h->holderNext = first;
    first         = h;

#ifdef POOL_SIZE_CHECK
    psize--;
#endif

    doChecks();
  }

  Ttype *out() {
#ifndef NDEBUG
    if(thid == 0)
      thid = pthread_self();
    I(thid == pthread_self());
    I(!deleted);
#endif
    I(first);

    I(first->inPool);
#ifndef NDEBUG
    first->inPool = false;
#endif
#ifdef POOL_TIMEOUT
    first->outCycle = globalClock;
    doChecks();
#endif

#ifdef POOL_SIZE_CHECK
    psize++;
    if(psize >= warn_psize) {
      fmt::print("Pool1 class size grew to {}\n", psize);
      warn_psize = 4 * psize;
    }
#endif

    Ttype *h = static_cast<Ttype *>(first);
    first    = first->holderNext;
    if(first == 0)
      reproduce();

    return h;
  }
};

//*********************************************

template <class Ttype> class tspool {
protected:
  class Holder : public Ttype {
  public:
    volatile Holder *holderNext;
#ifndef NDEBUG
    bool inPool;
#endif
  };

  volatile Holder *first; // List of free nodes

  const int32_t Size; // Reproduction size
  const char *  Name;
#ifndef NDEBUG
  bool deleted;
#endif

  void insert(Holder *h) {
    // Equivalent to
    //  h->holderNext = first;
    //  first         = h;

#ifndef NDEBUG
    h->inPool = true;
#endif
    volatile Holder *old_first;
    volatile Holder *c_first;
    do {
      c_first       = first;
      h->holderNext = c_first;
      old_first     = AtomicCompareSwap(&first, c_first, h);
    } while(old_first != c_first);
  };

  void reproduce() {
    I(first == 0);

    for(int32_t i = 0; i < Size; i++) {
      Holder *h = ::new Holder;
      insert(h);
    }
  };

public:
  tspool(int32_t s = 32, const char *n = "pool name not declared")
      : Size(s)
      , Name(n) {
    I(Size > 0);
#ifndef NDEBUG
    deleted = false;
#endif

    first = 0;

    reproduce();
  };

  ~tspool() {
    // The last pool should delete all the crap
    while(first) {
      volatile Holder *h = first;
      first              = first->holderNext;
      ::delete h;
    }
    first = 0;
#ifndef NDEBUG
    deleted = true;
#endif
  }

  void in(Ttype *data) {
#ifndef NDEBUG
    I(!deleted);
#endif
    Holder *h = static_cast<Holder *>(data);

    I(!h->inPool);

    insert(h);
  }

  Ttype *out() {
#ifndef NDEBUG
    I(!deleted);
    I(first);
#endif

    volatile Holder *old_first;
    volatile Holder *c_first;
    do {
      c_first   = first;
      old_first = AtomicCompareSwap(&first, c_first, c_first->holderNext);
    } while(old_first != c_first);

    if(first == 0)
      reproduce();

    Ttype *h = (Ttype *)(c_first);
    I(c_first->inPool);
#ifndef NDEBUG
    c_first->inPool = false;
#endif
    return h;
  }
};

template <class Ttype, bool noTimeCheck = false> class pool {
protected:
  class Holder : public Ttype {
  public:
    Holder *holderNext;
#ifndef NDEBUG
    Holder *  allNext; // List of Holders when active
    pthread_t thid;
    bool inPool;
#endif
#ifdef POOL_TIMEOUT
    Time_t outCycle; // Only valid if inPool is false
#endif
  };

#ifdef POOL_SIZE_CHECK
  unsigned long psize;
  unsigned long warn_psize;
#endif


#ifdef POOL_TIMEOUT
  Time_t need2cycle;
#endif
#ifndef NDEBUG
  Holder *  allFirst; // List of Holders when active
  pthread_t thid;
  bool deleted;
#endif

  const int32_t Size; // Reproduction size
  const char *  Name;

  Holder *first; // List of free nodes

  void reproduce() {
    I(first == 0);

    for(int32_t i = 0; i < Size; i++) {
      Holder *h = ::new Holder;
#ifdef CLEAR_ON_INSERT
      bzero(h, sizeof(Holder));
#endif
      h->holderNext = first;
#ifndef NDEBUG
      h->inPool = true;
      h->allNext = allFirst;
      allFirst   = h;
#endif
      first = h;
    }
  }

public:
  pool(int32_t s = 32, const char *n = "pool name not declared")
      : Size(s)
      , Name(n) {
    I(Size > 0);
#ifndef NDEBUG
    deleted = false;
#endif

#ifdef POOL_SIZE_CHECK
    psize      = 0;
    warn_psize = s * 8;
#endif

    first = 0;

#ifdef POOL_TIMEOUT
    need2cycle = globalClock + POOL_CHECK_CYCLE;
#endif
#ifndef NDEBUG
    allFirst = 0;
    thid     = 0;
#endif

    if(first == 0)
      reproduce();
  }

  ~pool() {
    // The last pool whould delete all the crap
#if 0
    while(first) {
      Holder *h = first;
      first = first->holderNext;
      ::delete h;
    }
    first = 0;
#ifndef NDEBUG
    deleted=true;
#endif
#endif
  }

  void doChecks() {
#ifdef POOL_TIMEOUT
    if(noTimeCheck)
      return;
    if(need2cycle < globalClock) {
      Holder *tmp = allFirst;
      while(tmp) {
        GI(!tmp->inPool, (tmp->outCycle + POOL_CHECK_CYCLE) > need2cycle);
        tmp = tmp->allNext;
      }
      need2cycle = globalClock + POOL_CHECK_CYCLE;
    }
#endif // POOL_TIMEOUT
  }

#ifndef NDEBUG
  Ttype *nextInUse(Ttype *current) {
    Holder *tmp = static_cast<Holder *>(current);
    tmp         = tmp->allNext;
    while(tmp) {
      if(!tmp->inPool)
        return static_cast<Ttype *>(tmp);
      tmp = tmp->allNext;
    }
    return 0;
  }
  Ttype *firstInUse() {
    return nextInUse(static_cast<Ttype *>(allFirst));
  }
#endif

  void in(Ttype *data) {
#ifndef NDEBUG
    if(thid == 0)
      thid = pthread_self();
    I(thid == pthread_self());
    I(!deleted);
#endif
    Holder *h = static_cast<Holder *>(data);

#ifdef CLEAR_ON_INSERT
    bzero(data, sizeof(Ttype));
#endif

#ifndef NDEBUG
    I(!h->inPool);
    h->inPool = true;
#endif

    h->holderNext = first;
    first         = h;

#ifdef POOL_SIZE_CHECK
    psize--;
#endif

    doChecks();
  }

  Ttype *out() {
#ifndef NDEBUG
    if(thid == 0)
      thid = pthread_self();
    I(thid == pthread_self());
    I(!deleted);
    I(first);

    I(first->inPool);
    first->inPool = false;

#endif
#ifdef POOL_TIMEOUT
    first->outCycle = globalClock;
    doChecks();
#endif

#ifdef POOL_SIZE_CHECK
    psize++;
    if(psize >= warn_psize) {
      fmt::print("{}:pool class size grew to {}\n", Name, psize);
      warn_psize = 4 * psize;
    }
#endif

    Ttype *h = static_cast<Ttype *>(first);
    first    = first->holderNext;
    if(first == 0)
      reproduce();

#if defined(CLEAR_ON_INSERT) && defined(DEBUG)
    const char *ptr = (const char *)(h);
    for(size_t i = 0; i < sizeof(Ttype); i++) {
      I(ptr[i] == 0);
    }
#endif

    return h;
  }
};

//*********************************************

template <class Ttype, bool timeCheck = false> class poolplus {
protected:
  class Holder : public Ttype {
  public:
    Holder *holderNext;
#ifndef NDEBUG
    Holder *allNext; // List of Holders when active
    bool inPool;
#endif
#ifdef POOL_TIMEOUT
    Time_t outCycle; // Only valid if inPool is false
#endif
  };

#ifdef POOL_SIZE_CHECK
  unsigned long psize;
  unsigned long warn_psize;
#endif

#ifdef POOL_TIMEOUT
  Time_t need2cycle;
#endif

#ifndef NDEBUG
  Holder *allFirst; // List of Holders when active
  bool deleted;
#endif

  const int32_t Size; // Reproduction size
  const char *  Name;

  Holder *first; // List of free nodes

  void reproduce() {
    I(first == 0);

    for(int32_t i = 0; i < Size; i++) {
      Holder *h     = ::new Holder;
      h->holderNext = first;
#ifndef NDEBUG
      h->inPool = true;
      h->allNext = allFirst;
      allFirst   = h;
#endif
      first = h;
    }
  }

public:
  poolplus(int32_t s = 32, const char *n = "poolplus name not declared")
      : Size(s)
      , Name(n) {
    I(Size > 0);
#ifndef NDEBUG
    deleted = false;
#endif

#ifdef POOL_SIZE_CHECK
    psize      = 0;
    warn_psize = s * 8;
#endif

    first = 0;

#ifdef POOL_TIMEOUT
    need2cycle = globalClock + POOL_CHECK_CYCLE;
#endif
#ifndef NDEBUG
    allFirst = 0;
#endif

    if(first == 0)
      reproduce();
  }

  ~poolplus() {
    // The last pool whould delete all the crap
    while(first) {
      Holder *h = first;
      first     = first->holderNext;
      ::delete h;
    }
    first = 0;
#ifndef NDEBUG
    deleted = true;
#endif
  }

  void doChecks() {
#ifdef POOL_TIMEOUT
    if(!timeCheck)
      return;
    if(need2cycle < globalClock) {
      Holder *tmp = allFirst;
      while(tmp) {
        GI(!tmp->inPool, (tmp->outCycle + POOL_CHECK_CYCLE) > need2cycle);
        tmp = tmp->allNext;
      }
      need2cycle = globalClock + POOL_CHECK_CYCLE;
    }
#endif // POOL_TIMEOUT
  }

  void in(Ttype *data) {
    Holder *h = static_cast<Holder *>(data);

#ifndef NDEBUG
    I(!deleted);
    I(!h->inPool);
    h->inPool = true;
#endif

    h->holderNext = first;
    first         = h;

#ifdef POOL_SIZE_CHECK
    psize--;
#endif

    doChecks();
  }

  Ttype *out() {
#ifndef NDEBUG
    I(!deleted);
    I(first);

    I(first->inPool);
    first->inPool = false;
#endif
#ifdef POOL_TIMEOUT
    first->outCycle = globalClock;
    doChecks();
#endif

#ifdef POOL_SIZE_CHECK
    psize++;
    if(psize >= warn_psize) {
      fmt::print("{}:pool class size grew to {}\n", Name, psize);
      warn_psize = 4 * psize;
    }
#endif

    Ttype *h = static_cast<Ttype *>(first);
    first    = first->holderNext;
    if(first == 0)
      reproduce();

    h->Ttype::prepare();

    return h;
  }
};

//*********************************************

