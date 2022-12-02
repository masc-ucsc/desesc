// See LICENSE for details.

#pragma once

#include "callback.hpp"
#include "execute_engine.hpp"
#include "iassert.hpp"
#include "stats.hpp"

class AccProcessor : public Execute_engine {
private:
protected:
  Addr_t myAddr;
  Addr_t addrIncr;
  int    reqid;
  int    total_accesses;
  int    outstanding_accesses;

  Stats_cntr accReads;
  Stats_cntr accWrites;

  Stats_avg accReadLatency;
  Stats_avg accWriteLatency;

  void read_performed(uint32_t id, Time_t startTime);
  void write_performed(uint32_t id, Time_t startTime);
  typedef CallbackMember2<AccProcessor, uint32_t, Time_t, &AccProcessor::read_performed>  read_performedCB;
  typedef CallbackMember2<AccProcessor, uint32_t, Time_t, &AccProcessor::write_performed> write_performedCB;

public:
  AccProcessor(GMemorySystem *gm, Hartid_t i);
  virtual ~AccProcessor();

  // API for Execute_egine
  bool        advance_clock_drain() override final;
  bool        advance_clock() override final;
  std::string get_type() const override final { return "accel"; }
};
