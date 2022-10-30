
/*
   ESESC: Super ESCalar simulator
   Copyright (C) 2010 University of California, Santa Cruz.
This file is part of ESESC.
ESESC is free software; you can redistribute it and/or modify it under the terms
of the GNU General Public License as published by the Free Software Foundation;
either version 2, or (at your option) any later version.
ESESC is distributed in the  hope that it will be useful, but WITHOUT ANY
WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A
PARTICULAR PURPOSE. See the GNU General Public License for more details.
You should have received a copy of the GNU General Public License along with
ESESC; see the file COPYING. If not, write to the Free Software Foundation, 59
Temple Place - Suite 330, Boston, MA 02111-1307, USA.
*/

#ifndef SCB_H
#define SCB_H


#include <map>
#include <set>
#include <vector>
#include <deque>
#include "GStats.h"
#include "estl.h"
#include "SCBEntryType.h"
#include "DInst.h"
#include "callback.h"
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
#endif
