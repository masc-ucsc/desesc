// See LICENSE for details.

#pragma once

#include <vector>

#include "dinst.hpp"
#include "resource.hpp"

using ResourcesPoolType = Opcode_array<std::vector<std::shared_ptr<Resource>>>;

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
  Opcode_array<uint32_t> nres;
  Opcode_array<uint32_t> pos;

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
  Opcode_array<uint32_t>                  nres;
  Opcode_array<uint32_t>                  pos;
  RegType_array<std::shared_ptr<Cluster>> cused;

public:
  UseClusterScheduler(const ResourcesPoolType &res);
  ~UseClusterScheduler();

  std::shared_ptr<Resource> getResource(Dinst *dinst);
};
