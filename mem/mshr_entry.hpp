// See LICENSE for details

#pragma once

#include "callback.hpp"
#include "dinst.hpp"

class MSHRentry {
private:
  CallbackContainer cc;
  int32_t           nFreeSEntries;

public:
  MSHRentry() { nFreeSEntries = 0; }

  ~MSHRentry() {}

  void setup(int32_t size) { nFreeSEntries = size; }

  bool addEntry(CallbackBase* rcb) {
    if (nFreeSEntries == 0) {
      return false;
    }

    nFreeSEntries--;
    cc.add(rcb);
    return true;
  }

  bool canAccept() const {
    if (nFreeSEntries == 0) {
      return false;
    }

    return true;
  }

  bool retire() {
    if (cc.empty()) {
      return true;
    }

    cc.callNext();
    return false;
  }
};
