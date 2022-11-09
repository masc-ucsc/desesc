// See LICENSE for details.

#pragma once

#include <stdio.h>

#include <vector>

#include "Resource.h"

#include "dinst.hpp"

typedef std::vector<std::vector<Resource *>> ResourcesPoolType;

class ClusterScheduler {
private:
protected:
  ResourcesPoolType res;

public:
  ClusterScheduler(const ResourcesPoolType ores);
  virtual ~ClusterScheduler();

  virtual Resource *getResource(Dinst *dinst) = 0;
};

class RoundRobinClusterScheduler : public ClusterScheduler {
private:
  std::vector<unsigned int> nres;
  std::vector<unsigned int> pos;

public:
  RoundRobinClusterScheduler(const ResourcesPoolType res);
  ~RoundRobinClusterScheduler();

  Resource *getResource(Dinst *dinst);
};

class LRUClusterScheduler : public ClusterScheduler {
private:
public:
  LRUClusterScheduler(const ResourcesPoolType res);
  ~LRUClusterScheduler();

  Resource *getResource(Dinst *dinst);
};

class UseClusterScheduler : public ClusterScheduler {
private:
  std::vector<unsigned int> nres;
  std::vector<unsigned int> pos;
  Cluster                  *cused[LREG_MAX];

public:
  UseClusterScheduler(const ResourcesPoolType res);
  ~UseClusterScheduler();

  Resource *getResource(Dinst *dinst);
};
