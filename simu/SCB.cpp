// See LICENSE for details.

#include "SCB.h"

#include "GProcessor.h"
#include "MemRequest.h"
#include "Resource.h"
#include "config.hpp"

class SCBEntryType;

SCB::SCB(int scbSize_)

    : freeEntries(scbSize_) {
  scbEntry = new SCBEntryType();
  MSG("SCB constructed !!! YAhoo");
  printf(" scbSize or Free Entries are  %lld \n", scbSize_);
}

bool SCB::canAccept(Dinst* dinst) {
  if (hasFreeEntries()) {
    return true;
  } else {
    return false;
  }
}

void SCB::tryAddInst(MemObj* firstLevelMemObject, Dinst* dinst) {
  if (hasFreeEntries()) {
    addInst(dinst);
  } else {
  }
  setFirstLevelMemObj(firstLevelMemObject);
  MemObj* DL1;
  if (strcasecmp(firstLevelMemObj->getDeviceType(), "cache") == 0) {
    DL1 = firstLevelMemObject;
  }
  const char* dl1_section = DL1->getSection();
  int         bsize       = SescConf->getInt(dl1_section, "bsize");
  lineSizeBits            = log2i(bsize);
}

void SCB::addInst(Dinst* dinst) {
  Addr_t           tag = dinst->getAddr() >> 6;
  int              pos = tag % 64;
  std::vector<int> wordBytePresent;
  wordBytePresent.assign(64, 0);
  for (int i = pos; i < pos + 8; i++) {
    if (i < 64) {
      wordBytePresent[i] = 1;
    }
  }
  scbEntry->wordBytePresent    = wordBytePresent;
  scbEntry->state              = SCBEntryType::StateType::U;
  scbEntry->isPendingOwnership = 0;
  scbEntry->dinst              = dinst;
  instMap.insert(std::pair<Addr_t, SCBEntryType*>(tag, scbEntry));
  decFreeEntries();
  bool found = findTag(tag);
}

bool SCB::isStateModified(Dinst* dinst) {
  HASH_MAP<Addr_t, SCBEntryType*>::iterator InstIt;
  Addr_t                                    tag = dinst->getAddr() >> 6;
  InstIt                                        = instMap.find(tag);
  if (InstIt != instMap.end()) {
    if (InstIt->second->state == SCBEntryType::StateType::M) {
      return true;
    } else {
      return false;
    }
  } else {
    return false;
  }
}

bool SCB::isTagHit(Dinst* dinst) {
  HASH_MAP<Addr_t, SCBEntryType*>::iterator InstIt;
  Addr_t                                    tag = dinst->getAddr() >> 6;
  InstIt                                        = instMap.find(tag);
  if (InstIt != instMap.end()) {
    return true;
  } else {
    return false;
  }
}

bool SCB::isWordBytesHit(Dinst* dinst) {
  HASH_MAP<Addr_t, SCBEntryType*>::iterator InstIt;
  Addr_t                                    tag = dinst->getAddr() >> 6;
  InstIt                                        = instMap.find(tag);
  int pos                                       = tag % 64;
  if (InstIt != instMap.end()) {
    for (int i = pos; i < pos + 8; i++) {
      if (i < 64) {
        if (InstIt->second->wordBytePresent.at(i) == 1) {
        } else {
          return false;
        }
      }
    }
    return true;
  } else {
    return false;
  }
}

void SCB::setWordBytesPresentinTagHit(Dinst* dinst) {
  Addr_t                                    tag = dinst->getAddr() >> 6;
  int                                       pos = tag % 64;
  HASH_MAP<Addr_t, SCBEntryType*>::iterator InstIt;
  InstIt = instMap.find(tag);
  if (InstIt != instMap.end()) {
    for (int i = pos; i < pos + 8; i++) {
      if (i < 64) {
        InstIt->second->wordBytePresent[i] = 1;
      }
    }
  }
}

bool SCB::findTag(Addr_t tag) {
  HASH_MAP<Addr_t, SCBEntryType*>::iterator InstIt;
  InstIt = instMap.find(tag);
  if (InstIt != instMap.end()) {
    return true;
  } else {
    return false;
  }
}

void SCB::issueWriteReqforOwnership(Dinst* dinst) {
  // issue Write Req for ownership after 1st time
  Addr_t                                    tag = dinst->getAddr() >> 6;
  HASH_MAP<Addr_t, SCBEntryType*>::iterator InstIt;
  InstIt = instMap.find(tag);
  if (InstIt != instMap.end()) {
    InstIt->second->isPendingOwnership = 1;
    MemRequest::sendReqWrite(getFirstLevelMemObj(),
                             dinst->getStatsFlag(),
                             tag << 6,
                             dinst->getPC(),
                             ownershipCB::create(this, dinst));
    bool found = findTag(tag);
  } else {
  }
}

void SCB::performedOwnership(Dinst* dinst) {
  // 1.get ack from L1 for M state

  Addr_t                                    tag = dinst->getAddr() >> 6;
  HASH_MAP<Addr_t, SCBEntryType*>::iterator InstIt;
  InstIt = instMap.find(tag);
  if (InstIt != instMap.end()) {
    InstIt->second->state              = SCBEntryType::StateType::M;
    InstIt->second->isPendingOwnership = 0;
    InstIt->second->dinst              = dinst;
    bool found                         = findTag(tag);
    replacementDinstQueue.push_back(dinst);
  } else {
    // printf("SCBPerformed:: Inst not found in SCB for tag %13lld\n",tag);
  }

  if (dinst->isRetired()) {
    dinst->recycle();
  }
  I(!dinst->isPerformed());
  if (dinst->isPerformed() == 1) {
  }
  dinst->markPerformed();
}

void SCB::doReplacement(Dinst* dinst) {
  HASH_MAP<Addr_t, SCBEntryType*>::iterator RIt;
  Addr_t                                    tag = dinst->getAddr() >> 6;
  for (RIt = instMap.begin(); RIt != instMap.end(); ++RIt) {
    if (RIt->second->state == SCBEntryType::StateType::M && RIt->first != tag) {
      Dinst* replacementDinst = RIt->second->dinst;
      Addr_t tagReplacement   = RIt->first;
      MemRequest::sendDirtyDisp(getFirstLevelMemObj(),
                                getFirstLevelMemObj(),
                                tagReplacement << 6,
                                replacementDinst->getStatsFlag(),
                                writebackCB::create(this, replacementDinst));
      instMap.erase(RIt);
      incFreeEntries();
      break;
    } else {
    }
  }
  if (dinst->isRetired()) {
    dinst->recycle();
  }
}

void SCB::performedWriteback(Dinst* dinst) {
  //  printf("ReplacemntQ done\n");
}
