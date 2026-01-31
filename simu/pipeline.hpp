// See LICENSE for details.

#pragma once

#include <queue>
#include <set>
#include <vector>

#include "dinst.hpp"
#include "fastqueue.hpp"
#include "iassert.hpp"

using CPU_t = uint32_t;
class IBucket;

class PipeIBucketLess {
public:
  bool operator()(const IBucket* x, const IBucket* y) const;
};

class Pipeline {
private:
  const size_t        PipeLength;
  const size_t        bucketPoolMaxSize;
  const int32_t       MaxIRequests;
  int32_t             nIRequests;
  FastQueue<IBucket*> buffer;
  FastQueue<IBucket*> transient_buffer;

  using IBucketCont = std::vector<IBucket*>;
  IBucketCont bucketPool;

  // using ReceivedType = boost::heap::priority_queue<IBucket *,boost::heap::compare<PipeIBucketLess> >;
  using ReceivedType = std::priority_queue<IBucket*, std::vector<IBucket*>, PipeIBucketLess>;
  // std::priority_queue<IBucket *, std::vector<IBucket*>, PipeIBucketLess> received;
  ReceivedType received;

  Time_t maxItemCntr;
  Time_t minItemCntr;

protected:
  void clearItems();

public:
  Pipeline(size_t s, size_t fetch, int32_t maxReqs);
  virtual ~Pipeline();

  void cleanMark();

  // FastQueue<Dinst *>   transient_buffer;
  [[nodiscard]] IBucket* newItem();
  [[nodiscard]] bool     hasOutstandingItems() const;
  void                   readyItem(IBucket* b);
  void                   doneItem(IBucket* b);
  void                   flush_transient_inst_from_buffer();
  [[nodiscard]] bool     transient_buffer_empty();
  [[nodiscard]] IBucket* nextItem();

  [[nodiscard]] size_t size() const noexcept { return buffer.size(); }
  [[nodiscard]] size_t bucketPool_size() const noexcept { return bucketPool.size(); }
};

class IBucket : public FastQueue<Dinst*> {
private:
protected:
  const bool cleanItem;

  Time_t pipeId;
  Time_t clock;

  friend class Pipeline;
  friend class PipeIBucketLess;

  Pipeline* const pipeLine;
#ifndef NDEBUG
  bool fetched;
#endif

  [[nodiscard]] Time_t getPipelineId() const noexcept { return pipeId; }
  void                 setPipelineId(Time_t i) { pipeId = i; }

  void markFetched();

  [[nodiscard]] Time_t getClock() const noexcept { return clock; }
  void                 setClock() { clock = globalClock; }

public:
  IBucket(size_t size, Pipeline* p, bool clean = false);
  virtual ~IBucket() = default;

  StaticCallbackMember0<IBucket, &IBucket::markFetched> markFetchedCB;
};

class PipeQueue {
public:
  PipeQueue(CPU_t i);
  ~PipeQueue();

  Pipeline            pipeLine;
  FastQueue<IBucket*> instQueue;
};
