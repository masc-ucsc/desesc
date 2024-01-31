// See LICENSE for details.

#include "cluster.hpp"

#include "config.hpp"
#include "estl.hpp"
#include "gmemory_system.hpp"
#include "gprocessor.hpp"
#include "port.hpp"
#include "resource.hpp"
#include "store_buffer.hpp"

// Begin: Fields used during constructions

Cluster::~Cluster() {
  // Nothing to do
}

Cluster::Cluster(const std::string &clusterName, uint32_t pos, uint32_t _cpuid)
    : window(_cpuid, cluster_id_counter, clusterName, pos)
    , MaxWinSize(Config::get_integer(clusterName, "win_size", 1, 32768))
    , windowSize(Config::get_integer(clusterName, "win_size"))
    , winNotUsed(fmt::format("P({})_{}{}_winNotUsed", _cpuid, clusterName, pos))
    , rdRegPool(fmt::format("P({})_{}{}_rdRegPool", _cpuid, clusterName, pos))
    , wrRegPool(fmt::format("P({})_{}{}_wrRegPool", _cpuid, clusterName, pos))
    , cpuid(_cpuid) {
  name       = fmt::format("{}{}", clusterName, pos);
  cluster_id = cluster_id_counter++;

  nready = 0;
}

std::shared_ptr<Resource> Cluster::buildUnit(const std::string &clusterName, uint32_t pos, std::shared_ptr<Gmemory_system> ms,
                                             std::shared_ptr<Cluster> cluster, Opcode op, GProcessor *gproc) {
  auto unitType = fmt::format("{}", op);

  if (!Config::has_entry(clusterName, unitType)) {
    return nullptr;
  }
  auto sUnitName = Config::get_string(clusterName, unitType);

  int id      = cpuid;
  int smt     = Config::get_integer("soc", "core", id, "smt", 1, 1024);
  int smt_ctx = id - (id % smt);

  TimeDelta_t  lat = Config::get_integer(sUnitName, "lat", 0, 1024);
  PortGeneric *gen;

  auto unitName = fmt::format("P({})_{}{}_{}", smt_ctx, clusterName, pos, sUnitName);
  auto it       = unitMap.find(unitName);
  if (it != unitMap.end()) {
    gen = it->second.gen;
  } else {
    UnitEntry e;
    e.num = Config::get_integer(sUnitName, "num", 0, 1024);
    e.occ = Config::get_integer(sUnitName, "occ", 0, 1024);

    e.gen = PortGeneric::create(unitName, e.num, e.occ);
    gen   = e.gen;

    unitMap[unitName] = e;
  }

  int unitID = 0;
  switch (op) {
    case Opcode::iBALU_LBRANCH:
    case Opcode::iBALU_LJUMP:
    case Opcode::iBALU_LCALL:
    case Opcode::iBALU_RBRANCH:
    case Opcode::iBALU_RJUMP:
    case Opcode::iBALU_RCALL:
    case Opcode::iBALU_RET: unitID = 1; break;
    case Opcode::iLALU_LD: unitID = 2; break;
    case Opcode::iSALU_LL:
    case Opcode::iSALU_SC:
    case Opcode::iSALU_ST:
    case Opcode::iSALU_ADDR: unitID = 3; break;
    default: unitID = 0; break;
  }

  auto resourceName = fmt::format("P({})_{}{}_{}_{}", smt_ctx, clusterName, pos, unitID, sUnitName);
  auto it2          = resourceMap.find(resourceName);

  std::shared_ptr<Resource> r;

  if (it2 != resourceMap.end()) {
    r = it2->second;
  } else {
    switch (op) {
      case Opcode::iOpInvalid:
      case Opcode::iRALU: r = std::make_shared<FURALU>(op, cluster, gen, lat, cpuid); break;
      case Opcode::iAALU:
      case Opcode::iCALU_FPMULT:
      case Opcode::iCALU_FPDIV:
      case Opcode::iCALU_FPALU:
      case Opcode::iCALU_MULT:
      case Opcode::iCALU_DIV: r = std::make_shared<FUGeneric>(op, cluster, gen, lat, cpuid); break;
      case Opcode::iBALU_LBRANCH:
      case Opcode::iBALU_LJUMP:
      case Opcode::iBALU_LCALL:
      case Opcode::iBALU_RBRANCH:
      case Opcode::iBALU_RJUMP:
      case Opcode::iBALU_RCALL:
      case Opcode::iBALU_RET: {
        auto max_branches = Config::get_integer("soc", "core", cpuid, "max_branches");
        if (max_branches == 0) {
          max_branches = INT_MAX;
        }
        bool drain_on_miss = Config::get_bool("soc", "core", cpuid, "drain_on_miss");

        r = std::make_shared<FUBranch>(op, cluster, gen, lat, cpuid, max_branches, drain_on_miss);
      } break;
      case Opcode::iLALU_LD: {
        TimeDelta_t st_fwd_delay = Config::get_integer("soc", "core", cpuid, "st_fwd_delay");
        int32_t     ldq_size     = Config::get_integer("soc", "core", cpuid, "ldq_size", 0, 256 * 1024);
        if (ldq_size == 0) {
          ldq_size = 256 * 1024;
        }

        r = std::make_shared<FULoad>(op,
                                     cluster,
                                     gen,
                                     gproc->getLSQ(),
                                     gproc->ref_SS(),
                                     gproc->ref_prefetcher(),
                                     gproc->ref_SCB(),
                                     st_fwd_delay,
                                     lat,
                                     ms,
                                     ldq_size,
                                     cpuid,
                                     "specld");
      } break;
      case Opcode::iSALU_LL:
      case Opcode::iSALU_SC:
      case Opcode::iSALU_ST:
      case Opcode::iSALU_ADDR: {
        int32_t stq_size = Config::get_integer("soc", "core", cpuid, "stq_size", 0, 256 * 1024);
        if (stq_size == 0) {
          stq_size = 256 * 1024;
        }

        r = std::make_shared<FUStore>(op,
                                      cluster,
                                      gen,
                                      gproc->getLSQ(),
                                      gproc->ref_SS(),
                                      gproc->ref_prefetcher(),
                                      gproc->ref_SCB(),
                                      lat,
                                      ms,
                                      stq_size,
                                      cpuid,
                                      fmt::format("{}", op));
      } break;
      default: Config::add_error(fmt::format("unknown unit type {}", op)); I(0);
    }
    I(r);
    resourceMap[resourceName] = r;
  }

  return r;
}

std::pair<std::shared_ptr<Cluster>, Opcode_array<std::shared_ptr<Resource>>> Cluster::create(const std::string &clusterName,
                                                                                             uint32_t           pos,
                                                                                             std::shared_ptr<Gmemory_system> ms,
                                                                                             uint32_t cpuid, GProcessor *gproc) {
  // Constraints
  Opcode_array<std::shared_ptr<Resource>> res;

  auto recycleAt = Config::get_string(clusterName, "recycle_at", {"executing", "executed", "retired"});

  int smt     = Config::get_integer("soc", "core", cpuid, "smt");
  int smt_ctx = cpuid - (cpuid % smt);

  auto cName = fmt::format("cluster({})_{}{}", smt_ctx, clusterName, pos);

  std::shared_ptr<Cluster> cluster;

  const auto it = clusterMap.find(cName);
  if (it != clusterMap.end()) {
    return it->second;
  }

  if (recycleAt == "retire") {
    cluster = std::make_shared<RetiredCluster>(clusterName, pos, cpuid);
  } else if (recycleAt == "executing") {
    cluster = std::make_shared<ExecutingCluster>(clusterName, pos, cpuid);
  } else {
    I(recycleAt == "executed");
    cluster = std::make_shared<ExecutedCluster>(clusterName, pos, cpuid);
  }

  cluster->nRegs     = Config::get_integer(clusterName, "num_regs", 2, 262144);
  cluster->regPool   = cluster->nRegs;
  cluster->lateAlloc = Config::get_bool(clusterName, "late_alloc");

  for (const auto t : Opcodes) {
    auto r = cluster->buildUnit(clusterName, pos, ms, cluster, t, gproc);
    res[t] = r;
  }

  clusterMap[cName] = std::pair(cluster, res);

  return std::pair(cluster, res);
}

void Cluster::select(Dinst *dinst) {
  I(nready >= 0);
  nready++;
  window.select(dinst);
}

StallCause Cluster::canIssue(Dinst *dinst) const {
  printf("Cluster::can issue Entering dinstID %ld\n", dinst->getID());
  if (regPool <= 0) {
    printf("Cluster::can issue reg stall:regPool <0 dinstID %ld\n", dinst->getID());
    return SmallREGStall;
  }

  if (windowSize <= 0) {
    printf("Cluster::can issue window size stall windowsize <0 dinstID %ld and windowsize<0  is %d\n", dinst->getID(), windowSize);
    return SmallWinStall;
  }

  StallCause sc = window.canIssue(dinst);
  if (sc != NoStall) {
    printf("Cluster::canissue window cannot isuue stall dinstID %ld\n", dinst->getID());
    return sc;
  }

  printf("Cluster::can issue Leaving dinstID %ld\n", dinst->getID());
  return dinst->getClusterResource()->canIssue(dinst);
}

void Cluster::add_inst(Dinst *dinst) {
  printf("Cluster::add_inst Entering dinstID %ld\n", dinst->getID());
  rdRegPool.add(2, dinst->has_stats());  // 2 reads

  if (!lateAlloc && dinst->getInst()->hasDstRegister()) {
    wrRegPool.inc(dinst->has_stats());
    I(regPool > 0);
    regPool--;
    printf("Cluster::add_inst so regPool-- %d and %d nRegs and Inst id %ld\n",
        regPool, nRegs,dinst->getID()) ;  

  }
  // dinst->dump("add");

  newEntry();

  window.add_inst(dinst);
  if(!dinst->is_in_cluster()) {
    window.add_inst(dinst);
  }

  printf("Cluster::add_inst leaving dinstID %ld\n", dinst->getID());
}

//************ Executing Cluster Class

void ExecutingCluster::executing(Dinst *dinst) {
  printf("ClusterExecuting ::executing Entering Insit %ld\n",dinst->getID());
  nready--;

  if (lateAlloc && dinst->getInst()->hasDstRegister()) {
    wrRegPool.inc(dinst->has_stats());
    I(regPool > 0);
    regPool--;
    printf("ExecutingCluster::executing added so regPool-- %d and %d nRegs and Inst id %ld\n",
        regPool, nRegs,dinst->getID()) ;  
  }
  printf("ClusterExecuting ::executing Insit %ld\n",dinst->getID());
  dinst->getGProc()->executing(dinst);

  printf("ClusterExecuting ::executing delete Entry Insit %ld\n",dinst->getID());
  delEntry();
  printf("ClusterExecuting ::executing Leaving Insit %ld\n",dinst->getID());
}
void ExecutingCluster::flushed(Dinst *dinst) {
  nready--;

  if (lateAlloc && dinst->getInst()->hasDstRegister()) {
    wrRegPool.inc(dinst->has_stats());
    I(regPool > 0);
    regPool--;
    printf("Cluster::executingCluster added so regPool-- %d and %d nRegs and Inst id %ld\n",
        regPool, nRegs,dinst->getID()) ;  
  }
  printf("ClusterExecuting ::executing Insit %ld\n",dinst->getID());
  dinst->getGProc()->flushed(dinst);

  printf("ClusterExecuting ::flushed delete Entry Insit %ld\n",dinst->getID());
  //delEntry();
}


void ExecutingCluster::executed(Dinst *dinst) {
  printf("ClusterExecuting ::executed  Entering Insit %ld\n",dinst->getID());
  window.executed(dinst);
  printf("ClusterExecuting ::executed Insit %ld\n",dinst->getID());
  dinst->getGProc()->executed(dinst);
  printf("ClusterExecuting ::executed  Leaving Insit %ld\n",dinst->getID());
}

bool ExecutingCluster::retire(Dinst *dinst, bool reply) {
  printf("ClusterExecuting ::retire  Entering  Insit %ld\n",dinst->getID());
  bool done = dinst->getClusterResource()->retire(dinst, reply);

  printf("ClusterExecuting ::retire Inst %ld\n",dinst->getID());
  if (!done) {
    printf("ClusterExecuting ::retire Inst getClusterResource()->retire(dinst) !done : FALSE not RETURN %ld\n",dinst->getID());
    return false;
  }

  bool hasDest = (dinst->getInst()->hasDstRegister());

  if (hasDest) {
    regPool++;
    printf("Cluster::ExecutingCluster retire :: regpool++ Inst reg Pool %d  %ld\n",regPool,  dinst->getID());
    I(regPool <= nRegs);
  }

  winNotUsed.sample(windowSize, dinst->has_stats());
  printf("ClusterExecuting ::retire Inst getClusterResource()->retire(dinst) TRUE RETURN %ld\n",dinst->getID());

  return true;
}

//************ Executed Cluster Class
//Only this Executed cluster is used in desesc now
void ExecutedCluster::executing(Dinst *dinst) {
  //if(!dinst->is_in_cluster() && !dinst->isIssued()) {
    //window.add_inst(dinst);
  //lima}

  printf("ClusterExecuted::executing Entering Insit %ld\n",dinst->getID());
  nready--;

  if (lateAlloc && dinst->getInst()->hasDstRegister()) {
    wrRegPool.inc(dinst->has_stats());
    I(regPool > 0);
    regPool--;
    printf("ExecutedCluster::executing  added so regPool-- %d and %d nRegs and Inst id %ld\n",
        regPool, nRegs,dinst->getID()) ;  
  }
  printf("ClusterExecuted ::executing Insit %ld\n",dinst->getID());
  dinst->getGProc()->executing(dinst);
  printf("ClusterExecuted::executing Leaving Insit %ld\n",dinst->getID());
}

void ExecutedCluster::executed(Dinst *dinst) {
  printf("ClusterExecuted::executed Entering Insit %ld\n",dinst->getID());
  window.executed(dinst);
  dinst->getGProc()->executed(dinst);
  if(!dinst->isTransient())
    I(!dinst->hasPending());
  printf("ClusterExecuted ::executed delete Entry Insit %ld\n",dinst->getID());
  //if(
  dinst->mark_del_entry();
  delEntry();
  printf("ClusterExecuted::executed Leaving Insit %ld\n",dinst->getID());
}
void ExecutedCluster::flushed(Dinst *dinst) {
  window.executed_flushed(dinst);
  printf("ClusterExecuted ::executed Insit %ld\n",dinst->getID());
  dinst->getGProc()->flushed(dinst); 
  //I(!dinst->hasPending());
  printf("ClusterExecuted ::flushed delete Entry Insit %ld\n",dinst->getID());
  //if(Executed cluster ) then delentry else return//TODO
  //delEntry();
}


bool ExecutedCluster::retire(Dinst *dinst, bool reply) {
  printf("ClusterExecuted ::retire  Entering  Insit %ld\n",dinst->getID());
  if(dinst->is_del_entry()) {
    printf("ClusterExecuted ::retire  del_entry Entering  Insit %ld\n",dinst->getID());
    //delEntry();
    printf("ClusterExecuted ::retire  del_entry Leaving  Insit %ld\n",dinst->getID());
    dinst->unmark_del_entry();
    printf("ClusterExecuted ::retire  del_entry Insit %ld\n",dinst->getID());
  }
  //always return true from resource::retire()
  bool done = dinst->getClusterResource()->retire(dinst, reply);
  printf("ClusterExecuted ::retire Insit %ld\n",dinst->getID());
  if (!done) {
    printf("ClusterExecuted ::retire  return false : !done resource::retire Insit %ld\n",dinst->getID());
    return false;
  }
  if(regPool >= nRegs){
    printf("ClusterExecuted ::retire :regPool > nregs is %d Insit %ld\n",regPool, dinst->getID());
    //return false;
  }
  /*if(windowSize >= MaxWinSize){
    printf("ClusterExecuted ::retire  return false: Window Size is %d Inst %ld\n",windowSize, dinst->getID());
    return false;
  }*/
  //delEntry();
  bool hasDest = (dinst->getInst()->hasDstRegister());
  if (hasDest) {
    printf("ExecutedCluster::retire ::regPool:: Inst regPool %d  nd nRegs is %d and Inst is %ld\n",
        regPool, nRegs, dinst->getID());
    regPool++;
    //if(!dinst->is_present_rrob()){
      //regPool++;
    //}
    printf("ExecutedCluster::retire ::regPool++:: Inst regPool %d  nd nRegs is %d and Inst is %ld\n",
        regPool, nRegs, dinst->getID());
    I(regPool <= nRegs);
  }
  // dinst->dump("ret");

  //delEntry();
  winNotUsed.sample(windowSize, dinst->has_stats());

  printf("ClusterExecuted ::retire  return TRUE Insit %ld\n",dinst->getID());
  return true;
}

//************ RetiredCluster Class

void RetiredCluster::executing(Dinst *dinst) {
  printf("ClusterRetired ::executing Entering Insit %ld\n",dinst->getID());
  nready--;

  if (lateAlloc && dinst->getInst()->hasDstRegister()) {
    wrRegPool.inc(dinst->has_stats());
    regPool--;
    printf("RetiredCluster::executing added so  regPool-- %d and %d nRegs and Inst id %ld\n",
        regPool, nRegs,dinst->getID()) ;  
  }
  printf("ClusterRetired ::executing  Insit %ld\n",dinst->getID());
  dinst->getGProc()->executing(dinst);
  printf("ClusterRetired ::executing Leaving Insit %ld\n",dinst->getID());
}

void RetiredCluster::executed(Dinst *dinst) {
  printf("ClusterRetired ::executed Entering Insit %ld\n",dinst->getID());
  window.executed(dinst);
  printf("ClusterRetired ::executed Insit %ld\n",dinst->getID());
  dinst->getGProc()->executed(dinst);
  printf("ClusterRetired ::executed Leaving Insit %ld\n",dinst->getID());
}

bool RetiredCluster::retire(Dinst *dinst, bool reply) {
  printf("ClusterRetired ::retire Entering Insit %ld\n",dinst->getID());
  bool done = dinst->getClusterResource()->retire(dinst, reply);
  if (!done) {
  printf("ClusterREtired ::retire  return false Insit %ld\n",dinst->getID());
    return false;
  }

  I(dinst->getGProc()->get_hid() == dinst->getFlowId());

  bool hasDest = (dinst->getInst()->hasDstRegister());

  if (hasDest) {
    regPool++;
  printf("Cluster::Retired  retire :: regPool++: Inst reg Pool %d and nRegs is %d and Inst is  %ld\n",
      regPool,nRegs , dinst->getID());
    I(regPool <= nRegs);
  }

  winNotUsed.sample(windowSize, dinst->has_stats());

  printf("ClusterRetired ::retire delete Entry Insit %ld\n",dinst->getID());
  delEntry();
  printf("ClusterREtired ::retire  return TRUE Insit %ld\n",dinst->getID());

  return true;
}
void RetiredCluster::flushed(Dinst *dinst) {
  bool done = dinst->getClusterResource()->flushed(dinst);
  if (!done) {
    //return false;
  }

  I(dinst->getGProc()->get_hid() == dinst->getFlowId());

  bool hasDest = (dinst->getInst()->hasDstRegister());

  if (hasDest) {
    regPool++;
    printf("Cluster::Retired  retire :: regPool++: Inst reg Pool %d  %ld\n",regPool,  dinst->getID());
    I(regPool <= nRegs);
  }

  winNotUsed.sample(windowSize, dinst->has_stats());

  printf("ClusterRetired ::retire delete Entry Insit %ld\n",dinst->getID());
 // delEntry();

  //return true;
}
