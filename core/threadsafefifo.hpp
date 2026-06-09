#ifndef THREADSAFEFIFO_H
#define THREADSAFEFIFO_H

#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <time.h>
#include <unistd.h>

#include <cstdlib>

#include "snippets.hpp"

template <class Type>
class ThreadSafeFIFO {
private:
  typedef uint16_t   IndexType;
  volatile IndexType tail;
  volatile IndexType head;
  Type               array[32768];

public:
  uint16_t size() const { return 32768 / 2 - 2048; }

  ThreadSafeFIFO() : tail(0), head(0) {}
  virtual ~ThreadSafeFIFO() {}

  Type* getTailRef() { return &array[tail]; }

  void push() {
    // AtomicAdd(&tail,static_cast<IndexType>(1));
    tail = (tail + 1) & 32767;
  };
  void push(const Type* item_) {
    array[tail] = *item_;
    push();
  };

  bool full() const {
    if (((tail + 2) & 32767) == head) {
      return true;
    }
    IndexType nextTail = ((tail + 1) & 32767);  // Give some space
    return (nextTail == head);
  }

  bool halfFull() const {
    uint32_t n;
    if (head > tail) {
      n = 32768 - head + tail;
    } else {
      n = tail - head;
    }

    return n > size();
  }

  bool empty() { return (tail == head); }

  void pop() {
    // AtomicAdd(&head,static_cast<IndexType>(1));
    head = (head + 1) & 32767;
  };
  Type* getHeadRef() { return &array[head]; }
  Type* getNextHeadRef() { return &array[static_cast<IndexType>((head + 1) & 32767)]; }
  void  pop(Type* obj) {
    *obj = array[head];
    // AtomicAdd(&head,static_cast<IndexType>(1));
    head = (head + 1) & 32767;
  };
};

#endif
