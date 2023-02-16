// See LICENSE for details.

#pragma once

#include <queue>
#include <string>
#include <vector>

#include "mrouter.hpp"
#include "callback.hpp"
#include "dinst.hpp"
#include "iassert.hpp"
#include "port.hpp"

class MemRequest;

#define PSIGN_NONE       0
#define PSIGN_RAS        1
#define PSIGN_NLINE      2
#define PSIGN_STRIDE     3
#define PSIGN_TAGE       4
#define PSIGN_INDIRECT   5
#define PSIGN_CHASE      6
#define PSIGN_MEGA       7
#define LDBUFF_SIZE      512
#define CIR_QUEUE_WINDOW 512  // FIXME: need to change this to a conf variable

#define LOT_QUEUE_SIZE 64  // 512 //FIXME: need to change this to a conf variable
// #define BOT_SIZE 512 //16 //512
// #define LOR_SIZE 512
// #define LOAD_TABLE_SIZE 512 //64 //512
// #define PLQ_SIZE 512 //512
#define LOAD_TABLE_CONF      63
#define LOAD_TABLE_DATA_CONF 63
// #define NUM_FSM_ALU 32
// #define ENABLE_LDBP

class MemObj {
private:
protected:
  friend class MRouter;

  MRouter    *router;
  std::string section;
  std::string name;
  std::string mem_type;

  const uint16_t  id;
  static uint16_t id_counter;
  int16_t         coreid;
  bool            firstLevelIL1;
  bool            firstLevelDL1;
  bool            isLLC;
  void            addLowerLevel(MemObj *obj);
  void            addUpperLevel(MemObj *obj);

public:
  MemObj(const std::string &section, const std::string &sName);
  MemObj();
  virtual ~MemObj();

  bool isLastLevelCache();

#ifdef ENABLE_LDBP

  PortGeneric *fsm_alu_port;

  const int NUM_FSM_ALU;
  const int BOT_SIZE;
  const int LOR_SIZE;
  const int LOAD_TABLE_SIZE;
  const int PLQ_SIZE;
  const int CODE_SLICE_DELAY;

  // NEW INTERFACE !!!!!! Nov 20, 2019
  ////Load Table
  void hit_on_load_table(Dinst *dinst, bool is_li);
  int  return_load_table_index(Addr_t pc);
  int  getLoadTableConf() const { return LOAD_TABLE_CONF; }
  int  getLoadDataConf() const { return LOAD_TABLE_DATA_CONF; }
  // PLQ
  int return_plq_index(Addr_t pc);
  // LOR
  void lor_allocate(Addr_t brpc, Addr_t ld_ptr, Addr_t ld_start, int64_t ld_del, int data_pos, bool is_li);
  // void lor_find_index(MemRequest *mreq);
  void                                                                        lor_find_index(Addr_t mreq_addr);
  void                                                                        lor_trigger_load_complete(Addr_t mreq_addr);
  typedef CallbackMember1<MemObj, Addr_t, &MemObj::lor_trigger_load_complete> lor_trigger_load_completeCB;
  int                                                                         return_lor_index(Addr_t ld_ptr);
  int                                                                         compute_lor_index(Addr_t brpc, Addr_t ld_ptr);
  // LOT
  void lot_fill_data(int lot_index, int lot_queue_index, Addr_t tl_addr);
  bool lot_tl_addr_range(Addr_t tl_addr, Addr_t start_addr, Addr_t end_addr, int64_t delta);
  int  getBotSize() const { return BOT_SIZE; }
  int  getLotQueueSize() const { return LOT_QUEUE_SIZE; }
  // BOT
  int  return_bot_index(Addr_t brpc);
  void bot_allocate(Addr_t brpc, Addr_t ld_ptr, Addr_t ld_ptr_addr);

  struct load_table {  // store stride pref info
    // fields: LDPC, last_addr, delta, conf
    load_table() {
      ldpc         = 0;
      ld_addr      = 0;
      prev_ld_addr = 0;
      delta        = 0;
      prev_delta   = 0;
      conf         = 0;
      use_slice    = 0;
      tracking     = 0;
      is_li        = false;
      chain_set    = false;
      chain_parent = false;
      chain_child  = false;
      // chain_parent_ptr.clear();
      chain_child_ptr = 0;
      data_delta      = 0;
      prev_data_delta = 0;
      data_conf       = 0;
      for (int i = 0; i < 16; i++) {
        chain_parent_ptr.push_back(0);
      }
    }
    Addr_t  ldpc;
    Addr_t  ld_addr;
    Addr_t  prev_ld_addr;
    int64_t delta;
    int64_t prev_delta;
    int     conf;
    bool    is_li;
    int     use_slice;  // set to 1 when Ld retires, reset to 0 when Br retires
    //-> if 1, indicates Br went through Ld else Br didn't use LD(and we don't have to trigger LD)
    int tracking;  // 0 to 3 -> useful counter
    // ld-ld chain params
    bool chain_set;  // flag to avoid overwriting of ld-ld chain fields; if true
    // the above flag is false if LD is not a chain child, else true
    bool                chain_child;   // child LD in a ld-ld chain
    bool                chain_parent;  // parent LD in ld-ld chain
    std::vector<Addr_t> chain_parent_ptr
        = std::vector<Addr_t>(16);  // pointer to chain parent -> says which LD is this entry's parent
    Addr_t chain_child_ptr;         // pointer to chain child -> says which LD is this entry's child
    // LD data stats - ESESC only params
    Data_t ld_data;
    Data_t prev_ld_data;
    Data_t data_delta;
    Data_t prev_data_delta;
    int    data_conf;

    void clear_chain_parent_ptr() {
      chain_parent_ptr.clear();
      for (int i = 0; i < 16; i++) {
        chain_parent_ptr.push_back(0);
      }
    }

    void lt_load_miss(Dinst *dinst) {
      load_table();
      ldpc    = dinst->getPC();
      ld_addr = dinst->getAddr();
      ld_data = dinst->getData();
    }

    void lt_load_hit(Dinst *dinst) {
      ldpc         = dinst->getPC();
      use_slice    = 1;
      prev_delta   = delta;
      prev_ld_addr = ld_addr;
      ld_addr      = dinst->getAddr();
      delta        = ld_addr - prev_ld_addr;
      if (delta == prev_delta) {
        if (conf < (LOAD_TABLE_CONF + 1)) {
          conf++;
        }
      } else {
        // conf = conf / 2;
#if 1
        if (conf > (LOAD_TABLE_CONF / 2)) {
          conf = conf - 4;
        } else {
          conf = conf / 2;
        }
#endif
      }
      prev_data_delta = data_delta;
      prev_ld_data    = ld_data;
      ld_data         = dinst->getData();
      data_delta      = ld_data - prev_ld_data;
      if (data_delta == prev_data_delta) {
        if (data_conf < (LOAD_TABLE_DATA_CONF + 1)) {
          data_conf++;
        }
      } else {
        // conf = conf / 2;
#if 1
        if (data_conf > (LOAD_TABLE_DATA_CONF / 2)) {
          data_conf = data_conf - 4;
        } else {
          data_conf = data_conf / 2;
        }
#endif
      }
    }

    void lt_load_imm(Dinst *dinst) {
      ldpc  = dinst->getPC();
      is_li = true;
      conf  = 4096;
    }

    void lt_update_tracking(bool inc) {
      if (inc && tracking < 3) {
        tracking++;
      } else if (!inc && tracking > 0) {
        tracking--;
      }
    }
  };

  std::vector<load_table> load_table_vec = std::vector<load_table>(LOAD_TABLE_SIZE);

  struct pending_load_queue {  // queue of LOADS
    // fields: stride_ptr and tracking
    pending_load_queue() {
      tracking     = 0;
      load_pointer = 0;
    }
    Addr_t load_pointer;
    int    tracking;  // 0 to 3 - just like tracking in load_table_vec

    void plq_update_tracking(bool inc) {
      if (inc && tracking < 3) {
        tracking++;
      } else if (!inc && tracking > 0) {
        tracking--;
      }
    }
  };

  std::vector<pending_load_queue> plq_vec = std::vector<pending_load_queue>(PLQ_SIZE);

  struct load_outcome_reg {
    // tracks trigger load info as each TL completes execution
    // fields: load start, delta, index(or n data), stride pointer, data position

    load_outcome_reg() {
      brpc         = 0;
      ld_pointer   = 0;
      ld_start     = 0;
      ld_delta     = 0;
      data_pos     = 0;  // ++ @Fetch and 0 @flush
      use_slice    = 0;
      trig_ld_dist = 4;
      is_li        = false;
    }
    Addr_t  ld_start;  // load start addr
    int64_t ld_delta;
    Addr_t  ld_pointer;  // load pointer from stride pref table
    Addr_t  brpc;        // helps differentiate LOR entries when 2 Brs use same LD pair
    // int64_t data_pos; //
    int data_pos;  //
    // tracks data position in LOT queue; used to index lot queue when TL returns
    int use_slice;  // LOR's use_slice variable
    // init to 0, LOR accessed at fetch only when use_slice == 1
    bool    is_li;         // ESESC flag to not trigger load if Li
    int64_t trig_ld_dist;  //

    void reset_entry() {
      brpc         = 0;
      ld_pointer   = 0;
      ld_start     = 0;
      ld_delta     = 0;
      data_pos     = 0;
      use_slice    = 0;
      trig_ld_dist = 4;
      is_li        = false;
    }
  };

  std::vector<load_outcome_reg> lor_vec = std::vector<load_outcome_reg>(LOR_SIZE);

  struct load_outcome_table {  // same number of entries as LOR
    // stores trigger load data
    load_outcome_table() {
      std::fill(tl_addr.begin(), tl_addr.end(), 0);
      std::fill(valid.begin(), valid.end(), 0);
    }
    // std::vector<Data_t> data = std::vector<Data_t>(LOT_QUEUE_SIZE);
    std::vector<Addr_t> tl_addr = std::vector<Addr_t>(LOT_QUEUE_SIZE);
    std::vector<int>    valid   = std::vector<int>(LOT_QUEUE_SIZE);

    void reset_valid() {
      std::fill(tl_addr.begin(), tl_addr.end(), 0);
      std::fill(valid.begin(), valid.end(), 0);
    }
  };

  std::vector<load_outcome_table> lot_vec = std::vector<load_outcome_table>(LOR_SIZE);

  struct branch_outcome_table {
    branch_outcome_table() {
      brpc        = 0;
      outcome_ptr = 0;
      br_flip     = -1;
      load_ptr.clear();
      curr_br_addr.clear();
      std::fill(valid.begin(), valid.end(), 0);
    }

    Addr_t brpc;
    // int64_t outcome_ptr; //Br count at fetch; used to index BOT queue at fetch
    int outcome_ptr;  // Br count at fetch; used to index BOT queue at fetch
    int br_flip;      // stop LOT update when br-flips
    // init to -1; 0 -> flip on NT; 1 -> flip on T
    std::vector<Addr_t> load_ptr     = std::vector<Addr_t>(16);
    std::vector<Addr_t> curr_br_addr = std::vector<Addr_t>(16);  // current ld addr used by Br (ESESC only param - for debugging)
    std::vector<int>    valid        = std::vector<int>(LOT_QUEUE_SIZE);

    void reset_valid() {
      outcome_ptr = 0;
      std::fill(valid.begin(), valid.end(), 0);
    }
  };

  std::vector<branch_outcome_table> bot_vec = std::vector<branch_outcome_table>(BOT_SIZE);

#endif

  const std::string &getSection() const { return section; }
  const std::string &getName() const { return name; }
  const std::string &get_type() const { return mem_type; }
  uint16_t           getID() const { return id; }
  int16_t            getCoreID() const { return coreid; }
  void               setCoreDL1(int16_t cid) {
    coreid        = cid;
    firstLevelDL1 = true;
  }
  void setCoreIL1(int16_t cid) {
    coreid        = cid;
    firstLevelIL1 = true;
  }
  bool isFirstLevel() const { return coreid != -1; };
  bool isFirstLevelDL1() const { return firstLevelDL1; };
  bool isFirstLevelIL1() const { return firstLevelIL1; };

  MRouter *getRouter() { return router; }

  virtual void tryPrefetch(Addr_t addr, bool doStats, int degree, Addr_t pref_sign, Addr_t pc, CallbackBase *cb = 0) = 0;

  // Interface for fast-forward (no BW, just warmup caches)
  virtual TimeDelta_t ffread(Addr_t addr)  = 0;
  virtual TimeDelta_t ffwrite(Addr_t addr) = 0;

  // DOWN
  virtual void req(MemRequest *req)         = 0;
  virtual void setStateAck(MemRequest *req) = 0;
  virtual void disp(MemRequest *req)        = 0;

  virtual void doReq(MemRequest *req)         = 0;
  virtual void doSetStateAck(MemRequest *req) = 0;
  virtual void doDisp(MemRequest *req)        = 0;

  // UP
  virtual void blockFill(MemRequest *req);
  virtual void reqAck(MemRequest *req)   = 0;
  virtual void setState(MemRequest *req) = 0;

  virtual void doReqAck(MemRequest *req)   = 0;
  virtual void doSetState(MemRequest *req) = 0;

  virtual bool isBusy(Addr_t addr) const = 0;

  // Print stats
  virtual void dump() const;

  // Optional virtual methods
  virtual bool checkL2TLBHit(MemRequest *req);
  virtual void replayCheckLSQ_removeStore(Dinst *);
  virtual void updateXCoreStores(Addr_t addr);
  virtual void replayflush();
  virtual void plug();

  virtual void setNeedsCoherence();
  virtual void clearNeedsCoherence();

  virtual bool Invalid(Addr_t addr) const;
};

class DummyMemObj : public MemObj {
private:
protected:
public:
  DummyMemObj();
  DummyMemObj(const std::string &section, const std::string &sName);

  // Entry points to schedule that may schedule a do?? if needed
  void req(MemRequest *req) { doReq(req); };
  void reqAck(MemRequest *req) { doReqAck(req); };
  void setState(MemRequest *req) { doSetState(req); };
  void setStateAck(MemRequest *req) { doSetStateAck(req); };
  void disp(MemRequest *req) { doDisp(req); }

  // This do the real work
  void doReq(MemRequest *req);
  void doReqAck(MemRequest *req);
  void doSetState(MemRequest *req);
  void doSetStateAck(MemRequest *req);
  void doDisp(MemRequest *req);

  TimeDelta_t ffread(Addr_t addr);
  TimeDelta_t ffwrite(Addr_t addr);

  void tryPrefetch(Addr_t addr, bool doStats, int degree, Addr_t pref_sign, Addr_t pc, CallbackBase *cb = 0);

  bool isBusy(Addr_t addr) const;
};
