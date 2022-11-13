// See LICENSE for details.

#pragma once

#include "Resource.h"

#include "stats.hpp"
#include "port.hpp"
#include "iassert.hpp"

class Dinst;
class Cluster;

class DepWindow {
private:
  Cluster *srcCluster;

  TimeDelta_t inter_cluster_lat;
  TimeDelta_t sched_lat;

  Stats_cntr inter_cluster_fwd;

  PortGeneric *schedPort;

protected:
  void preSelect(Dinst *dinst);

public:
  ~DepWindow();
  DepWindow(uint32_t cpuid, Cluster *aCluster, const std::string &clusterName, uint32_t pos);

  void select(Dinst *dinst);

  StallCause canIssue(Dinst *dinst) const;
  void       add_inst(Dinst *dinst);
  void       executed(Dinst *dinst);
};
