// This file is distributed under the BSD 3-Clause License. See LICENSE for details.

#pragma once

#define STRICT_PRIORITY      1
#define PORT_STRICT_PRIORITY 1

#include <cstdlib>
#include <vector>  // std::vector<>

#include "fmt/format.h"
#include "iassert.hpp"
#include "pool.hpp"
#include "snippets.hpp"
#include "tqueue.hpp"

#ifdef PORT_STRICT_PRIORITY
// Forward declaration for port registration
class PortGeneric;
#endif

/////////////////////////////////////////////////////////////////////////////
//
// DESCRIPTION:
//
// This is an abstract class that defines the interface for the Callback
// framework for C++. It is templated on the message "Parameter" to be
// used by the parties involved.
//
// Debug callbacks is a problem because once they have been scheduled
// it is not possible to know WHERE it was scheduled. The file an
// lineno that scheduled the callback can be known in debug mode by
// calling dump
//
/////////////////////////////////////////////////////////////////////////////

class EventScheduler : public TQueue<EventScheduler*, Time_t>::User {
private:
  using TimedCallbacksQueue = TQueue<EventScheduler*, Time_t>;

  static TimedCallbacksQueue cbQ;

#ifndef NDEBUG
  bool priority_set = false;
#endif
  Time_t priority = 0;

#ifndef NDEBUG
  const char* fileName;
  int32_t     lineno;
#endif
protected:
  void initPriority(Time_t p) {
#ifndef NDEBUG
    I(!priority_set);
    priority_set = true;
#endif
    priority = p;
  }

  void resetPriority() {
#ifndef NDEBUG
    priority_set = false;
#endif
    priority = 0;
  }

public:
  EventScheduler() = default;

  [[nodiscard]] Time_t getPriority() const { return priority; }

  virtual void call() = 0;
  virtual ~EventScheduler() {
    // Nothing
  }

  void dump() const;

#ifdef PORT_STRICT_PRIORITY
  // Port registration for priority-based allocation
  static void registerPort(PortGeneric* port);
  static void processPendingPortRequests();
#endif

  static void schedule(Time_t tim, EventScheduler* cb) {
    (void)tim;
    (void)cb;
    fmt::print("BOG ALERT! BUGABUGABUG.\nPerhaps you meant to use scheduleAbsi\n");
    exit(1);
  }
  static void schedule(TimeDelta_t delta, EventScheduler* cb) {
    I(delta);  // Only for performance reasons
#ifndef NDEBUG
    cb->fileName = __FILE__;
    cb->lineno   = __LINE__;
#endif
    cbQ.insert(cb, globalClock + delta);
  }

  static void scheduleAbs(TimeDelta_t tim, EventScheduler* cb) {
    (void)tim;
    (void)cb;
    fmt::print("BOG ALERT! BUGABUGABUG.\nPerhaps you meant to use schedule\n");
    exit(1);
  }
  static void scheduleAbs(Time_t tim, EventScheduler* cb) {
    I(tim > globalClock);  // Only for performance reasons
#ifndef NDEBUG
    static bool once = true;
    GI(globalClock > 10000000 && !once, tim < (2 * globalClock));  // may be fine, but be suspicious if it repeats!!
    once         = false;
    cb->fileName = __FILE__;
    cb->lineno   = __LINE__;
#endif
    cbQ.insert(cb, tim);
  }

  static void advanceClock() {
    EventScheduler* cb;

#ifndef NDEBUG
    while ((cb = cbQ.nextJob(globalClock))) {
      I(0);  // There should be no job in current cycle (executed before, and not possible to schedule events with 0 delay
      cb->call();
    }
#endif
    globalClock++;

#ifdef PORT_STRICT_PRIORITY
    // Process pending port requests BEFORE regular callbacks
    // This allows priority-ordered allocation at cycle start
    processPendingPortRequests();
#endif

    uint32_t cb_per_clock = 0;
    while ((cb = cbQ.nextJob(globalClock))) {
      cb->call();
      cb_per_clock++;
    }

    if (cb_per_clock == 0) {
      deadClock++;
    }
  }

  static bool empty() { return cbQ.empty(); }

  static size_t size() { return cbQ.size(); }

  static void reset() {
    I(empty());
    cbQ.reset();
    globalClock = 0;
#ifdef PORT_STRICT_PRIORITY
    // Clear port registrations on reset
    getRegisteredPorts().clear();
#endif
  }

#ifdef PORT_STRICT_PRIORITY
private:
  // Static storage for registered ports (accessor pattern for static initialization order)
  static std::vector<PortGeneric*>& getRegisteredPorts() {
    static std::vector<PortGeneric*> registered_ports;
    return registered_ports;
  }
#endif
};

class CallbackBase : public EventScheduler {
private:
protected:
  CallbackBase* nextCB4Container;
  CallbackBase* getNextCallbackBase() const { return nextCB4Container; }
  void          setNextCallbackBase(CallbackBase* cb) { nextCB4Container = cb; }
  friend class CallbackContainer;
#ifndef NDEBUG
  CallbackBase() { nextCB4Container = nullptr; }
  virtual ~CallbackBase() {
    I(nextCB4Container == nullptr);  // Destroying a callback still enqueed?
  }
#endif
public:
  virtual void destroy() {}
  void         schedule(TimeDelta_t delta) { EventScheduler::schedule(delta, this); }
  void         scheduleAbs(Time_t tim) { EventScheduler::scheduleAbs(tim, this); }
};

class StaticCallbackBase : public CallbackBase {
  // Gives xtra freedom so that it can be forced to require an
  // staticCallbackBase instead of a CallbackBase
};

/////////////////////////////////////////////////////////////////////////////
//
// DESCRIPTION:
//
// This is a concrete class that implements the interface for the Callback
// framework for C++ for Global functions. This class should be used with
// global functions or static methods of a particular class.
//
/////////////////////////////////////////////////////////////////////////////

template <class Parameter1, class Parameter2, class Parameter3, void (*funcPtr)(Parameter1, Parameter2, Parameter3)>
class CallbackFunction3 : public CallbackBase {
private:
  using poolType = pool<CallbackFunction3>;

  static poolType cbPool;
  friend class pool<CallbackFunction3>;

  Parameter1 p1;
  Parameter2 p2;
  Parameter3 p3;

protected:
  CallbackFunction3() {}
  virtual ~CallbackFunction3() {}

public:
  static CallbackFunction3* create(Parameter1 a1, Parameter2 a2, Parameter3 a3, Time_t priority = 0) {
    CallbackFunction3* cb = cbPool.out();
    cb->resetPriority();
    cb->initPriority(priority);
    cb->p1 = a1;
    cb->p2 = a2;
    cb->p3 = a3;

    return cb;
  }

  static void schedule(TimeDelta_t delta, Parameter1 a1, Parameter2 a2, Parameter3 a3, Time_t priority = 0) {
    if (delta == 0) {
      (*funcPtr)(a1, a2, a3);
    } else {
      CallbackFunction3* cb = create(a1, a2, a3, priority);
      EventScheduler::schedule(delta, cb);
    }
  }

  static void scheduleAbs(Time_t tim, Parameter1 a1, Parameter2 a2, Parameter3 a3, Time_t priority = 0) {
    I(tim >= globalClock);
    if (tim == globalClock) {
      (*funcPtr)(a1, a2, a3);
    } else {
      CallbackFunction3* cb = create(a1, a2, a3, priority);
      EventScheduler::scheduleAbs(tim, cb);
    }
  }

  void call() {
    (*funcPtr)(p1, p2, p3);
    destroy();
  }

  void destroy() { cbPool.in(this); }

  void setParam1(Parameter1 a1) { p1 = a1; }
};

template <class Parameter1, class Parameter2, class Parameter3, void (*funcPtr)(Parameter1, Parameter2, Parameter3)>
typename CallbackFunction3<Parameter1, Parameter2, Parameter3, funcPtr>::poolType
    CallbackFunction3<Parameter1, Parameter2, Parameter3, funcPtr>::cbPool(32, "CBF3");

template <class Parameter1, class Parameter2, void (*funcPtr)(Parameter1, Parameter2)>
class CallbackFunction2 : public CallbackBase {
private:
  using poolType = pool<CallbackFunction2>;

  static poolType cbPool;
  friend class pool<CallbackFunction2>;

  Parameter1 p1;
  Parameter2 p2;

protected:
  CallbackFunction2() {}
  virtual ~CallbackFunction2() {}

public:
  static CallbackFunction2* create(Parameter1 a1, Parameter2 a2, Time_t priority = 0) {
    CallbackFunction2* cb = cbPool.out();
    cb->resetPriority();
    cb->initPriority(priority);
    cb->p1 = a1;
    cb->p2 = a2;

    return cb;
  }

  static void schedule(TimeDelta_t delta, Parameter1 a1, Parameter2 a2, Time_t priority = 0) {
    if (delta == 0) {
      (*funcPtr)(a1, a2);
    } else {
      CallbackFunction2* cb = create(a1, a2, priority);
      EventScheduler::schedule(delta, cb);
    }
  }

  static void scheduleAbs(Time_t tim, Parameter1 a1, Parameter2 a2, Time_t priority = 0) {
    if (tim == globalClock) {
      (*funcPtr)(a1, a2);
    } else {
      CallbackFunction2* cb = create(a1, a2, priority);
      EventScheduler::scheduleAbs(tim, cb);
    }
  }

  void call() {
    (*funcPtr)(p1, p2);
    destroy();
  }

  void destroy() { cbPool.in(this); }

  void setParam1(Parameter1 a1) { p1 = a1; }
};

template <class Parameter1, class Parameter2, void (*funcPtr)(Parameter1, Parameter2)>
typename CallbackFunction2<Parameter1, Parameter2, funcPtr>::poolType CallbackFunction2<Parameter1, Parameter2, funcPtr>::cbPool(
    32, "CBF2");

template <class Parameter1, void (*funcPtr)(Parameter1)>
class CallbackFunction1 : public CallbackBase {
private:
  using poolType = pool<CallbackFunction1>;

  static poolType cbPool;
  friend class pool<CallbackFunction1>;

  Parameter1 p1;

protected:
  CallbackFunction1() {}
  virtual ~CallbackFunction1() {}

public:
  static CallbackFunction1* create(Parameter1 a1, Time_t priority = 0) {
    CallbackFunction1* cb = cbPool.out();
    cb->resetPriority();
    cb->initPriority(priority);
    cb->p1 = a1;
    return cb;
  }

  static void schedule(TimeDelta_t delta, Parameter1 a1, Time_t priority = 0) {
    if (delta == 0) {
      (*funcPtr)(a1);
    } else {
      CallbackFunction1* cb = create(a1, priority);
      EventScheduler::schedule(delta, cb);
    }
  }

  static void scheduleAbs(Time_t tim, Parameter1 a1, Time_t priority = 0) {
    if (tim == globalClock) {
      (*funcPtr)(a1);
    } else {
      CallbackFunction1* cb = create(a1, priority);
      EventScheduler::scheduleAbs(tim, cb);
    }
  }

  void call() {
    (*funcPtr)(p1);
    destroy();
  }

  void destroy() { cbPool.in(this); }

  void setParam1(Parameter1 a1) { p1 = a1; }
};

template <class Parameter1, void (*funcPtr)(Parameter1)>
typename CallbackFunction1<Parameter1, funcPtr>::poolType CallbackFunction1<Parameter1, funcPtr>::cbPool(32, "CBF1");

template <void (*funcPtr)()>
class CallbackFunction0 : public CallbackBase {
private:
  using poolType = pool<CallbackFunction0>;

  static poolType cbPool;
  friend class pool<CallbackFunction0>;

protected:
  CallbackFunction0() {}
  virtual ~CallbackFunction0() {}

public:
  static CallbackFunction0* create(Time_t priority = 0) {
    CallbackFunction0* cb = cbPool.out();
    cb->resetPriority();
    cb->initPriority(priority);
    return cb;
  }

  static void schedule(TimeDelta_t delta, Time_t priority = 0) {
    if (delta == 0) {
      (*funcPtr)();
    } else {
      CallbackFunction0* cb = create(priority);
      EventScheduler::schedule(delta, cb);
    }
  }

  static void scheduleAbs(Time_t tim, Time_t priority = 0) {
    if (tim == globalClock) {
      (*funcPtr)();
    } else {
      CallbackFunction0* cb = create(priority);
      EventScheduler::scheduleAbs(tim, cb);
    }
  }

  void call() {
    (*funcPtr)();
    destroy();
  }

  void destroy() { cbPool.in(this); }
};

template <void (*funcPtr)()>
typename CallbackFunction0<funcPtr>::poolType CallbackFunction0<funcPtr>::cbPool(32, "CBF1");

template <class Parameter1, class Parameter2, void (*funcPtr)(Parameter1, Parameter2)>
class StaticCallbackFunction2 : public StaticCallbackBase {
private:
#ifndef NDEBUG
  bool isFree;
#endif
  Parameter1 p1;
  Parameter2 p2;

protected:
public:
  StaticCallbackFunction2() {
#ifndef NDEBUG
    isFree = true;
    ;
#endif
  }
  virtual ~StaticCallbackFunction2() {}

  void schedule(TimeDelta_t delta, Parameter1 a1, Parameter2 a2) {
#ifndef NDEBUG
    I(isFree);
#endif
    if (delta == 0) {
      (*funcPtr)(a1, a2);
    } else {
      p1 = a1;
      p2 = a2;
#ifndef NDEBUG
      isFree = false;
#endif
      EventScheduler::schedule(delta, this);
    }
  }

  void scheduleAbs(Time_t tim, Parameter1 a1, Parameter2 a2) {
#ifndef NDEBUG
    I(isFree);
#endif
    if (tim == globalClock) {
      (*funcPtr)(a1, a2);
    } else {
      p1 = a1;
      p2 = a2;
#ifndef NDEBUG
      isFree = false;
#endif
      EventScheduler::scheduleAbs(tim, this);
    }
  }

  void call() {
#ifndef NDEBUG
    isFree = true;
#endif
    (*funcPtr)(p1, p2);
  }

  void setParam1(Parameter1 a1) { p1 = a1; }

  void setParam2(Parameter2 a2) { p2 = a2; }
};

template <void (*funcPtr)(void)>
class StaticCallbackFunction0 : public StaticCallbackBase {
private:
protected:
public:
  StaticCallbackFunction0() {}
  virtual ~StaticCallbackFunction0() {}

  void schedule(TimeDelta_t delta) {
    if (delta == 0) {
      (*funcPtr)();
    } else {
      EventScheduler::schedule(delta, this);
    }
  }

  void scheduleAbs(Time_t tim) {
    if (tim == globalClock) {
      (*funcPtr)();
    } else {
      EventScheduler::scheduleAbs(tim, this);
    }
  }

  void call() { (*funcPtr)(); }
};

/////////////////////////////////////////////////////////////////////////////
//
// DESCRIPTION:
//
// This is a concrete class that implements the interface for the Callback
// framework for C++ for member functions of a class. This class should be
// used with methods of a class.
//
///////////////////////////////////////////////////////////////////////////

/************************************************************************************/
template <class ClassType, class Parameter1, class Parameter2, class Parameter3, class Parameter4, class Parameter5,
          class Parameter6, void (ClassType::*memberPtr)(Parameter1, Parameter2, Parameter3, Parameter4, Parameter5, Parameter6)>
class CallbackMember6 : public CallbackBase {
private:
  using poolType = pool<CallbackMember6>;

  static poolType cbPool;
  friend class pool<CallbackMember6>;

  Parameter1 p1;
  Parameter2 p2;
  Parameter3 p3;
  Parameter4 p4;
  Parameter5 p5;
  Parameter6 p6;

  ClassType* instance;

protected:
  CallbackMember6() {}

  virtual ~CallbackMember6() {}

public:
  static CallbackMember6* create(ClassType* i, Parameter1 a1, Parameter2 a2, Parameter3 a3, Parameter4 a4, Parameter5 a5,
                                 Parameter6 a6, Time_t priority = 0) {
    CallbackMember6* cb = cbPool.out();
    cb->resetPriority();
    cb->initPriority(priority);
    cb->instance = i;
    cb->p1       = a1;
    cb->p2       = a2;
    cb->p3       = a3;
    cb->p4       = a4;
    cb->p5       = a5;
    cb->p6       = a6;

    return cb;
  }

  static void schedule(TimeDelta_t delta, ClassType* i, Parameter1 a1, Parameter2 a2, Parameter3 a3, Parameter4 a4, Parameter5 a5,
                       Parameter6 a6, Time_t priority = 0) {
    if (delta == 0) {
      (i->*memberPtr)(a1, a2, a3, a4, a5, a6);
    } else {
      CallbackMember6* cb = create(i, a1, a2, a3, a4, a5, a6, priority);
      EventScheduler::schedule(delta, cb);
    }
  }

  static void scheduleAbs(Time_t tim, ClassType* i, Parameter1 a1, Parameter2 a2, Parameter3 a3, Parameter4 a4, Parameter5 a5,
                          Parameter6 a6, Time_t priority = 0) {
    if (tim == globalClock) {
      (i->*memberPtr)(a1, a2, a3, a4, a5, a6);
    } else {
      CallbackMember6* cb = create(i, a1, a2, a3, a4, a5, a6, priority);
      EventScheduler::scheduleAbs(tim, cb);
    }
  }

  void call() {
    (instance->*memberPtr)(p1, p2, p3, p4, p5, p6);
    destroy();
  }

  void destroy() { cbPool.in(this); }

  void setParam1(Parameter1 a1) { p1 = a1; }
};

template <class ClassType, class Parameter1, class Parameter2, class Parameter3, class Parameter4, class Parameter5,
          class Parameter6, void (ClassType::*memberPtr)(Parameter1, Parameter2, Parameter3, Parameter4, Parameter5, Parameter6)>
typename CallbackMember6<ClassType, Parameter1, Parameter2, Parameter3, Parameter4, Parameter5, Parameter6, memberPtr>::poolType
    CallbackMember6<ClassType, Parameter1, Parameter2, Parameter3, Parameter4, Parameter5, Parameter6, memberPtr>::cbPool(32,
                                                                                                                          "CBM6");

/************************************************************************************/

/************************************************************************************/
template <class ClassType, class Parameter1, class Parameter2, class Parameter3, class Parameter4, class Parameter5,
          void (ClassType::*memberPtr)(Parameter1, Parameter2, Parameter3, Parameter4, Parameter5)>
class CallbackMember5 : public CallbackBase {
private:
  using poolType = pool<CallbackMember5>;

  static poolType cbPool;
  friend class pool<CallbackMember5>;

  Parameter1 p1;
  Parameter2 p2;
  Parameter3 p3;
  Parameter4 p4;
  Parameter5 p5;

  ClassType* instance;

protected:
  CallbackMember5() {}

  virtual ~CallbackMember5() {}

public:
  static CallbackMember5* create(ClassType* i, Parameter1 a1, Parameter2 a2, Parameter3 a3, Parameter4 a4, Parameter5 a5,
                                 Time_t priority = 0) {
    CallbackMember5* cb = cbPool.out();
    cb->resetPriority();
    cb->initPriority(priority);
    cb->instance = i;
    cb->p1       = a1;
    cb->p2       = a2;
    cb->p3       = a3;
    cb->p4       = a4;
    cb->p5       = a5;

    return cb;
  }

  static void schedule(TimeDelta_t delta, ClassType* i, Parameter1 a1, Parameter2 a2, Parameter3 a3, Parameter4 a4, Parameter5 a5,
                       Time_t priority = 0) {
    if (delta == 0) {
      (i->*memberPtr)(a1, a2, a3, a4, a5);
    } else {
      CallbackMember5* cb = create(i, a1, a2, a3, a4, a5, priority);
      EventScheduler::schedule(delta, cb);
    }
  }

  static void scheduleAbs(Time_t tim, ClassType* i, Parameter1 a1, Parameter2 a2, Parameter3 a3, Parameter4 a4, Parameter5 a5,
                          Time_t priority = 0) {
    if (tim == globalClock) {
      (i->*memberPtr)(a1, a2, a3, a4, a5);
    } else {
      CallbackMember5* cb = create(i, a1, a2, a3, a4, a5, priority);
      EventScheduler::scheduleAbs(tim, cb);
    }
  }

  void call() {
    (instance->*memberPtr)(p1, p2, p3, p4, p5);
    destroy();
  }

  void destroy() { cbPool.in(this); }

  void setParam1(Parameter1 a1) { p1 = a1; }
};

template <class ClassType, class Parameter1, class Parameter2, class Parameter3, class Parameter4, class Parameter5,
          void (ClassType::*memberPtr)(Parameter1, Parameter2, Parameter3, Parameter4, Parameter5)>
typename CallbackMember5<ClassType, Parameter1, Parameter2, Parameter3, Parameter4, Parameter5, memberPtr>::poolType
    CallbackMember5<ClassType, Parameter1, Parameter2, Parameter3, Parameter4, Parameter5, memberPtr>::cbPool(32, "CBM5");

/************************************************************************************/

template <class ClassType, class Parameter1, class Parameter2, class Parameter3, class Parameter4,
          void (ClassType::*memberPtr)(Parameter1, Parameter2, Parameter3, Parameter4)>
class CallbackMember4 : public CallbackBase {
private:
  using poolType = pool<CallbackMember4>;

  static poolType cbPool;
  friend class pool<CallbackMember4>;

  Parameter1 p1;
  Parameter2 p2;
  Parameter3 p3;
  Parameter4 p4;

  ClassType* instance;

protected:
  CallbackMember4() {}
  virtual ~CallbackMember4() {}

public:
  static CallbackMember4* create(ClassType* i, Parameter1 a1, Parameter2 a2, Parameter3 a3, Parameter4 a4, Time_t priority = 0) {
    CallbackMember4* cb = cbPool.out();
    cb->resetPriority();
    cb->initPriority(priority);
    cb->instance = i;
    cb->p1       = a1;
    cb->p2       = a2;
    cb->p3       = a3;
    cb->p4       = a4;

    return cb;
  }

  static void schedule(TimeDelta_t delta, ClassType* i, Parameter1 a1, Parameter2 a2, Parameter3 a3, Parameter4 a4,
                       Time_t priority = 0) {
    if (delta == 0) {
      (i->*memberPtr)(a1, a2, a3, a4);
    } else {
      CallbackMember4* cb = create(i, a1, a2, a3, a4, priority);
      EventScheduler::schedule(delta, cb);
    }
  }

  static void scheduleAbs(Time_t tim, ClassType* i, Parameter1 a1, Parameter2 a2, Parameter3 a3, Parameter4 a4,
                          Time_t priority = 0) {
    if (tim == globalClock) {
      (i->*memberPtr)(a1, a2, a3, a4);
    } else {
      CallbackMember4* cb = create(i, a1, a2, a3, a4, priority);
      EventScheduler::scheduleAbs(tim, cb);
    }
  }

  void call() {
    (instance->*memberPtr)(p1, p2, p3, p4);
    destroy();
  }

  void destroy() { cbPool.in(this); }

  void setParam1(Parameter1 a1) { p1 = a1; }
};

template <class ClassType, class Parameter1, class Parameter2, class Parameter3, class Parameter4,
          void (ClassType::*memberPtr)(Parameter1, Parameter2, Parameter3, Parameter4)>
typename CallbackMember4<ClassType, Parameter1, Parameter2, Parameter3, Parameter4, memberPtr>::poolType
    CallbackMember4<ClassType, Parameter1, Parameter2, Parameter3, Parameter4, memberPtr>::cbPool(32, "CBM4");

template <class ClassType, class Parameter1, class Parameter2, class Parameter3,
          void (ClassType::*memberPtr)(Parameter1, Parameter2, Parameter3)>
class CallbackMember3 : public CallbackBase {
private:
  using poolType = pool<CallbackMember3>;

  static poolType cbPool;
  friend class pool<CallbackMember3>;

  Parameter1 p1;
  Parameter2 p2;
  Parameter3 p3;

  ClassType* instance;

protected:
  CallbackMember3() {}
  virtual ~CallbackMember3() {}

public:
  static CallbackMember3* create(ClassType* i, Parameter1 a1, Parameter2 a2, Parameter3 a3, Time_t priority = 0) {
    CallbackMember3* cb = cbPool.out();
    cb->resetPriority();
    cb->initPriority(priority);
    cb->instance = i;
    cb->p1       = a1;
    cb->p2       = a2;
    cb->p3       = a3;

    return cb;
  }

  static void schedule(TimeDelta_t delta, ClassType* i, Parameter1 a1, Parameter2 a2, Parameter3 a3, Time_t priority = 0) {
    if (delta == 0) {
      (i->*memberPtr)(a1, a2, a3);
    } else {
      CallbackMember3* cb = create(i, a1, a2, a3, priority);
      EventScheduler::schedule(delta, cb);
    }
  }

  static void scheduleAbs(Time_t tim, ClassType* i, Parameter1 a1, Parameter2 a2, Parameter3 a3, Time_t priority = 0) {
    if (tim == globalClock) {
      (i->*memberPtr)(a1, a2, a3);
    } else {
      CallbackMember3* cb = create(i, a1, a2, a3, priority);
      EventScheduler::scheduleAbs(tim, cb);
    }
  }

  void call() {
    (instance->*memberPtr)(p1, p2, p3);
    destroy();
  }

  void destroy() { cbPool.in(this); }

  void setParam1(Parameter1 a1) { p1 = a1; }
};

template <class ClassType, class Parameter1, class Parameter2, class Parameter3,
          void (ClassType::*memberPtr)(Parameter1, Parameter2, Parameter3)>
typename CallbackMember3<ClassType, Parameter1, Parameter2, Parameter3, memberPtr>::poolType
    CallbackMember3<ClassType, Parameter1, Parameter2, Parameter3, memberPtr>::cbPool(32, "CBM3");

template <class ClassType, class Parameter1, class Parameter2, void (ClassType::*memberPtr)(Parameter1, Parameter2)>
class CallbackMember2 : public CallbackBase {
private:
  using poolType = pool<CallbackMember2>;

  static poolType cbPool;
  friend class pool<CallbackMember2>;

  Parameter1 p1;
  Parameter2 p2;

  ClassType* instance;

protected:
  CallbackMember2() {}
  virtual ~CallbackMember2() {}

public:
  static CallbackMember2* create(ClassType* i, Parameter1 a1, Parameter2 a2, Time_t priority = 0) {
    CallbackMember2* cb = cbPool.out();
    cb->resetPriority();
    cb->initPriority(priority);
    cb->instance = i;
    cb->p1       = a1;
    cb->p2       = a2;

    return cb;
  }

  static void schedule(TimeDelta_t delta, ClassType* i, Parameter1 a1, Parameter2 a2, Time_t priority = 0) {
    if (delta == 0) {
      (i->*memberPtr)(a1, a2);
    } else {
      CallbackMember2* cb = create(i, a1, a2, priority);
      EventScheduler::schedule(delta, cb);
    }
  }

  static void scheduleAbs(Time_t tim, ClassType* i, Parameter1 a1, Parameter2 a2, Time_t priority = 0) {
    if (tim == globalClock) {
      (i->*memberPtr)(a1, a2);
    } else {
      CallbackMember2* cb = create(i, a1, a2, priority);
      EventScheduler::scheduleAbs(tim, cb);
    }
  }

  void call() {
    (instance->*memberPtr)(p1, p2);
    destroy();
  }

  void destroy() { cbPool.in(this); }

  void setParam1(Parameter1 a1) { p1 = a1; }
};

template <class ClassType, class Parameter1, class Parameter2, void (ClassType::*memberPtr)(Parameter1, Parameter2)>
typename CallbackMember2<ClassType, Parameter1, Parameter2, memberPtr>::poolType
    CallbackMember2<ClassType, Parameter1, Parameter2, memberPtr>::cbPool(32, "CBM2");

template <class ClassType, class Parameter1, void (ClassType::*memberPtr)(Parameter1)>
class CallbackMember1 : public CallbackBase {
private:
  using poolType = pool<CallbackMember1>;

  static poolType cbPool;
  friend class pool<CallbackMember1>;

  Parameter1 p1;

  ClassType* instance;

protected:
  CallbackMember1() {}
  virtual ~CallbackMember1() {}

public:
  static CallbackMember1* create(ClassType* i, Parameter1 a1, Time_t priority = 0) {
    CallbackMember1* cb = cbPool.out();
    cb->resetPriority();
    cb->initPriority(priority);
    cb->instance = i;
    cb->p1       = a1;

    return cb;
  }

  static void schedule(TimeDelta_t delta, ClassType* i, Parameter1 a1, Time_t priority = 0) {
    if (delta == 0) {
      (i->*memberPtr)(a1);
    } else {
      CallbackMember1* cb = create(i, a1, priority);
      EventScheduler::schedule(delta, cb);
    }
  }

  static void scheduleAbs(Time_t tim, ClassType* i, Parameter1 a1, Time_t priority = 0) {
    if (tim == globalClock) {
      (i->*memberPtr)(a1);
    } else {
      CallbackMember1* cb = create(i, a1, priority);
      EventScheduler::scheduleAbs(tim, cb);
    }
  }

  void call() {
    (instance->*memberPtr)(p1);
    destroy();
  }

  void destroy() { cbPool.in(this); }

  void setParam1(Parameter1 a1) { p1 = a1; }
};

template <class ClassType, class Parameter1, void (ClassType::*memberPtr)(Parameter1)>
typename CallbackMember1<ClassType, Parameter1, memberPtr>::poolType CallbackMember1<ClassType, Parameter1, memberPtr>::cbPool(
    32, "CBM1");

template <class ClassType, void (ClassType::*memberPtr)()>
class CallbackMember0 : public CallbackBase {
private:
  using poolType = pool<CallbackMember0>;

  static poolType cbPool;
  friend class pool<CallbackMember0>;

  ClassType* instance;

protected:
  CallbackMember0() {}
  virtual ~CallbackMember0() {}

public:
  static CallbackMember0* create(ClassType* i, Time_t priority = 0) {
    CallbackMember0* cb = cbPool.out();
    cb->resetPriority();
    cb->initPriority(priority);
    cb->instance = i;

    return cb;
  }

  static void schedule(TimeDelta_t delta, ClassType* i, Time_t priority = 0) {
    if (delta == 0) {
      (i->*memberPtr)();
    } else {
      CallbackMember0* cb = create(i, priority);
      EventScheduler::schedule(delta, cb);
    }
  }

  static void scheduleAbs(Time_t tim, ClassType* i, Time_t priority = 0) {
    if (tim == globalClock) {
      (i->*memberPtr)();
    } else {
      CallbackMember0* cb = create(i, priority);
      EventScheduler::scheduleAbs(tim, cb);
    }
  }

  void call() {
    (instance->*memberPtr)();
    destroy();
  }

  void destroy() { cbPool.in(this); }
};

template <class ClassType, void (ClassType::*memberPtr)()>
typename CallbackMember0<ClassType, memberPtr>::poolType CallbackMember0<ClassType, memberPtr>::cbPool(32, "CBM0");

// STATIC SECTION

template <class ClassType, class Parameter1, class Parameter2, void (ClassType::*memberPtr)(Parameter1, Parameter2)>
class StaticCallbackMember2 : public StaticCallbackBase {
private:
#ifndef NDEBUG
  bool isFree;
#endif
  ClassType* instance;
  Parameter1 p1;
  Parameter2 p2;

protected:
public:
  StaticCallbackMember2(ClassType* i) {
    instance = i;
#ifndef NDEBUG
    isFree = true;
#endif
  }
  virtual ~StaticCallbackMember2() {}

  void schedule(TimeDelta_t delta, Parameter1 a1, Parameter2 a2) {
#ifndef NDEBUG
    I(isFree);
#endif
    if (delta == 0) {
      (instance->*memberPtr)(a1, a2);
    } else {
      p1 = a1;
      p2 = a2;
#ifndef NDEBUG
      isFree = false;
#endif
      EventScheduler::schedule(delta, this);
    }
  }

  void scheduleAbs(Time_t tim, Parameter1 a1, Parameter2 a2) {
#ifndef NDEBUG
    I(isFree);
#endif
    if (tim == globalClock) {
      (instance->*memberPtr)(a1, a2);
    } else {
      p1 = a1;
      p2 = a2;
#ifndef NDEBUG
      isFree = false;
#endif
      EventScheduler::scheduleAbs(tim, this);
    }
  }

  void call() {
#ifndef NDEBUG
    isFree = true;
#endif
    (instance->*memberPtr)(p1, p2);
  }

  void setParam1(Parameter1 a1) { p1 = a1; }

  void setParam2(Parameter2 a2) { p2 = a2; }
};

template <class ClassType, class Parameter1, void (ClassType::*memberPtr)(Parameter1)>
class StaticCallbackMember1 : public StaticCallbackBase {
private:
#ifndef NDEBUG
  bool isFree;
#endif
  ClassType* instance;
  Parameter1 p1;

protected:
public:
  StaticCallbackMember1(ClassType* i) {
    instance = i;
#ifndef NDEBUG
    isFree = true;
#endif
  }
  virtual ~StaticCallbackMember1() {}

  void schedule(TimeDelta_t delta, Parameter1 a1) {
#ifndef NDEBUG
    I(isFree);
#endif
    if (delta == 0) {
      (instance->*memberPtr)(a1);
    } else {
      p1 = a1;
#ifndef NDEBUG
      isFree = false;
#endif
      EventScheduler::schedule(delta, this);
    }
  }

  void scheduleAbs(Time_t tim, Parameter1 a1) {
#ifndef NDEBUG
    I(isFree);
#endif
    if (tim == globalClock) {
      (instance->*memberPtr)(a1);
    } else {
      p1 = a1;
#ifndef NDEBUG
      isFree = false;
#endif
      EventScheduler::scheduleAbs(tim, this);
    }
  }

  void call() {
#ifndef NDEBUG
    isFree = true;
#endif
    (instance->*memberPtr)(p1);
  }

  void setParam1(Parameter1 a1) { p1 = a1; }
};

template <class ClassType, void (ClassType::*memberPtr)()>
class StaticCallbackMember0 : public StaticCallbackBase {
private:
#ifndef NDEBUG
  bool isFree;
#endif
  ClassType* instance;

protected:
public:
  StaticCallbackMember0(ClassType* i) {
    instance = i;
#ifndef NDEBUG
    isFree = true;
#endif
  }
  virtual ~StaticCallbackMember0() {}

  void schedule(TimeDelta_t delta) {
#ifndef NDEBUG
    I(isFree);
#endif
    if (delta == 0) {
      call();
    } else {
#ifndef NDEBUG
      isFree = false;
#endif
      EventScheduler::schedule(delta, this);
    }
  }

  void scheduleAbs(Time_t tim) {
#ifndef NDEBUG
    I(isFree);
#endif
    if (tim == globalClock) {
      call();
    } else {
#ifndef NDEBUG
      isFree = false;
#endif
      EventScheduler::scheduleAbs(tim, this);
    }
  }

  void call() {
#ifndef NDEBUG
    isFree = true;
#endif
    (instance->*memberPtr)();
  }
};

/////////////////////////////////////////////////////////////////////////////
//
// DESCRIPTION:
//
// This class is a container based on STL vector, implemented here as a helper
// class for multi-casting of notifications of the callback framework.
//
/////////////////////////////////////////////////////////////////////////////
class CallbackContainer {
private:
  CallbackBase* first;
  CallbackBase* last;

public:
  uint64_t size;
  CallbackContainer() {
    first = nullptr;
    last  = nullptr;
    size  = 0;
  }

  ~CallbackContainer() { I(first == 0); }

  void add(CallbackBase* c) {
    I(c->getNextCallbackBase() == 0);
    c->setNextCallbackBase(0);

    if (last == nullptr) {
      first = c;
      last  = c;
    } else {
#ifdef STRICT_PRIORITY
      if (last->getPriority() <= c->getPriority()) {
        last->setNextCallbackBase(c);
        last = c;
      } else {
        CallbackBase* prev = nullptr;
        CallbackBase* cur  = first;
        while (cur && cur->getPriority() <= c->getPriority()) {
          prev = cur;
          cur  = cur->getNextCallbackBase();
        }
        if (prev == nullptr) {
          c->setNextCallbackBase(first);
          first = c;
        } else {
          prev->setNextCallbackBase(c);
          c->setNextCallbackBase(cur);
        }
        if (cur == nullptr) {
          last = c;
        }
      }
#else
      last->setNextCallbackBase(c);
      last = c;
#endif
    }
    size++;
  }

  void call() {
    // optimization for te most common case
    if (first == nullptr) {
      return;
    }

    do {
      CallbackBase* cb = first;
#ifndef NDEBUG
      CallbackBase* t = first;
#endif
      first = first->getNextCallbackBase();
#ifndef NDEBUG
      t->setNextCallbackBase(0);
#endif
      if (first == nullptr) {
        last = 0;
      }
      cb->call();
      size--;
    } while (first);
  }

  void mycall() {
    // optimization for te most common case
    if (first == nullptr) {
      return;
    }

    uint64_t mysize = size;
    do {
      CallbackBase* cb = first;
#ifndef NDEBUG
      CallbackBase* t = first;
#endif
      first = first->getNextCallbackBase();
#ifndef NDEBUG
      t->setNextCallbackBase(0);
#endif
      if (first == nullptr) {
        last = 0;
      }
      cb->call();
      size--;
      mysize--;
    } while (first && (mysize > 0));
  }

  void callNext() {
    if (first == nullptr) {
      return;
    }

    CallbackBase* cb = first;
#ifndef NDEBUG
    CallbackBase* t = first;
#endif
    first = first->getNextCallbackBase();
#ifndef NDEBUG
    t->setNextCallbackBase(0);
#endif
    if (first == nullptr) {
      last = 0;
    }

    cb->call();
  }

  [[nodiscard]] bool empty() const noexcept { return first == nullptr; }

  void makeEmpty() { first = nullptr; }
};
