// See LICENSE for details.

#include "pipeline.hpp"

#include <vector>

#include "config.hpp"
#include "gprocessor.hpp"

IBucket::IBucket(size_t size, Pipeline* p, bool clean)
    : FastQueue<Dinst*>(size), cleanItem(clean), pipeLine(p), markFetchedCB(this) {}

void IBucket::markFetched() {
#ifndef NDEBUG
  I(fetched == false);
  fetched = true;  // Only called once
#endif

  if (!empty()) {
    //    if (top()->getFlowId())
    //      MSG("@%lld: markFetched Bucket[%p]",(long long int)globalClock, this);
  }

  // printf("Pipeline::readyitem::markfetched() complete\n");
  pipeLine->readyItem(this);
}

bool PipeIBucketLess::operator()(const IBucket* x, const IBucket* y) const { return x->getPipelineId() > y->getPipelineId(); }

Pipeline::Pipeline(size_t s, size_t fetch, int32_t maxReqs)
    : PipeLength(s)
    , bucketPoolMaxSize(s + 1 + maxReqs)
    , MaxIRequests(maxReqs)
    , nIRequests(maxReqs)
    , buffer(s + 1 + maxReqs)
    , transient_buffer(s + 1 + maxReqs)

{
  maxItemCntr = 0;
  minItemCntr = 0;

  bucketPool.reserve(bucketPoolMaxSize);
  I(bucketPool.empty());

  for (size_t i = 0; i < bucketPoolMaxSize; i++) {
    IBucket* ib = new IBucket(fetch + 1, this);  // +1 instructions
    bucketPool.push_back(ib);
  }

  I(bucketPool.size() == bucketPoolMaxSize);
}

Pipeline::~Pipeline() {
  while (!bucketPool.empty()) {
    delete bucketPool.back();
    bucketPool.pop_back();
  }
  while (!buffer.empty()) {
    delete buffer.top();
    buffer.pop();
  }
  while (!received.empty()) {
    delete received.top();
    received.pop();
  }
}

void Pipeline::readyItem(IBucket* b) {
  b->setClock();

  nIRequests++;
  if (b->getPipelineId() != minItemCntr) {
    received.push(b);
    return;
  }

  // If the message is received in-order. Do not use the sorting
  // receive structure (remember that a cache can respond
  // out-of-order the memory requests)
  minItemCntr++;

  if (b->empty()) {
    doneItem(b);
  } else {
    buffer.push(b);
    // printf("Pipeline::readyitem::buffersize is %lu\n", buffer.size());
  }

  clearItems();  // Try to insert on minItem reveiced (OoO) buckets
}

void Pipeline::clearItems() {
  while (!received.empty()) {
    IBucket* b = received.top();

    if (b->getPipelineId() != minItemCntr) {
      break;
    }

    received.pop();

    minItemCntr++;

    if (b->empty()) {
      doneItem(b);
    } else {
      buffer.push(b);
    }
  }
}

void Pipeline::doneItem(IBucket* b) {
  I(b->getPipelineId() < minItemCntr);
  I(b->empty());
  b->clock = 0;

  bucketPool.push_back(b);
}
bool Pipeline::transient_buffer_empty() { return transient_buffer.empty(); }

void Pipeline::flush_transient_inst_from_buffer() {
  while (!buffer.empty()) {
    auto* bucket = buffer.end_data();
    I(bucket);
    I(!bucket->empty());

    while (!bucket->empty()) {
      auto* dinst = bucket->end_data();
      I(dinst);
      I(!dinst->is_present_in_rob());
      if (dinst->isTransient()) {
        dinst->destroyTransientInst();
        bucket->pop_from_back();
      } else {
        return;  // Nothing else to do
      }
    }  // while_!bucket_empty buffer.pop();
    if (bucket->empty()) {
      // printf("Pipeline::flush::bucket.empty () \n");
      I(bucket->empty());
      bucket->clock = 0;
      buffer.pop_from_back();
      bucketPool.push_back(bucket);
    }
  }
}

IBucket* Pipeline::nextItem() {
  while (1) {
    if (buffer.empty()) {
#ifndef NDEBUG
      // It should not be possible to propagate more buckets
      clearItems();
      I(buffer.empty());
#endif
      return 0;
    }

    if (((buffer.top())->getClock() + PipeLength) > globalClock) {
#if 0
//#if 1
        fprintf(stderr,"1 @%lld Buffer[%p] .top.ID (%d) ->getClock(@%lld) to be issued after %d cycles\n" 
           ,(long long int) globalClock
           ,buffer.top()
           ,(int) ((buffer.top())->top())->getID()
           ,(long long int)((buffer.top())->getClock())
           ,PipeLength
           );
#endif
      return 0;
    } else {
#if 0
//#if 1
       fprintf(stderr,"2 @%lld Buffer[%p] .top.ID (%d) ->getClock(@%lld) to be issued after %d cycles\n"1
          ,(long long int) globalClock
          ,buffer.top()
          ,(int) ((buffer.top())->top())->getID()
          ,(long long int)((buffer.top())->getClock())
          ,PipeLength
          );
#endif
    }
    IBucket* b = buffer.top();
    buffer.pop();
    // fprintf(stderr,"@%lld: Popping Bucket[%p]\n",(long long int)globalClock ,b);
    I(!b->empty());
    I(!b->cleanItem);

    I(!b->empty());
    I(b->top() != 0);

    return b;
  }
}

PipeQueue::PipeQueue(CPU_t i)
    : pipeLine(Config::get_integer("soc", "core", i, "decode_delay", 1, 64)
                   + Config::get_integer("soc", "core", i, "rename_delay", 1, 8),
               Config::get_integer("soc", "core", i, "fetch_width", 1, 64),
               Config::get_integer("soc", "core", i, "ftq_size", 1, 64))
    , instQueue(Config::get_integer("soc", "core", i, "decode_bucket_size", 1, 128)) {}

PipeQueue::~PipeQueue() {
  // do nothing
}

IBucket* Pipeline::newItem() {
  if (nIRequests == 0 || bucketPool.empty()) {
    return 0;
  }

  nIRequests--;

  IBucket* b = bucketPool.back();
  bucketPool.pop_back();

  b->setPipelineId(maxItemCntr);

  maxItemCntr++;

#ifndef NDEBUG
  b->fetched = false;
  I(b->empty());
#endif

  return b;
}

bool Pipeline::hasOutstandingItems() const {
  // bucketPool.size() has lineal time O(n)
#if 0
  if (!buffer.empty()){
    MSG("Pipeline !buffer.empty()");
  }

  if (!received.empty()){
    MSG("Pipeline !received.empty()");
  }

  if (nIRequests < MaxIRequests){
    MSG("Pipeline nIRequests(%d) < MaxIRequests(%d)",nIRequests, MaxIRequests);
  }
#endif
  return !buffer.empty() || !received.empty() || nIRequests < MaxIRequests;
}
