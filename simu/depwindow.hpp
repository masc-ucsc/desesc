// See LICENSE for details.

#pragma once

#include "iassert.hpp"
#include "port.hpp"
#include "resource.hpp"
#include "stats.hpp"

class Dinst;
class Cluster;

class DepWindow {
private:
  int src_cluster_id;

  TimeDelta_t inter_cluster_lat;
  TimeDelta_t sched_lat;

  Stats_cntr inter_cluster_fwd;

  PortGeneric *schedPort;

protected:
  void preSelect(Dinst *dinst);

public:
  ~DepWindow();
  DepWindow(uint32_t cpuid, int _src_cluster_id, const std::string &clusterName, uint32_t pos);

  void select(Dinst *dinst);

  StallCause canIssue(Dinst *dinst) const;
  void       add_inst(Dinst *dinst);
  void       executed(Dinst *dinst);
  void       executed_flushed(Dinst *dinst);
};
