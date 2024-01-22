// See LICENSE for details.

#pragma once

#include <cstdint>
#include <vector>

#include "iassert.hpp"

template <class Data>
class FastQueue {
private:
  std::vector<Data> pipe;
  std::vector<Data> pipe_in_cluster;

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

  
  void push_pipe_in_cluster(Data d) {
    pipe_in_cluster.push_back(d);
  }
  
  void pop_pipe_in_cluster() {
     pipe_in_cluster.pop_back();
  }


  Data top() const {
    // I(nElems);
    return pipe[start];
  }

  Data back_pipe_in_cluster() const {
    // I(nElems);
    return pipe_in_cluster.back();
  }
  
  Data end_data() const {
    // I(nElems);
    //end is the position where new data 
    //is added so need to pop existing data by (end-1) position
    return pipe[end-1];
  }

  void pop() {
    nElems--;
    start = (start + 1) & pipeMask;
  }
  
  void pop_from_back() {
    nElems--;
    //start = (start + 1) & pipeMask;
    end = (end - 1) & pipeMask;
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
  bool  empty_pipe_in_cluster() const { 
          return pipe_in_cluster.empty();
  }

};
