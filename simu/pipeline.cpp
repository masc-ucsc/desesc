// See LICENSE for details.

#include "pipeline.hpp"

#include <vector>

#include "config.hpp"
#include "gprocessor.hpp"

IBucket::IBucket(size_t size, Pipeline* p, bool clean) : FastQueue<Dinst*>(size), cleanItem(clean), pipeLine(p) {}

void IBucket::markFetched() {
#ifndef NDEBUG
  I(fetched == false);
  fetched = true;  // Only called once
#endif

  if (!empty()) {
    //    if (top()->getFlowId())
  }

  //printf("Pipeline::markFetched:: Came from FetchEngine:: Memrequest at @Clockcyle %lu\n", globalClock);
  //printf("Pipeline::markfetched::bucket->PipelineID is %lu at @Clockcyle %lu \n", this->getPipelineId(), globalClock);
  //printf("Pipeline::markFetched::Now send to pipeline::readyitem at @clock %lu\n", globalClock);
  pipeLine->readyItem(this);
}

bool PipeIBucketLess::operator()(const IBucket* x, const IBucket* y) const { return x->getPipelineId() > y->getPipelineId(); }

Pipeline::Pipeline(size_t s, size_t fetch, int32_t maxReqs)
    : PipeLength(s)
    , bucketPoolMaxSize(s + 2 + maxReqs)
    //, bucketPoolMaxSize(s + 9 + maxReqs)
    , MaxIRequests(maxReqs)
    , nIRequests(maxReqs)
    , buffer(s + 1 + maxReqs)
    , transient_buffer(s + 2 + maxReqs)

{
  maxItemCntr = 0;
  minItemCntr = 0;  // next ticket number to serve; should be the lowerest inorder; the outoforder are kept in received.

  bucketPool.reserve(bucketPoolMaxSize);
  I(bucketPool.empty());
  //printf("Pipeline::Pipeline:: bucketPoolMaxSize is %ld\n", bucketPoolMaxSize);

  for (size_t i = 0; i < bucketPoolMaxSize; i++) {
    IBucket* ib = new IBucket(fetch + 1, this);  // +1 instructions
    bucketPool.push_back(ib);
  }

  flushing_from_last_transientid = 0;
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

// push fetched Inst(IF->PipelineQ) into PipelineQ
// Buffer is the biggest one: buckets resides inside buffer
void Pipeline::readyItem(IBucket* b) {
  // printf("Pipeline::readyitem::Entering readyitem \n");
  b->setClock();
  b->reset_transient();
  nIRequests++;
  //Time_t flushing_from_last_transientid = gproc->get_flushing_last_transientid();
  /*if (!b->empty() && b->top()->isTransient() && flushing_from_last_transientid >= b->top()->getID()) {
   
    while (!b->empty()) {
      auto* dinst = b->end_data();
      I(dinst);
      I(!dinst->is_present_in_rob());
      if (dinst->isTransient()) {
        //printf("Pipeline::itemready  transient destroy instID %lu\n", dinst->getID());
        dinst->destroyTransientInst();
        b->pop_from_back();
      } else {
        // printf("Pipeline::itemready NOT TRansient anymore instID %lu\n", dinst->getID());
        break;
      }
    }
  }*/
      
      /*else {
        // printf("Pipeline::flush_transient_int_from_Pipelinebuffer No inst in PipeLineBuffer \n");
        if (bucket->empty()) {
          doneItem(bucket);
        }
      }*/





  // out-of-order pipelineId are kept separately in recieved; latter works on them
  if (b->getPipelineId() != minItemCntr) {
    // printf(
        // "Pipeline::readyitem-->recieved.push(b) PipelineID != minItemCntr !!!\nbucket->PipelineID is %llu and minItemCntr is %llu "
        // "\n",
        // b->getPipelineId(),
        // minItemCntr);
    // printf("Pipeline::readyitem::recived.push(bucket)::not actual !buffer.push() inst  %llu at @clockcycle %llu\n",
           // b->top()->getID(),
           // globalClock);
    received.push(b);
    return;
  }

  // If the message is received in-order. Do not use the sorting
  // receive structure (remember that a cache can respond
  // out-of-order the memory requests)
  // printf("Pipeline::readyitem-->PipelineID == minItemCntr !!!\nbucket->PipelineID is %llu and minItemCntr is %llu \n",
         // b->getPipelineId(),
         // minItemCntr);
  minItemCntr++;
  // printf("Pipeline::readyitem:: minItemCntr++ bucket->PipelineID is %llu and minItemCntr is %llu \n",
         // b->getPipelineId(),
         // minItemCntr);

  if (b->empty()) {
    //printf("Pipeline::readyitem::bufferEmpty buffer size is %lu\n", buffer.size());
    doneItem(b);
  } else {
    buffer.push(b);
    //printf("Pipeline::readyitem::buffersize is %lu\n", buffer.size());
    //printf("Pipeline::ReadyItem::pushing bucket--into-->buffer:: inst %lu at @clockcycle %lu\n", b->top()->getID(), globalClock);
  }
  // clear received
  clearItems();
}

void Pipeline::clearItems() {
  // printf("Pipeline::clearitem::Entering clearitem \n");
  // printf("Pipeline::clearitem::minItemCntr :: Before minItemCntr is %llu \n", minItemCntr);
  
  // Check if minItemCntr was already freed during transient flush
  //flushedPipelineIDs={4,5,10}
  //mintitemcnt=3 :waiting for pipeiD=3
  
  while (true) {
      // Check if minItemCntr was already freed during transient flush
    auto it = std::find(flushedPipelineIDs.begin(), flushedPipelineIDs.end(), minItemCntr);
    if (it != flushedPipelineIDs.end()) {
      //printf("Pipeline::clearitem::skipping flushed pipelineId %ld minItemCntr now %ld\n",
               //minItemCntr, minItemCntr + 1);
      flushedPipelineIDs.erase(it);
      minItemCntr++;
      continue;  // keep looping — next ID might also be in flushedPipelineIDs
    }
    
    if (received.empty()) {
      break;
    }
    IBucket* b = received.top();
    if (b->getPipelineId() != minItemCntr) {
      break;
    }
    //if(b->getPipelineId() == minItemCntr) starts herei!!!
    received.pop();
    minItemCntr++;
    //printf("Pipeline::clearitem::minItemCntr++ now %ld\n", minItemCntr);
      
    if (b->empty()) {
      doneItem(b);
    } else {
      buffer.push(b);
      flush_transient_inst_from_buffer();
    }
  }
}




/*void Pipeline::clearItems() {
  // printf("Pipeline::clearitem::Entering clearitem \n");
  // printf("Pipeline::clearitem::minItemCntr :: Before minItemCntr is %llu \n", minItemCntr);
  while (!received.empty()) {
    IBucket* b = received.top();

    if (b->getPipelineId() != minItemCntr) {
      break;
    }

    received.pop();

    // printf("Pipeline::clearitem::minItemCntr :: Before minItemCntr is %llu \n", minItemCntr);
    // should be minItemCnt--
    minItemCntr++;
    // printf("Pipeline::clearitem::minItemCntr++ ::AFter  minItemCntr is %llu \n", minItemCntr);

    if (b->empty()) {
      doneItem(b);
    } else {
      //pushing recieved to buffer and then flush
      buffer.push(b);
      flush_transient_inst_from_buffer();
    }
  }
}
*/
void Pipeline::doneItem(IBucket* b) {
  I(b->getPipelineId() < minItemCntr);
  I(b->empty());
  b->clock = 0;
  b->reset_transient();
  //printf("Pipeline::doneItem::bucket.empty()-->bucketpool.push() \n");
  // printf("Pipeline::flush_buffer::bucket.empty()-->bucketpool.push() \n");

  //printf("Pipeline::doneItem:: Before bucketPool Size is %ld and bucketPoolMaxSize is %ld\n", bucketPool.size(), bucketPoolMaxSize);
  bucketPool.push_back(b);
  //printf("Pipeline::doneItem:: After stocked bucketPool++ Size is %ld and bucketPoolMaxSize is %ld\n",
         //bucketPool.size(),
         //bucketPoolMaxSize);
}

bool Pipeline::transient_buffer_empty() { return transient_buffer.empty(); }

IBucket* Pipeline::flush_transient_inst_from_bucket(IBucket* b) { return b; }

void Pipeline::flush_transient_inst_from_buffer() {
  // printf("Pipeline::flush_transient_int_from_Pipelinebuffer Entering before new fetch!!!\n");
  while (!buffer.empty()) {
    auto* bucket = buffer.end_data();
    I(bucket);
    // I(!bucket->empty());

    while (!bucket->empty()) {
      auto* dinst = bucket->end_data();
      I(dinst);
      I(!dinst->is_present_in_rob());
      if (dinst->isTransient()) {
        // printf("Pipeline::flush_transient_int_from_Pipelinebuffer destroy instID %llu\n", dinst->getID());
        dinst->destroyTransientInst();
        bucket->pop_from_back();
      } else {
        // printf("Pipeline::flush_transient_int_from_Pipelinebuffer No inst in PipeLineBuffer \n");
        if (bucket->empty()) {
          doneItem(bucket);
        }
        return;  // Nothing else to do
      }
    }  // while_!bucket_empty buffer.pop();
    if (bucket->empty()) {
      // printf("Pipeline::flush_buffer::bucket.empty()-->bucketpool.push() \n");
      I(bucket->empty());
      doneItem(bucket);
      buffer.pop_from_back();
    } else {
      buffer.pop_from_back();
      // printf("Pipeline::flush_buffer::!bucket.empty()-->!bucketpool.push() \n");
    }
    // limamustbuffer.pop_from_back();
  }
}

void Pipeline::flush_transient_inst_from_received_bucket() {
  //printf("Pipeline::flush_transient_int_from_received_bucket Entering !!!\n");
  //printf("Pipeline::flush_transient_int_from_received_bucket Stocked bucketPool.Size is %ld\n", bucketPool.size());
  std::vector<IBucket*> to_return;  // non-empty buckets go back into received

  if(received.empty()) {
    //printf("Pipeline::flush_transient_int_from_Received_bucket OH!!! Received empty return!!!\n");
    return;
  }

  while (!received.empty()) {
    IBucket* bucket = received.top();
    received.pop();
     //printf("Pipeline::flush_transient_int_from_Received_bucket New Bucket Started!!! \n");

    if (bucket) {
      while (!bucket->empty()) {
       //printf("Pipeline::flush_transient_int_from_Received_bucket Bucket:: bucket->PipelineID is %lu and minItemCntr is %lu \n",
               //bucket->getPipelineId(),
               //minItemCntr);
        auto* dinst = bucket->end_data();
        I(dinst);
        I(!dinst->is_present_in_rob());
        if (dinst->isTransient()) {
          //printf("Pipeline::flush_transient_int_from_received_bucket destroy instID %lu\n", dinst->getID());
          dinst->destroyTransientInst();
          bucket->pop_from_back();
        } else {
          //printf("Pipeline::flush_transient_int_from_received_bucket Not Transient so BREAK!!!instID %lu\n", dinst->getID());
          break;// stop at first non-transient
        }
      }  // bucket_empty_while_loop_end

      
      if (bucket->empty()) {
        // Save this pipelineId so clearItems() can advance minItemCntr past it
        flushedPipelineIDs.push_back(bucket->getPipelineId());
        // Free the bucket directly — doneItem's assert (pipelineId < minItemCntr)
        // does not hold yet; clearItems() will advance minItemCntr when it sees
        // this ID in flushedPipelineIDs
        bucket->clock = 0;
        bucket->reset_transient();
        bucketPool.push_back(bucket);
        //printf("Pipeline::flush_transient_int_from_Received_bucket Yahoo!!! bucket==empty!!! \n");
        //printf("Pipeline::flush_received freed pipelineId %ld to pushing to bucketpool\n", bucket->getPipelineId());
        //printf("Pipeline::flush_transient_int_from_received_bucket Stocked bucketPool.Size is %ld\n", bucketPool.size());
      } else {
        // Still has non-transient instructions — put back
        to_return.push_back(bucket);
        //printf("Pipeline::flush_transient_int_from_Received_bucket Yahoo!!! bucket!=empty!!! \n");
        //printf("Pipeline::flush_received freed pipelineId %ld has Non Transient Inst :to_return\n", bucket->getPipelineId());
      }
    
    
    }//if_bucket_end
}//received_empty_loop_end
    for (auto* b : to_return) {
      received.push(b);
    }
  //printf("Pipeline::flush_transient_int_from_received_bucket Leaving !!!\n");
}
      
      
      
      
      
      
/*      
      
      
      if (bucket->getPipelineId() == minItemCntr) {
        // printf(
            // "Pipeline::flush_transient_int_from_Received_bucket Bucket minItemcntr++ ::PipelineID == minItemCntr "
            // "!!!\nbucket->PipelineID is %llu and minItemCntr is %llu \n",
            // bucket->getPipelineId(),
            // minItemCntr);
        minItemCntr++;
      }

      if (bucket->getPipelineId() != minItemCntr) {
        // printf(
            // "Pipeline::flush_transient_int_from_Received_bucket Bucket ended BREAK PipelineID != minItemCntr "
            // "!!!\nbucket->PipelineID is %llu and minItemCntr is %llu \n",
            // bucket->getPipelineId(),
            // minItemCntr);

        bucket->set_transient();
        break;
      }
      if (bucket->empty()) {
        bucket->clock = 0;
        printf("Pipeline::flush_transient_int_from_Received_bucket BucketEmpty-->Push to BucketPOOL!!! \n");
        doneItem(bucket);
      }
      received.pop();
    }  // if_bucket
  }    //! recieved_empty
}
*/

/*void Pipeline::flush_transient_inst_from_received_bucket() {
   printf("Pipeline::flush_transient_int_from_received_bucket Entering before new fetch!!!\n");
   printf("Pipeline::flush_transient_int_from_received_bucket Stocked bucketPool.Size is %ld\n", bucketPool.size());
  while (!received.empty()) {
    IBucket* bucket = received.top();
    // printf("Pipeline::flush_transient_int_from_Received_bucket New Bucket Started!!! \n");
    if (bucket->getPipelineId() != minItemCntr) {
      // printf(
          // "Pipeline::flush_transient_int_from_Received_bucket Bucket ended BREAK PipelineID != minItemCntr !!!\nbucket->PipelineID "
          // "is %llu and minItemCntr is %llu \n",
          // bucket->getPipelineId(),
          // minItemCntr);

      bucket->set_transient();
      break;
    }

    if (bucket) {
      while (!bucket->empty()) {
        // printf("Pipeline::flush_transient_int_from_Received_bucket Bucket:: bucket->PipelineID is %llu and minItemCntr is %llu \n",
               // bucket->getPipelineId(),
               // minItemCntr);
        auto* dinst = bucket->end_data();
        I(dinst);
        I(!dinst->is_present_in_rob());
        if (dinst->isTransient()) {
          // printf("Pipeline::flush_transient_int_from_received_bucket destroy instID %llu\n", dinst->getID());
          dinst->destroyTransientInst();
          bucket->pop_from_back();
        } else {
          return;  // Nothing else to do
        }
      }  // bucket_empty_while
      // printf("Pipeline::flush_transient_int_from_Received_bucket Yahoo!!! 1 Bucket ended:: bucket==empty!!! \n");

      if (bucket->getPipelineId() == minItemCntr) {
        // printf(
            // "Pipeline::flush_transient_int_from_Received_bucket Bucket minItemcntr++ ::PipelineID == minItemCntr "
            // "!!!\nbucket->PipelineID is %llu and minItemCntr is %llu \n",
            // bucket->getPipelineId(),
            // minItemCntr);
        minItemCntr++;
      }

      if (bucket->getPipelineId() != minItemCntr) {
        // printf(
            // "Pipeline::flush_transient_int_from_Received_bucket Bucket ended BREAK PipelineID != minItemCntr "
            // "!!!\nbucket->PipelineID is %llu and minItemCntr is %llu \n",
            // bucket->getPipelineId(),
            // minItemCntr);

        bucket->set_transient();
        break;
      }
      if (bucket->empty()) {
        bucket->clock = 0;
        printf("Pipeline::flush_transient_int_from_Received_bucket BucketEmpty-->Push to BucketPOOL!!! \n");
        doneItem(bucket);
      }
      received.pop();
    }  // if_bucket
  }    //! recieved_empty
}
*/
IBucket* Pipeline::nextItem() {
  // printf("Pipeline::nextitem::Entering nextitem \n");
  while (1) {
    if (buffer.empty()) {
#ifndef NDEBUG
      // It should not be possible to propagate more buckets
      // printf("Pipeline::nextitem::Bufferempty+ so return NULL!!!\n");
      clearItems();
      I(buffer.empty());
#endif
      return nullptr;
    }

    if (((buffer.top())->getClock() + PipeLength) > globalClock) {
      return nullptr;
    }
    IBucket* b = buffer.top();
    buffer.pop();
    I(!b->empty());
    I(!b->cleanItem);

    I(!b->empty());
    I(b->top() != nullptr);

    if (b) {
      // Dinst* dinst = b->top();
      // printf("Pipeline::nextitem inst  %llu at @clockcycle %llu\n", dinst->getID(), globalClock);
    }
    return b;
  }
}

PipeQueue::PipeQueue(CPU_t i)
    : pipeLine(
        Config::get_integer("soc", "core", i, "decode_delay", 1, 64) + Config::get_integer("soc", "core", i, "rename_delay", 1, 8),
        Config::get_integer("soc", "core", i, "fetch_width", 1, 64), Config::get_integer("soc", "core", i, "ftq_size", 1, 64))
    , instQueue(Config::get_integer("soc", "core", i, "decode_bucket_size", 1, 128)) {}

PipeQueue::~PipeQueue() {
  // do nothing
}

IBucket* Pipeline::newItem() {
  if (nIRequests == 0) {
    //printf("Pipeline::Newitem:: No new item:: nIRequests==0 return FALSE::at @clockcycle %lu\n", globalClock);
    return 0;
  }
  if (bucketPool.empty()) {
    //printf("Pipeline::Newitem:: No new item ::bucketPool.empty())::return FALSE::at @clockcycle %lu\n", globalClock);
    return 0;
  }

  nIRequests--;

  IBucket* b = bucketPool.back();
  //printf("Pipeline::NewItem():: Before bucketPool Size is %ld and bucketPoolMaxSize is %ld\n",
          //bucketPool.size(),
         //bucketPoolMaxSize);
  bucketPool.pop_back();
  //printf("Pipeline::doneItem:: After bucketPool-- Size is %ld and bucketPoolMaxSize is %ld\n",
         //bucketPool.size(),
         //bucketPoolMaxSize);

  b->setPipelineId(maxItemCntr);

  //printf("Pipeline::Newitem:: new item ::at bucket->PipelineId is %lu at @clockcycle %lu\n", maxItemCntr, globalClock);
  maxItemCntr++;

#ifndef NDEBUG
  b->fetched = false;
  I(b->empty());
#endif

  return b;
}

bool Pipeline::hasOutstandingItems() const { return !buffer.empty() || !received.empty() || nIRequests < MaxIRequests; }
