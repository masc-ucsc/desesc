// See LICENSE for details

#include <cmath>
#include <iostream>
#include <queue>
#include <vector>

#include "mem_controller.hpp"
#include "memory_system.hpp"
#include "config.hpp"

MemController::MemController(MemorySystem *current, const std::string &section, const std::string &name)
    /* constructor {{{1 */
    : MemObj(section, name)
    , delay(Config::get_integer(section, "delay",1,1024))
    , PreChargeLatency(Config::get_integer(section, "PreChargeLatency",1, 1024))
    , RowAccessLatency(Config::get_integer(section, "RowAccessLatency",1, 1024))
    , ColumnAccessLatency(Config::get_integer(section, "ColumnAccessLatency",4, 1024))
    , nPrecharge(fmt::format("{}:nPrecharge", name))
    , nColumnAccess(fmt::format("{}:nColumnAccess", name))
    , nRowAccess(fmt::format("{}:nRowAccess", name))
    , avgMemLat(fmt::format("{}_avgMemLat", name))
    , readHit(fmt::format("{}:readHit", name))
    , memRequestBufferSize(Config::get_integer(section, "memRequestBufferSize",1, 1024)) {
  MemObj *lower_level = NULL;

  NumUnits_t  num = Config::get_integer(section, "port_num");
  TimeDelta_t occ = Config::get_integer(section, "port_occ");

  cmdPort = PortGeneric::create(name + "_cmd", num, occ);

  numBanks                = Config::get_power2(section, "NumBanks");
  unsigned int numRows    = Config::get_power2(section, "NumRows");
  unsigned int ColumnSize = Config::get_power2(section, "ColumnSize");
  unsigned int numColumns = Config::get_power2(section, "NumColumns");

  columnOffset = log2(ColumnSize);
  columnMask   = numColumns - 1;
  columnMask   = columnMask << columnOffset;  // FIXME: Use Addr_t

  rowOffset = columnOffset + log2(numColumns);
  rowMask   = numRows - 1;
  rowMask   = rowMask << rowOffset;  // FIXME: use Addr_t

  bankOffset = rowOffset + log2(numRows);
  bankMask   = numBanks - 1;
  bankMask   = bankMask << bankOffset;

  bankState = new BankStatus[numBanks];
  for (uint32_t curBank = 0; curBank < numBanks; curBank++) {
    bankState[curBank].activeRow = 0;
    bankState[curBank].state     = INIT;  // Changed from ACTIVE (LNB)
    bankState[curBank].bankTime  = 0;     // added (LNB)
  }
  I(current);
  lower_level = current->declareMemoryObj(section, "lower_level");
  if (lower_level) {
    addLowerLevel(lower_level);
  }
}
/* }}} */

void MemController::doReq(MemRequest *mreq)
/* request reaches the memory controller {{{1 */
{
  readHit.inc(mreq->has_stats());
  addMemRequest(mreq);
}
/* }}} */

void MemController::doReqAck(MemRequest *mreq) {
  (void)mreq;
  I(0);
}

void MemController::doDisp(MemRequest *mreq) {
  addMemRequest(mreq);
}

void MemController::doSetState(MemRequest *mreq) {
  (void)mreq;
  I(0);
}

void MemController::doSetStateAck(MemRequest *mreq) {
  (void)mreq;
}

bool MemController::isBusy(Addr_t addr) const {
  (void)addr;
  return false;
}

void MemController::tryPrefetch(Addr_t addr, bool doStats, int degree, Addr_t pref_sign, Addr_t pc, CallbackBase *cb)
{
  (void)addr;
  (void)doStats;
  (void)degree;
  (void)pref_sign;
  (void)pc;
  if (cb) {
    cb->destroy();
  }
  // FIXME:
}

TimeDelta_t MemController::ffread(Addr_t addr) {
  (void)addr;
  return delay + RowAccessLatency;
}

TimeDelta_t MemController::ffwrite(Addr_t addr) {
  (void)addr;
  return delay + RowAccessLatency;
}

void MemController::addMemRequest(MemRequest *mreq) {
  I(0); // really, a new?? FIXME:
  FCFSField *newEntry = new FCFSField;

  newEntry->Bank        = getBank(mreq);
  newEntry->Row         = getRow(mreq);
  newEntry->Column      = getColumn(mreq);
  newEntry->mreq        = mreq;
  newEntry->TimeEntered = globalClock;

  OverflowMemoryRequests.push(newEntry);

  manageRam();
}

// This function implements the FR-FCFS memory scheduling algorithm
void MemController::manageRam(void) {
  // First, we need to determine if any actions (precharging, activating, or accessing) have been completed
  for (uint32_t curBank = 0; curBank < numBanks; curBank++) {
    if ((bankState[curBank].state == PRECHARGE) && (globalClock - bankState[curBank].bankTime >= PreChargeLatency)) {
      bankState[curBank].state = IDLE;

    } else if ((bankState[curBank].state == ACTIVATING) && (globalClock - bankState[curBank].bankTime >= RowAccessLatency)) {
      bankState[curBank].state = ACTIVE;

    } else if ((bankState[curBank].state == ACCESSING) && (globalClock - bankState[curBank].bankTime >= ColumnAccessLatency)) {
      bankState[curBank].state = ACTIVE;

      for (FCFSList::iterator it = curMemRequests.begin(); it != curMemRequests.end(); it++) {
        FCFSField *tempMem = *it;

        // If current memory request has completed, finish processing the request by sending the proper ACK
        if ((curBank == tempMem->Bank) && (bankState[curBank].activeRow == tempMem->Row)) {
          I(tempMem->mreq);

          if (tempMem->mreq->isDisp()) {
            tempMem->mreq->ack();  // Fixed doDisp Acknowledge -- LNB 5/28/2014
          } else {
            MemRequest *mreq = tempMem->mreq;
            I(mreq->isReq());

            if (mreq->getAction() == ma_setValid || mreq->getAction() == ma_setExclusive) {
              mreq->convert2ReqAck(ma_setExclusive);
            } else {
              mreq->convert2ReqAck(ma_setDirty);
            }

            Time_t delta = globalClock - tempMem->TimeEntered;

            router->scheduleReqAck(mreq, 1);  //  Fixed doReq acknowledge -- LNB 5/28/2014
            avgMemLat.sample(delta, mreq->has_stats());
          }
          #ifndef NDEBUG
          tempMem->mreq = 0;
          #endif

          curMemRequests.erase(it);

          break;
        }
      }
    }
  }

  // Call function to replace any deleted address with a new one from queue
  transferOverflowMemory();
  // Call function to determine what the next action should begin
  scheduleNextAction();
}

// This function adds any pending references in the queue to the buffer if there is space available
void MemController::transferOverflowMemory(void) {
  while ((curMemRequests.size() <= memRequestBufferSize) && (!OverflowMemoryRequests.empty())) {
    curMemRequests.push_back(OverflowMemoryRequests.front());
    OverflowMemoryRequests.pop();
  }
}

// This function determines what action can be performed next and schedules a callback for when that action completes
void MemController::scheduleNextAction(void) {
  uint32_t oldestReadyColsBank = numBanks + 1;
  uint32_t oldestReadyRowsBank = numBanks + 1;
  uint32_t oldestReadyRow      = 0;
  uint32_t oldestbank          = 0;

  bool oldestColumnFound = false;
  bool oldestRowFound    = false;
  bool oldestBankFound   = false;

  // Go through memory references in buffer to determine what actions are ready to begin
  for (uint32_t curReference = 0; curReference < curMemRequests.size(); curReference++) {
    auto curBank = curMemRequests[curReference]->Bank;
    auto curRow  = curMemRequests[curReference]->Row;
    if (!oldestColumnFound) {
      if ((bankState[curBank].state == ACTIVE) && (bankState[curBank].activeRow == curRow)) {
        bankState[curBank].cpend = true;
        oldestColumnFound        = true;
        //   if(bankState[oldestReadyColsBank].bankTime > bankState[curBank].bankTime){
        oldestReadyColsBank = curBank;
        //   }
      }
    }
    if ((bankState[curBank].state == ACTIVE) && (bankState[curBank].activeRow != curRow)) {
      bankState[curBank].bpend = true;
    }
    if (!oldestRowFound) {
      if (bankState[curBank].state == IDLE) {
        oldestRowFound = true;
        //    if(bankState[oldestReadyRowsBank].bankTime > bankState[curBank].bankTime){
        oldestReadyRow      = curMemRequests[curReference]->Row;
        oldestReadyRowsBank = curBank;
        //    }
      }
    }
    if (bankState[curBank].state == INIT) {
      oldestBankFound = true;
      oldestbank      = curBank;
    }
  }

  //... and determine if a bank has no pending column references
  for (uint32_t curBank = 0; curBank < numBanks; curBank++) {
    if ((bankState[curBank].bpend) && (!bankState[curBank].cpend)) {
      oldestBankFound = true;
      if (bankState[oldestbank].bankTime > bankState[curBank].bankTime) {
        oldestbank = curBank;
      }
    }
    bankState[curBank].bpend = false;
    bankState[curBank].cpend = false;
  }

  // Now determine which of the ready actions should be start and when the callback should occur
  if (oldestBankFound) {
    bankState[oldestbank].state    = PRECHARGE;
    bankState[oldestbank].bankTime = globalClock;

    nPrecharge.inc();

    ManageRamCB::schedule(PreChargeLatency, this);
  } else if (oldestColumnFound) {
    bankState[oldestReadyColsBank].state    = ACCESSING;
    bankState[oldestReadyColsBank].bankTime = globalClock;

    nColumnAccess.inc();

    ManageRamCB::schedule(ColumnAccessLatency, this);
  } else if (oldestRowFound) {
    bankState[oldestReadyRowsBank].state     = ACTIVATING;
    bankState[oldestReadyRowsBank].bankTime  = globalClock;
    bankState[oldestReadyRowsBank].activeRow = oldestReadyRow;

    nRowAccess.inc();

    ManageRamCB::schedule(RowAccessLatency, this);
  }
}

uint32_t MemController::getBank(MemRequest *mreq) const {
  uint32_t bank = (mreq->getAddr() & bankMask) >> bankOffset;
  return bank;
}
uint32_t MemController::getRow(MemRequest *mreq) const {
  uint32_t row = (mreq->getAddr() & rowMask) >> rowOffset;
  return row;
}

uint32_t MemController::getColumn(MemRequest *mreq) const {
  uint32_t column = (mreq->getAddr() & columnMask) >> columnOffset;
  return column;
}
