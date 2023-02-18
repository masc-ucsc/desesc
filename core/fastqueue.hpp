// See LICENSE for details.

#pragma once

#include <cstdint>
#include <vector>

#include "iassert.hpp"

template <class Data>
class FastQueue {
private:
  std::vector<Data> pipe;

  uint32_t pipeMask;
  uint32_t start;
  uint32_t end;
  uint32_t nElems;

protected:
public:
  FastQueue(std::size_t size) {
    // Find the closest power of two
    I(size);
    pipeMask = size;
    I(size < (256 * 1024));  // define FASTQUEUE_USE_QUEUE for those cases

    while (pipeMask & (pipeMask - 1)) {
      pipeMask++;
    }

    pipe.resize(pipeMask);

    pipeMask--;
    start  = 0;
    end    = 0;
    nElems = 0;
  }

  void push(Data d) {
    I(nElems <= pipeMask);

    //    pipe[(start+nElems) & pipeMask]=d;
    pipe[end] = d;
    I(end == ((start + nElems) & pipeMask));
    end = (end + 1) & pipeMask;
    nElems++;
  }

  Data top() const {
    // I(nElems);
    return pipe[start];
  }

  void pop() {
    nElems--;
    start = (start + 1) & pipeMask;
  }

  uint32_t getIDFromTop(uint32_t i) const {
    I(nElems > i);
    return (start + i) & pipeMask;
  }

  uint32_t getNextId(uint32_t id) const { return (id + 1) & pipeMask; }
  bool     isEnd(uint32_t id) const { return id == end; }

  Data getData(uint32_t id) const {
    I(id <= pipeMask);
    I(id != end);
    return pipe[id];
  }

  Data topNext() const { return getData(getIDFromTop(1)); }

  std::size_t size() const { return nElems; }
  bool        empty() const { return nElems == 0; }
};
