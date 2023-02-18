// See LICENSE for details.

#pragma once

#include <vector>

#include "dinst.hpp"
#include "resource.hpp"

using ResourcesPoolType = std::array<std::vector<std::shared_ptr<Resource>>, iMAX>;

class ClusterScheduler {
private:
protected:
  ResourcesPoolType res;

public:
  ClusterScheduler(const ResourcesPoolType &ores);
  virtual ~ClusterScheduler();

  virtual std::shared_ptr<Resource> getResource(Dinst *dinst) = 0;
};

class RoundRobinClusterScheduler : public ClusterScheduler {
private:
  std::vector<unsigned int> nres;
  std::vector<unsigned int> pos;

public:
  RoundRobinClusterScheduler(const ResourcesPoolType &res);
  ~RoundRobinClusterScheduler();

  std::shared_ptr<Resource> getResource(Dinst *dinst);
};

class LRUClusterScheduler : public ClusterScheduler {
private:
public:
  LRUClusterScheduler(const ResourcesPoolType &res);
  ~LRUClusterScheduler();

  std::shared_ptr<Resource> getResource(Dinst *dinst);
};

class UseClusterScheduler : public ClusterScheduler {
private:
  std::vector<unsigned int>                      nres;
  std::vector<unsigned int>                      pos;
  std::array<std::shared_ptr<Cluster>, LREG_MAX> cused;

public:
  UseClusterScheduler(const ResourcesPoolType &res);
  ~UseClusterScheduler();

  std::shared_ptr<Resource> getResource(Dinst *dinst);
};
