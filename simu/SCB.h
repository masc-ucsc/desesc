// See LICENSE for details.

#pragma once

#include <deque>
#include <map>
#include <set>
#include <vector>

#include "Resource.h"
#include "SCBEntryType.h"
#include "estl.h"

#include "callback.hpp"
#include "dinst.hpp"

class MemObj;
class SCB {
public:
  int32_t            freeEntries;
  MemObj*            firstLevelMemObj;
  std::deque<Dinst*> replacementDinstQueue;
  SCBEntryType*      scbEntry;
  int                bsize;
  int                lineSizeBits;
  SCB(int scbSize_);
  ~SCB() {}

  bool                                                           canAccept(Dinst* dinst);
  void                                                           tryAddInst(MemObj* firstLevelMemObject, Dinst* dinst);
  void                                                           addInst(Dinst* dinst);
  bool                                                           findTag(Addr_t tag);
  void                                                           performedOwnership(Dinst* dinst);
  void                                                           performedWriteback(Dinst* dinst);
  void                                                           doReplacement(Dinst* dinst);
  bool                                                           isWordBytesHit(Dinst* dinst);
  bool                                                           isTagHit(Dinst* dinst);
  bool                                                           isStateModified(Dinst* dinst);
  void                                                           setWordBytesPresentinTagHit(Dinst* dinst);
  void                                                           issueWriteReqforOwnership(Dinst* dinst);
  typedef CallbackMember1<SCB, Dinst*, &SCB::performedOwnership> ownershipCB;
  typedef CallbackMember1<SCB, Dinst*, &SCB::performedWriteback> writebackCB;
  typedef HASH_MAP<Addr_t, SCBEntryType*>                        SCBinstMap;
  SCBinstMap                                                     instMap;

  void incFreeEntries() { freeEntries++; }
  void decFreeEntries() {
    freeEntries--;
    if (freeEntries < 0)
      freeEntries = 0;
  }
  bool hasFreeEntries() const {
    if (freeEntries > 0)
      return true;
    else
      return false;
  }
  void    setFirstLevelMemObj(MemObj* firstLevelMemObject) { firstLevelMemObj = firstLevelMemObject; }
  MemObj* getFirstLevelMemObj() { return firstLevelMemObj; }
};
