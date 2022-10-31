// See LICENSE for details.

#pragma once

#include <map>
#include <set>
#include <vector>
#include <deque>

#include "dinst.hpp"
#include "callback.hpp"

#include "GStats.h"
#include "estl.h"
#include "SCBEntryType.h"
#include "Resource.h"

class MemObj;
class SCB {
public:
  int32_t              freeEntries;
  MemObj*              firstLevelMemObj;  
  std::deque< DInst* > replacementDInstQueue;
  SCBEntryType*        scbEntry; 
  int                  bsize;
  int                  lineSizeBits;
  SCB( int scbSize_);
  ~SCB() {
  }

  bool   canAccept(DInst* dinst); 
  void   tryAddInst(MemObj *firstLevelMemObject,DInst* dinst);
  void   addInst( DInst* dinst);
  bool   findTag(AddrType tag);
  void   performedOwnership( DInst* dinst);
  void   performedWriteback(DInst* dinst);
  void   doReplacement(DInst* dinst);
  bool   isWordBytesHit(DInst* dinst);
  bool   isTagHit(DInst* dinst);
  bool   isStateModified(DInst* dinst);
  void   setWordBytesPresentinTagHit(DInst* dinst);
  void   issueWriteReqforOwnership(DInst* dinst);
  typedef CallbackMember1<SCB, DInst *,&SCB::performedOwnership> ownershipCB;
  typedef CallbackMember1<SCB, DInst *,&SCB::performedWriteback> writebackCB;
  typedef HASH_MAP<AddrType, SCBEntryType*> SCBinstMap ;
  SCBinstMap           instMap;

  void incFreeEntries() {
    freeEntries++;
  }
  void decFreeEntries() {
    freeEntries--;
    if(freeEntries<0)
      freeEntries=0;
  }
  bool hasFreeEntries() const {
    if(freeEntries > 0)
      return true;
    else 
      return false;
  }
  void setFirstLevelMemObj(MemObj *firstLevelMemObject){
    firstLevelMemObj=firstLevelMemObject;
  }
  MemObj* getFirstLevelMemObj(){
    return firstLevelMemObj;
  }
};

