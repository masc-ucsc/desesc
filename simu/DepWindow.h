// See LICENSE for details.

#pragma once

#include "Port.h"
#include "Resource.h"
#include "iassert.hpp"

class Dinst;
class Cluster;

class DepWindow {
private:
  Cluster *srcCluster;

  const int32_t Id;

  TimeDelta_t InterClusterLat;
  TimeDelta_t SchedDelay;

  GStatsCntr wrForwardBus;

  PortGeneric *schedPort;

protected:
  void preSelect(Dinst *dinst);

public:
  ~DepWindow();
  DepWindow(uint32_t cpuid, Cluster *aCluster, const char *clusterName, uint32_t pos);

  void select(Dinst *dinst);

  StallCause canIssue(Dinst *dinst) const;
  void       addInst(Dinst *dinst);
  void       executed(Dinst *dinst);
};
