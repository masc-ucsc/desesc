// See LICENSE for details.

#pragma once

#include <queue>
#include <set>
#include <vector>

#include "dinst.hpp"
#include "fastqueue.hpp"
#include "iassert.hpp"

typedef uint32_t CPU_t;
class IBucket;

class PipeIBucketLess {
public:
  bool operator()(const IBucket *x, const IBucket *y) const;
};

class Pipeline {
private:
  const size_t         PipeLength;
  const size_t         bucketPoolMaxSize;
  const int32_t        MaxIRequests;
  int32_t              nIRequests;
  FastQueue<IBucket *> buffer;

  typedef std::vector<IBucket *> IBucketCont;
  IBucketCont                    bucketPool;

  // typedef boost::heap::priority_queue<IBucket *,boost::heap::compare<PipeIBucketLess> > ReceivedType;
  typedef std::priority_queue<IBucket *, std::vector<IBucket *>, PipeIBucketLess> ReceivedType;
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

  IBucket *newItem();
  bool     hasOutstandingItems() const;
  void     readyItem(IBucket *b);
  void     doneItem(IBucket *b);
  void     flush_transient_inst_from_buffer();
  IBucket *nextItem();
  IBucket *next_item_transient();


  size_t size() const { return buffer.size(); }
};

class IBucket : public FastQueue<Dinst *> {
private:
protected:
  const bool cleanItem;

  Time_t pipeId;
  Time_t clock;

  friend class Pipeline;
  friend class PipeIBucketLess;

  Pipeline *const pipeLine;
#ifndef NDEBUG
  bool fetched;
#endif

  Time_t getPipelineId() const { return pipeId; }
  void   setPipelineId(Time_t i) { pipeId = i; }

  void markFetched();

  Time_t getClock() const { return clock; }
  void   setClock() { clock = globalClock; }

public:
  IBucket(size_t size, Pipeline *p, bool clean = false);
  virtual ~IBucket() {}

  StaticCallbackMember0<IBucket, &IBucket::markFetched> markFetchedCB;
};

class PipeQueue {
public:
  PipeQueue(CPU_t i);
  ~PipeQueue();

  Pipeline             pipeLine;
  FastQueue<IBucket *> instQueue;
 
};
