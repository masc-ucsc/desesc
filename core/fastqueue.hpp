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
  explicit FastQueue(std::size_t size) {
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

    printf("fastqueue ::push:: nElems is  %lu and pipemask is %ld\n", nElems, pipeMask);
    pipe[end] = d;
    I(end == ((start + nElems) & pipeMask));
    end = (end + 1) & pipeMask;
    nElems++;
  }

  void push_pipe_in_cluster(Data d) { pipe_in_cluster.push_back(d); }

  void pop_pipe_in_cluster() { pipe_in_cluster.pop_back(); }

  Data top() const {
    // I(nElems);
    return pipe[start];
  }

  Data back_pipe_in_cluster() const {
    // I(nElems);
    return pipe_in_cluster.back();
  }

  Data end_data() const {
    I(nElems);
    printf("fastqueue ::end_data :: Elements are  %lu and  end position is %ld and new end is %ld\n",
           nElems,
           pipeMask,
           end,
           end - 1);
    return pipe[end ? end - 1 : pipeMask];
  }

  Data nilufar_push_to_end_data(Data d) {
    I(nElems <= pipeMask);

    printf("fastqueue ::push:: nElems is  %lu and pipemask is %ld\n", nElems, pipeMask);
    pipe[end + 1] = d;
    I(end == ((start + nElems) & pipeMask));
    end = (end + 1) & pipeMask;
    nElems++;
  }

  void pop() {
    I(nElems);
    nElems--;
    start = (start + 1) & pipeMask;
  }

  void pop_from_back() {
    I(nElems);
    printf("fastqueue ::pop_from_back ::  Before Elements are  %lu and  end position is %ld\n", nElems, end);
    nElems--;
    end = (end - 1) & pipeMask;
    printf("fastqueue ::pop_from_back :: After Elements are  %lu and  end position is %ld\n", nElems, end);
  }

  [[nodiscard]] uint32_t getIDFromTop(uint32_t i) const {
    I(nElems > i);
    return (start + i) & pipeMask;
  }

  [[nodiscard]] uint32_t getNextId(uint32_t id) const { return (id + 1) & pipeMask; }
  [[nodiscard]] bool     isEnd(uint32_t id) const { return id == end; }

  Data getData(uint32_t id) const {
    I(id <= pipeMask);
    I(id != end);
    return pipe[id];
  }

  Data topNext() const { return getData(getIDFromTop(1)); }

  [[nodiscard]] std::size_t size() const { return nElems; }
  [[nodiscard]] bool        empty() const { return nElems == 0; }
  [[nodiscard]] bool        empty_pipe_in_cluster() const { return pipe_in_cluster.empty(); }
};
