// See LICENSE for details.

#include "dinst.hpp"

class SCBEntryType {
public:
  enum StateType { U, M };
  std::vector<int> wordBytePresent;
  StateType        state;
  int              validBit;
  Dinst*           dinst;
  int              isPendingOwnership;

  SCBEntryType() { wordBytePresent.assign(64, 0); }
  void      setLineStatetoM() { state = M; }
  void      setLineStatetoUncoherent() { state = U; }
  StateType getLineState() { return state; }
  void      resetLineValidBit() { validBit = 0; }

  int getWordBytePresentSize() { return wordBytePresent.size(); }

  void setWordBytePresent(Addr_t tag) {
    int pos = (int)tag % 64;

    for (int i = pos; i < pos + 8; i++) {
      wordBytePresent[i] = 1;
    }
  }
  void resetWordBytePresent(Addr_t tag) {
    int pos = (int)tag % 64;
    for (int i = pos; i < pos + 8; i++) {
      wordBytePresent[i] = 0;
    }
  }

  bool isWordBytesHit(Addr_t tag) {
    int pos = (int)tag % 64;
    for (int i = pos; i < pos + 8; i++) {
      if (wordBytePresent[i] == 1) {
      } else {
        return false;
      }
    }  // for
    printf("Finally HIT tag %lld\n", tag);
    return true;
  }

  int getLineValidBit() { return validBit; }
};
