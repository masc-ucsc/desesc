
#pragma once

#include "iassert.hpp"

#include "GProcessor.h"

class GOoOProcessor : public GProcessor {
private:
protected:
  // BEGIN VIRTUAL FUNCTIONS of GProcessor

  //  virtual void fetch(EmulInterface *eint, FlowID fid);
  //  virtual bool advance_clock();
  //  virtual StallCause addInst(DInst *dinst);
  //  virtual void retire();

  // END VIRTUAL FUNCTIONS of GProcessor
public:
  GOoOProcessor(GMemorySystem *gm, CPU_t i);
  virtual ~GOoOProcessor() {
  }
  //  virtual LSQ *getLSQ();
};

