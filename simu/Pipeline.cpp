// See LICENSE for details.

#include "pipeline.hpp"

#include "gprocessor.hpp"
#include "config.hpp"

IBucket::IBucket(size_t size, Pipeline *p, bool clean)
    : FastQueue<Dinst *>(size), cleanItem(clean), pipeLine(p), markFetchedCB(this) {}

void IBucket::markFetched() {
#ifndef NDEBUG
  I(fetched == false);
  fetched = true;  // Only called once
#endif

  if (!empty()) {
    //    if (top()->getFlowId())
    //      MSG("@%lld: markFetched Bucket[%p]",(long long int)globalClock, this);
  }

  printf("Pipeline::readyitem::markfetched() complete\n"); 
  pipeLine->readyItem(this);
}

bool PipeIBucketLess::operator()(const IBucket *x, const IBucket *y) const { return x->getPipelineId() > y->getPipelineId(); }

Pipeline::Pipeline(size_t s, size_t fetch, int32_t maxReqs)
    : PipeLength(s)
    , bucketPoolMaxSize(s + 1 + maxReqs)
    , MaxIRequests(maxReqs)
    , nIRequests(maxReqs)
    , buffer(s + 1 + maxReqs)

{
  maxItemCntr = 0;
  minItemCntr = 0;

  bucketPool.reserve(bucketPoolMaxSize);
  I(bucketPool.empty());

  for (size_t i = 0; i < bucketPoolMaxSize; i++) {
    IBucket *ib = new IBucket(fetch + 1, this);  // +1 instructions
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

void Pipeline::readyItem(IBucket *b) {
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
    printf("Pipeline::readyitem::buffersize is %lu\n",buffer.size()); 
  }

  clearItems();  // Try to insert on minItem reveiced (OoO) buckets
}

void Pipeline::clearItems() {
  while (!received.empty()) {
    IBucket *b = received.top();

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

void Pipeline::doneItem(IBucket *b) {
  I(b->getPipelineId() < minItemCntr);
  I(b->empty());
  b->clock = 0;

  bucketPool.push_back(b);
}

/*void Pipeline::flush_transient_inst_from_buffer() {
  
  uint32_t buffer_size = buffer.size();
  printf("Pipeline::flush::Entering ::buffersize is %u\n",buffer_size); 
  for ( uint32_t i = 0; i < buffer_size; i++) {
    uint32_t pos = buffer.getIDFromTop(i);
    IBucket *bucket = buffer.getData(pos);
    if (bucket) {
    uint32_t bucket_size = bucket->size();
    for( uint32_t j = 0; j < bucket_size; j++) {
      uint32_t p = bucket->getIDFromTop(j);
      if(!bucket->empty()){
      Dinst *dinst = bucket->getData(p);
      if (dinst->isTransient() && !dinst->is_present_in_rob()){
        dinst-> destroyTransientInst();
        //bucket->pop();
        
      }
      }
    }//j_loop
    
    if(bucket->empty()) {
      I(bucket->empty());
      bucket->clock = 0;
      bucketPool.push_back(bucket);
      //buffer.pop();
    }
    }//if(bucket)
  }//i_loop
}

*/
  void Pipeline::flush_transient_inst_from_buffer() {
    printf("Pipeline::flush::Entering ::buffersize is %lu\n",buffer.size()); 
    
    while(!buffer.empty()) {
    
    printf("Pipeline::flush::!buffer.empty () buffer size inside is %lu\n",buffer.size()); 
    auto *bucket = next_item_transient();
    if (bucket) {
      while(!bucket->empty()) {
        auto *dinst = bucket->top();
        I(dinst);
        if(dinst) { 
          printf("Pipeline::flush::bucket.size is  %lu and instID %ld and Transient is %b\n",bucket->size(), dinst->getID(), dinst->isTransient()); 
        }
        bucket->pop();
        //I(dinst->isTransient());
        if (dinst->isTransient() && !dinst->is_present_in_rob()) {
         I(dinst->isTransient());
         printf("Pipeline::flush:: destroying transient bucket size is %lu and instID is %ld\n",bucket->size(), 
            dinst->getID());  
         dinst->destroyTransientInst();
         } else {
         //push to a new buffer_rob_shadow;
         }
      }
      //buffer.pop();
      if(bucket->empty()) {
       printf("Pipeline::flush::bucket.empty () \n"); 
        I(bucket->empty());
        bucket->clock = 0;
        bucketPool.push_back(bucket);
      }
    }
    }
  }
IBucket *Pipeline::next_item_transient() {
  printf("Pipeline::nextItemtran::buffer.top()  \n"); 
  //I(!buffer.empty());
  //I(buffer.top() != 0);
  IBucket *b = buffer.top();
  I(!buffer.empty());
  buffer.pop();
  I(!b->empty());
  I(!b->cleanItem);

  I(!b->empty());
  I(b->top() != 0);

  printf("Pipeline::buffer->nextItem()::returns! \n"); 
  if(b) {
    return b;
  } else {
    printf(" Pipeline::next_item_transient return no buffer.top \n");
    return 0;
  }
}



IBucket *Pipeline::nextItem() {
  while (1) {
    if (buffer.empty()) {
       printf("Pipeline::NextItem empty () \n"); 

#ifndef NDEBUG
      // It should not be possible to propagate more buckets
      clearItems();
      I(buffer.empty());
#endif
      return 0;
    }

    if (((buffer.top())->getClock() + PipeLength) > globalClock) {
       printf("Pipeline::NextItem  () buffer.top())->getClock() + PipeLength) ::returns 0\n"); 
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
    printf("Pipeline::buffer->nextItem()::butter.top()  \n"); 
    IBucket *b = buffer.top();
    buffer.pop();
    //fprintf(stderr,"@%lld: Popping Bucket[%p]\n",(long long int)globalClock ,b);
    I(!b->empty());
    I(!b->cleanItem);

    I(!b->empty());
    I(b->top() != 0);

    printf("Pipeline::buffer->nextItem()::returns! \n"); 
    return b;
    }
}


PipeQueue::PipeQueue(CPU_t i)
    : pipeLine(
        Config::get_integer("soc", "core", i, "decode_delay", 1, 64) + Config::get_integer("soc", "core", i, "rename_delay", 1, 8),
        Config::get_integer("soc", "core", i, "fetch_width", 1, 64), Config::get_integer("soc", "core", i, "ftq_size", 1, 64))
    , instQueue(Config::get_integer("soc", "core", i, "instq_size", 1, 128)) {
  auto fetch_width = Config::get_integer("soc", "core", i, "fetch_width", 1, 64);
  Config::get_integer("soc", "core", i, "instq_size", fetch_width, 128);
}

PipeQueue::~PipeQueue() {
  // do nothing
}

IBucket *Pipeline::newItem() {
  if (nIRequests == 0 || bucketPool.empty()) {
    return 0;
  }

  nIRequests--;

  IBucket *b = bucketPool.back();
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
