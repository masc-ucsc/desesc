// See LICENSE for details.

#include "MemObj.h"

#include <string.h>

#include <set>

#include "GMemorySystem.h"
#include "MemRequest.h"
#include "absl/strings/str_split.h"
#include "config.hpp"
#include "fmt/format.h"

uint16_t MemObj::id_counter = 0;

MemObj::MemObj(const std::string &sSection, const std::string &sName)
    /* constructor {{{1 */
    : section(sSection)
    , name(sName)
#ifdef ENABLE_LDBP
    , BOT_SIZE(Config::get_integer(section, "bot_size"))
    , LOR_SIZE(Config::get_integer(section, "lor_size"))
    , LOAD_TABLE_SIZE(Config::get_integer(section, "pref_size"))
    , PLQ_SIZE(Config::get_integer(section, "pref_size"))
    , CODE_SLICE_DELAY(Config::get_integer(section, "cs_delay"))
    , NUM_FSM_ALU(Config::get_integer(section, "num_fsm"))
#endif
    , id(id_counter++) {

  mem_type = Config::get_string(section, "type");

  coreid        = -1;  // No first Level cache by default
  firstLevelIL1 = false;
  firstLevelDL1 = false;
  isLLC         = false;

  std::vector<std::string> lower_level = absl::StrSplit(Config::get_string(section, "lower_level"), ' ');
  if (lower_level.empty()) {
    Config::add_error(fmt::format("malformed {} lower_level entry", section));
    return;
  }
  auto lower_level_type = Config::get_string(lower_level[0], "type");
  if (lower_level_type != "cache" && mem_type == "cache") {
    isLLC = true;
  }

  // Create router (different objects may override the default router)
  router = new MRouter(this);
  /*if(strcmp(section, "DL1_core") == 0){
  }*/

  if (!name.empty()) {
    std::string name_lc(name);
    std::transform(name_lc.begin(), name_lc.end(), name_lc.begin(), [](unsigned char c) { return std::tolower(c); });
    static std::set<std::string> usedNames;
    // Verify that one else uses the same name
    if (usedNames.find(name_lc) != usedNames.end()) {
      Config::add_error(fmt::format("multiple memory objects have same name '{}' (rename one of them)", name_lc));
    } else {
      usedNames.insert(name_lc);
    }
  }
}
/* }}} */

MemObj::~MemObj()
/* destructor {{{1 */
{}
/* }}} */

void MemObj::addLowerLevel(MemObj *obj) {
  router->addDownNode(obj);
  I(obj);
  obj->addUpperLevel(this);
}

void MemObj::addUpperLevel(MemObj *obj) {
  // printf("%s upper level is %s\n",getName(),obj->getName());
  router->addUpNode(obj);
}

void MemObj::blockFill(MemRequest *mreq) {
  (void)mreq;
  // Most objects do nothing
}

bool MemObj::isLastLevelCache() { return isLLC; }

#if 0
void MemObj::tryPrefetch(Addr_t addr, bool doStats, int degree, Addr_t pref_sign, Addr_t pc, CallbackBase *cb)
  /* forward tryPrefetch {{{1 */
{
  router->tryPrefetch(addr,doStats, degree, pref_sign, pc, cb);
}
/* }}} */
#endif

void MemObj::dump() const
/* dump statistics {{{1 */
{
  fmt::print("memObj name [{}]\n", name);
}
/* }}} */

DummyMemObj::DummyMemObj()
    /* dummy constructor {{{1 */
    : MemObj("dummySection", "dummyMem") {}
/* }}} */

DummyMemObj::DummyMemObj(const std::string &_section, const std::string &sName)
    /* dummy constructor {{{1 */
    : MemObj(_section, sName) {}
/* }}} */

#ifdef ENABLE_LDBP
// NEW INTERFACE !!!!!
void MemObj::hit_on_load_table(Dinst *dinst, bool is_li) {
  // if hit on load_table, update and move entry to LRU position
  for (int i = LOAD_TABLE_SIZE - 1; i >= 0; i--) {
    if (dinst->getPC() == load_table_vec[i].ldpc) {
      if (is_li) {
        load_table_vec[i].lt_load_imm(dinst);
      } else {
        load_table_vec[i].lt_load_hit(dinst);
      }
#if 0
      load_table l = load_table_vec[i];
      load_table_vec.erase(load_table_vec.begin() + i);
      load_table_vec.push_back(load_table());
      load_table_vec[LOAD_TABLE_SIZE - 1] = l;
#endif
      std::vector<load_table> lt;
      load_table              l = load_table_vec[i];
      for (int j = 0; j < LOAD_TABLE_SIZE; j++) {
        if (j != i) {
          lt.push_back(load_table_vec[j]);
        }
      }
      load_table_vec.clear();
      load_table_vec = lt;
      // load_table_vec.push_back(load_table());
      load_table_vec.push_back(l);

      if (load_table_vec[LOAD_TABLE_SIZE - 1].tracking > 0) {
        // append ld_ptr to PLQ
        // int plq_idx = return_plq_index(load_table_vec[LOAD_TABLE_SIZE - 1].ldpc);
        int plq_idx = return_plq_index(dinst->getPC());
        if (plq_idx != -1) {
          // pending_load_queue p = plq_vec[plq_idx];
          // plq_vec.erase(plq_vec.begin() + plq_idx);
          std::vector<pending_load_queue> plq;
          for (int x = 0; x < PLQ_SIZE; x++) {
            if (x != plq_idx) {
              plq.push_back(plq_vec[x]);
            }
          }
          plq_vec.clear();
          plq_vec = plq;
          plq_vec.push_back(pending_load_queue());

        } else {
          // plq_vec.erase(plq_vec.begin());
          std::vector<pending_load_queue> plq;
          for (int x = 1; x < PLQ_SIZE; x++) {
            plq.push_back(plq_vec[x]);
          }
          plq_vec.clear();
          plq_vec = plq;
          plq_vec.push_back(pending_load_queue());
        }
        // plq_idx = PLQ_SIZE - 1;
        plq_idx = plq_vec.size() - 1;
        // plq_vec.push_back(pending_load_queue());
        plq_vec[plq_idx].load_pointer = dinst->getPC();
        int lt_idx                    = return_load_table_index(dinst->getPC());
        if ((lt_idx != -1) && (load_table_vec[lt_idx].delta == load_table_vec[lt_idx].prev_delta)) {
          plq_vec[plq_idx].plq_update_tracking(true);
        } else {
          plq_vec[plq_idx].plq_update_tracking(false);
        }
      }
      return;
    }
  }
  // if load_table miss
#if 0
  load_table_vec.erase(load_table_vec.begin());
  load_table_vec.push_back(load_table());
#endif
  std::vector<load_table> lt;
  // load_table l = load_table_vec[i];
  for (int j = 1; j < load_table_vec.size(); j++) {
    lt.push_back(load_table_vec[j]);
  }
  load_table_vec.clear();
  load_table_vec = lt;
  load_table_vec.push_back(load_table());
  if (is_li) {
    load_table_vec[load_table_vec.size() - 1].lt_load_imm(dinst);
  } else {
    load_table_vec[load_table_vec.size() - 1].lt_load_miss(dinst);
  }
}

int MemObj::return_load_table_index(Addr_t pc) {
  // if hit on load_table, update and move entry to LRU position
  for (int i = LOAD_TABLE_SIZE - 1; i >= 0; i--) {
    // for(int i = 0; i < LOAD_TABLE_SIZE; i++) {
    if (pc == load_table_vec[i].ldpc) {
      return i;
    }
  }
  return -1;
}

int MemObj::return_plq_index(Addr_t pc) {
  // if hit on load_table, update and move entry to LRU position
  for (int i = PLQ_SIZE - 1; i >= 0; i--) {
    // for(int i = 0; i < PLQ_SIZE; i++) {
    if (pc == plq_vec[i].load_pointer) {
      return i;
    }
  }
  return -1;
}

void MemObj::lor_find_index(Addr_t tl_addr) {
  // lor_trigger_load_completeCB::schedule(1, this, tl_addr);
  // lor_trigger_load_completeCB::schedule(CODE_SLICE_DELAY, this, tl_addr);

  if (!fsm_alu_port) {
    fsm_alu_port = PortGeneric::create("fsm_alu_0", NUM_FSM_ALU, 1);
  }

  Time_t fsm_alu_time = fsm_alu_port->nextSlot(false);
  fsm_alu_time += CODE_SLICE_DELAY;  // 1 cycle delay for simple ALUs
  lor_trigger_load_completeCB::scheduleAbs(fsm_alu_time, this, tl_addr);

#if 0
  for(int i = 0; i < lor_vec.size(); i++) {
    Addr_t lor_start  = lor_vec[i].ld_start;
    int64_t lor_delta  = lor_vec[i].ld_delta;
    int idx = 0;
    if(lor_delta != 0) {
      idx             = (tl_addr - lor_start) / lor_delta;
    }
    Addr_t lor_end_addr = lor_start + ((LOT_QUEUE_SIZE - 1) * lor_delta);
    Addr_t check_addr = lor_start + (idx * lor_delta);
    bool tl_addr_check = lot_tl_addr_range(check_addr, lor_start, lor_end_addr, lor_delta);
    if(tl_addr_check && (tl_addr == check_addr)) {
      //hit on LOR for returning TL
      //get absolute value of queue index
      idx = abs(idx);
      lot_fill_data(i, idx, tl_addr);
    }
  }
#endif
}

void MemObj::lor_trigger_load_complete(Addr_t tl_addr) {
  // Addr_t tl_addr = mreq->getAddr();
  for (int i = 0; i < LOR_SIZE; i++) {
    Addr_t  lor_start = lor_vec[i].ld_start;
    int64_t lor_delta = lor_vec[i].ld_delta;
    int     idx       = 0;
    if (lor_delta != 0) {
      idx = (tl_addr - lor_start) / lor_delta;
    }
    // Addr_t lor_end_addr = lor_start + ((LOT_QUEUE_SIZE - 1) * lor_delta);
    Addr_t lor_end_addr  = lor_start + ((getLotQueueSize() - 1) * lor_delta);
    Addr_t check_addr    = lor_start + (idx * lor_delta);
    bool   tl_addr_check = lot_tl_addr_range(check_addr, lor_start, lor_end_addr, lor_delta);
    if (tl_addr_check && (tl_addr == check_addr)) {
      // hit on LOR for returning TL
      // get absolute value of queue index
      idx = abs(idx);
      lot_fill_data(i, idx, tl_addr);
    }
  }
}

bool MemObj::lot_tl_addr_range(Addr_t tl_addr, Addr_t start, Addr_t end, int64_t del) {
  // returns true if tl_addr within [start, end] range;  else false
  if (del < 0) {
    if ((tl_addr <= start) && (tl_addr >= end)) {
      return true;
    }
  } else {
    if ((tl_addr >= start) && (tl_addr <= end)) {
      return true;
    }
  }
  return false;
}

void MemObj::lot_fill_data(int lot_id, int lot_queue_id, Addr_t tl_addr) {
  // data_pos updated @F -> use ld_ptr info from BOT @Br fetch; ++ data_pos when we ++ outcome_ptr
  // lot_queue_pos => [curr_br_count + lor_id(based on mreq_addr)] % lot_q_size
  int lot_queue_entry = (lor_vec[lot_id].data_pos + lot_queue_id) % getLotQueueSize();
  // lor_vec[lot_id].data_pos, lot_queue_id);
  lot_vec[lot_id].tl_addr[lot_queue_entry] = tl_addr;
  lot_vec[lot_id].valid[lot_queue_entry]   = 1;
}

void MemObj::bot_allocate(Addr_t brpc, Addr_t ld_ptr, Addr_t ld_ptr_addr) {
  int bot_id = return_bot_index(brpc);
  if (bot_id != -1) {
    bot_vec[bot_id].brpc = brpc;
    // bot_vec[bot_id].outcome_ptr = 1;
    bot_vec[bot_id].outcome_ptr = 0;
    // bot_vec[bot_id].load_ptr.push_back(ld_ptr);
    // bot_vec[bot_id].curr_br_addr.push_back(ld_ptr_addr);
    branch_outcome_table b = bot_vec[bot_id];
#if 0
    bot_vec.erase(bot_vec.begin() + bot_id);
    bot_vec.push_back(branch_outcome_table());
    bot_vec[BOT_SIZE - 1] = b;
#endif
    std::vector<branch_outcome_table> bot;
    for (int i = 0; i < BOT_SIZE; i++) {
      if (i != bot_id) {
        bot.push_back(bot_vec[i]);
      }
    }
    bot_vec.clear();
    bot_vec = bot;
    bot_vec.push_back(branch_outcome_table());
    bot_vec[BOT_SIZE - 1].brpc        = brpc;
    bot_vec[BOT_SIZE - 1].outcome_ptr = 0;
    return;
  }
  // bot_vec.erase(bot_vec.begin());
  std::vector<branch_outcome_table> bot;
  for (int i = 0; i < BOT_SIZE; i++) {
    if (i != 0) {
      bot.push_back(bot_vec[i]);
    }
  }
  bot_vec.clear();
  bot_vec = bot;
  bot_vec.push_back(branch_outcome_table());
  bot_vec[BOT_SIZE - 1].brpc        = brpc;
  bot_vec[BOT_SIZE - 1].outcome_ptr = 0;
  // bot_vec[LOR_SIZE - 1].load_ptr.push_back(ld_ptr);
}

void MemObj::lor_allocate(Addr_t brpc, Addr_t ld_ptr, Addr_t ld_start_addr, int64_t ld_del, int data_pos, bool is_li) {
#if 0
  int lor_id = compute_lor_index(brpc, ld_ptr);
#if 0
  if(lor_index_track > LOR_SIZE - 1) {
    lor_index_track = 0;
  }
  int lor_id = ((lor_index_track++) % LOR_SIZE);
#endif

  lor_vec[lor_id].ld_pointer = ld_ptr;
  lor_vec[lor_id].brpc = brpc;
  lor_vec[lor_id].ld_start = ld_start_addr;
  lor_vec[lor_id].ld_delta = ld_del;
  lor_vec[lor_id].data_pos = 0;
  lor_vec[lor_id].trig_ld_dist = 4;
  lor_vec[lor_id].is_li = is_li;
  //reset corresponding LOT entry too
  lot_vec[lor_id].reset_valid();
#endif

#if 1
  // int lor_id = return_lor_index(ld_ptr);
  int lor_id = compute_lor_index(brpc, ld_ptr);
  if (lor_id != -1) {
#if 0
    lor_vec[lor_id].ld_pointer = ld_ptr;
    lor_vec[lor_id].brpc = brpc;
    lor_vec[lor_id].ld_start = ld_start_addr;
    lor_vec[lor_id].ld_delta = ld_del;
    lor_vec[lor_id].data_pos = 0;
    lor_vec[lor_id].trig_ld_dist = 4;
    lor_vec[lor_id].is_li = is_li;

    load_outcome_reg l = lor_vec[lor_id];
    lor_vec.erase(lor_vec.begin() + lor_id);
    lor_vec.push_back(load_outcome_reg());
    lor_vec[LOR_SIZE - 1] = l;
#endif
    // move LOR entry to LRU position
    std::vector<load_outcome_reg> lor;
    for (int i = 0; i < LOR_SIZE; i++) {
      if (i != lor_id) {
        lor.push_back(lor_vec[i]);
      }
    }
    lor_vec.clear();
    lor_vec = lor;
    lor_vec.push_back(load_outcome_reg());

    lor_vec[LOR_SIZE - 1].ld_pointer   = ld_ptr;
    lor_vec[LOR_SIZE - 1].brpc         = brpc;
    lor_vec[LOR_SIZE - 1].ld_start     = ld_start_addr;
    lor_vec[LOR_SIZE - 1].ld_delta     = ld_del;
    lor_vec[LOR_SIZE - 1].data_pos     = 0;
    lor_vec[LOR_SIZE - 1].trig_ld_dist = 4;
    lor_vec[LOR_SIZE - 1].is_li        = is_li;

    // reset corresponding LOT entry too
#if 0
    //lot_vec[lor_id].reset_valid();
    load_outcome_table lot = lot_vec[lor_id];
    lot_vec.erase(lot_vec.begin() + lor_id);
    lot_vec.push_back(load_outcome_table());
    //lot_vec[LOR_SIZE - 1] = lot;
#endif
    std::vector<load_outcome_table> lot;
    for (int i = 0; i < LOR_SIZE; i++) {
      if (i != lor_id) {
        lot.push_back(lot_vec[i]);
      }
    }
    lot_vec.clear();
    lot_vec = lot;
    lot_vec.push_back(load_outcome_table());
    // lot_vec[LOR_SIZE - 1].reset_valid();

    return;
  }

  // if miss on LOR table
#if 0
  lor_vec.erase(lor_vec.begin());
  lor_vec.push_back(load_outcome_reg());
#endif
  std::vector<load_outcome_reg> lor;
  for (int i = 1; i < LOR_SIZE; i++) {
    lor.push_back(lor_vec[i]);
    // if(i != 0) {
    //   lor.push_back(lor_vec[i]);
    // }
  }
  lor_vec.clear();
  lor_vec = lor;
  lor_vec.push_back(load_outcome_reg());

  lor_vec[LOR_SIZE - 1].ld_pointer   = ld_ptr;
  lor_vec[LOR_SIZE - 1].brpc         = brpc;
  lor_vec[LOR_SIZE - 1].ld_start     = ld_start_addr;
  lor_vec[LOR_SIZE - 1].ld_delta     = ld_del;
  lor_vec[LOR_SIZE - 1].data_pos     = 0;
  lor_vec[LOR_SIZE - 1].trig_ld_dist = 4;
  lor_vec[LOR_SIZE - 1].is_li        = is_li;

  // update corresponding LOT entry as well
#if 0
  lot_vec.erase(lot_vec.begin());
  lot_vec.push_back(load_outcome_table());
  //lot_vec[LOR_SIZE - 1].reset_valid();
#endif

  std::vector<load_outcome_table> lot;
  for (int i = 1; i < LOR_SIZE; i++) {
    lot.push_back(lot_vec[i]);
    // if(i != 0) {
    //   lot.push_back(lot_vec[i]);
    // }
  }
  lot_vec.clear();
  lot_vec = lot;
  lot_vec.push_back(load_outcome_table());

#endif
}

int MemObj::return_bot_index(Addr_t brpc) {
  // for(int i = 0; i < BOT_SIZE; i++) {
  for (int i = BOT_SIZE - 1; i >= 0; i--) {
    if (brpc == bot_vec[i].brpc) {
      return i;
    }
  }
  return -1;
}

int MemObj::return_lor_index(Addr_t ld_ptr) {
  // for(int i = 0; i < LOR_SIZE; i++) {
  for (int i = LOR_SIZE - 1; i >= 0; i--) {
    if (ld_ptr == lor_vec[i].ld_pointer) {
      return i;
    }
  }
  return -1;
}

int MemObj::compute_lor_index(Addr_t brpc, Addr_t ld_ptr) {
#if 0
  int lor_idx = (brpc ^ ld_ptr) & (LOR_SIZE - 1);
  return lor_idx;
#endif

#if 1
  // for(int i = 0; i < LOR_SIZE; i++) {
  for (int i = LOR_SIZE - 1; i >= 0; i--) {
    if ((lor_vec[i].brpc == brpc) && (lor_vec[i].ld_pointer == ld_ptr)) {
      return i;
    }
  }
  return -1;
#endif
}

#if 0
void MemObj::fill_li_at_retire(Addr_t brpc, int ldbr, bool d1_valid, bool d2_valid, int depth1, int depth2, Data_t d1, Data_t d2) {
  int i = hit_on_bot(brpc);
  if(i != -1) {
    cir_queue[i].ldbr = ldbr;
    if(d1_valid && !d2_valid) {
      cir_queue[i].ldbr_type[0] = ldbr;
      cir_queue[i].set_flag[0] = 0;
      cir_queue[i].dep_depth[0] = depth1;
      cir_queue[i].li_data_valid = true;
      cir_queue[i].li_data = d1;
    }else if(!d1_valid && d2_valid) {
      cir_queue[i].ldbr_type2[0] = ldbr;
      cir_queue[i].set_flag2[0] = 0;
      cir_queue[i].dep_depth2[0] = depth1;
      cir_queue[i].li_data2_valid = true;
      cir_queue[i].li_data2 = d2;
    }else if(d1_valid && d2_valid) {
      cir_queue[i].ldbr_type[0] = ldbr;
      cir_queue[i].set_flag[0] = 0;
      cir_queue[i].dep_depth[0] = depth1;
      cir_queue[i].li_data_valid = true;
      cir_queue[i].li_data = d1;

      cir_queue[i].ldbr_type2[0] = ldbr;
      cir_queue[i].set_flag2[0] = 0;
      cir_queue[i].dep_depth2[0] = depth2;
      cir_queue[i].li_data2_valid = true;
      cir_queue[i].li_data2 = d2;
    }
  }
}

int MemObj::hit_on_ldbuff(Addr_t pc) {
  //hit on ld_buff is start addr and end addr matches
  for(int i = 0; i < LDBUFF_SIZE; i++) {
    if(load_data_buffer[i].brpc == pc) { //hit on ld_buff @retire
      return i;
    }
  }
  return -1;
}
int MemObj::hit_on_bot(Addr_t pc) {
  for(int i = 0; i < BOT_SIZE; i++) {
    if(cir_queue[i].brpc == pc) //hit on BOT
      return i;
  }
  return -1; //miss
}

void MemObj::fill_mv_stats(Addr_t pc, int ldbr, Data_t d1, Data_t d2, bool swap, int mv_out) {
  int idx = hit_on_bot(pc);
  //if(idx != -1 && cir_queue[idx].br_mv_outcome == 0) { //hit on bot
  if(idx != -1 && swap) { //hit on bot && swap
    cir_queue[idx].br_mv_outcome = mv_out;
    cir_queue[idx].ldbr      = ldbr;
    if(ldbr == 22) {
      cir_queue[idx].br_data1      = d2;
    }else if(ldbr == 21) {
      cir_queue[idx].br_data2      = d1;
    }
    return;
  }
}

void MemObj::fill_fetch_count_bot(Addr_t pc) {
  int idx = hit_on_bot(pc);
  if(idx != -1) { //hit on bot
    cir_queue[idx].fetch_count++;
    //return;
  }else {
    //insert entry into LRU postion (last index)
    cir_queue.erase(cir_queue.begin());
    cir_queue.push_back(bot_entry());
    cir_queue[BOT_SIZE-1].brpc        = pc;
    cir_queue[BOT_SIZE-1].fetch_count = 1;
  }
}

void MemObj::fill_retire_count_bot(Addr_t pc) {
  int idx = hit_on_bot(pc);
  if(idx != -1) {
    if(cir_queue[idx].fetch_count > cir_queue[idx].retire_count) {
      cir_queue[idx].retire_count++;
      return;
    }else {
      //reset fc and rc if rc > fc at any point
      ////FIXME: should u also retire set_flag cir_queue here?????
      cir_queue[idx].fetch_count = 0;
      cir_queue[idx].retire_count = 0;
    }
  }
}

void MemObj::fill_bpred_use_count_bot(Addr_t brpc, int _hit2_miss3, int _hit3_miss2) {
  int idx = hit_on_bot(brpc);
  if(idx != -1) {
    cir_queue[idx].hit2_miss3 = _hit2_miss3;
    cir_queue[idx].hit3_miss2 = _hit3_miss2;
  }
}

void MemObj::fill_ldbuff_mem(Addr_t pc, Addr_t sa, Addr_t ea, int64_t del, Addr_t raddr, int q_idx, int tl_type) {
  int i = hit_on_ldbuff(pc);
  if(i != -1) { //hit on ldbuff
    if(tl_type == 1) {
      load_data_buffer[i].start_addr      = sa;
      load_data_buffer[i].end_addr        = ea;
      load_data_buffer[i].delta           = del;
      load_data_buffer[i].req_addr[q_idx] = raddr;
    }else if(tl_type == 2) {
      load_data_buffer[i].start_addr2      = sa;
      load_data_buffer[i].end_addr2        = ea;
      load_data_buffer[i].delta2           = del;
      load_data_buffer[i].req_addr2[q_idx] = raddr;
    }
    //move this entry to LRU position
    load_data_buffer_entry l = load_data_buffer[i];
    load_data_buffer.erase(load_data_buffer.begin() + i);
    load_data_buffer.push_back(load_data_buffer_entry());
    load_data_buffer[LDBUFF_SIZE-1] = l;
    return;
  }
  //if miss, insert entry into LRU postion (last index)
  load_data_buffer.erase(load_data_buffer.begin());
  load_data_buffer.push_back(load_data_buffer_entry());
  load_data_buffer[LDBUFF_SIZE-1].brpc            = pc;
  if(tl_type == 1) {
    load_data_buffer[LDBUFF_SIZE-1].delta           = del;
    load_data_buffer[LDBUFF_SIZE-1].start_addr      = sa;
    load_data_buffer[LDBUFF_SIZE-1].end_addr        = ea;
    load_data_buffer[LDBUFF_SIZE-1].req_addr[q_idx] = raddr;
  }else if(tl_type == 2) {
    load_data_buffer[LDBUFF_SIZE-1].delta2           = del;
    load_data_buffer[LDBUFF_SIZE-1].start_addr2      = sa;
    load_data_buffer[LDBUFF_SIZE-1].end_addr2        = ea;
    load_data_buffer[LDBUFF_SIZE-1].req_addr2[q_idx] = raddr;
  }
}

void MemObj::fill_bot_retire(Addr_t pc, Addr_t ldpc, Addr_t saddr, Addr_t eaddr, int64_t del, int lb_type, int tl_type) {
/*
 * fill ret count, saddr, eaddr ,delta @retire and move entry to LRU; send trig_load req
 * @mem - if mreq addr is within [saddr, eaddr], then fill cir_queue
 * */
  int bot_idx = hit_on_bot(pc);
  //must be a hit(bcos fc must be inserted @F). if miss, then don't do anything
  if(bot_idx != -1) {
    if(tl_type == 1) {
      if(cir_queue[bot_idx].seq_start_addr == 0) {
        cir_queue[bot_idx].seq_start_addr = saddr;
      }
      cir_queue[bot_idx].ldpc       = ldpc;
      cir_queue[bot_idx].start_addr = saddr;
      cir_queue[bot_idx].end_addr   = eaddr;
      cir_queue[bot_idx].delta      = del;
      cir_queue[bot_idx].ldbr       = lb_type;
    }else if(tl_type == 2) {
      if(cir_queue[bot_idx].seq_start_addr2 == 0) {
        cir_queue[bot_idx].seq_start_addr2 = saddr;
      }
      cir_queue[bot_idx].ldpc2       = ldpc;
      cir_queue[bot_idx].start_addr2 = saddr;
      cir_queue[bot_idx].end_addr2   = eaddr;
      cir_queue[bot_idx].delta2      = del;
      cir_queue[bot_idx].ldbr2       = lb_type;
    }
  }
}

void MemObj::find_cir_queue_index(MemRequest *mreq) {
  int64_t delta      = mreq->getDelta();
  int ldbr            = mreq->getLBType();
  int depth           = mreq->getDepDepth();
  Addr_t brpc       = mreq->getDepPC();
  Addr_t req_addr   = mreq->getAddr();
  int bot_idx         = hit_on_bot(brpc);
  int q_idx           = 0;
  int tl_type         = mreq->getTLType();
  zero_delta          = true;

  if(bot_idx != -1) {
      Addr_t saddr;
      Addr_t eaddr;
      int64_t del;
    if(tl_type == 1) {
      saddr = cir_queue[bot_idx].start_addr;
      eaddr = cir_queue[bot_idx].end_addr;
      del   = cir_queue[bot_idx].delta;
    }else if(tl_type == 2) {
      saddr = cir_queue[bot_idx].start_addr2;
      eaddr = cir_queue[bot_idx].end_addr2;
      del   = cir_queue[bot_idx].delta2;
    }
    if(delta != del) {
      //reset fc, rc, cir_queue
      flush_bot_mem(bot_idx);
      flush_ldbuff_mem(brpc);
      return;
    }
    if(req_addr >= saddr && req_addr <= eaddr) {
      if(delta != 0) {
        q_idx      = abs((int)(req_addr-saddr) / delta);
        zero_delta = false;
      }
      if(q_idx <= CIR_QUEUE_WINDOW - 1) {
        if(tl_type == 1) {
          cir_queue[bot_idx].set_flag[q_idx]  = 1;
          cir_queue[bot_idx].ldbr_type[q_idx] = ldbr;
          cir_queue[bot_idx].dep_depth[q_idx] = depth;
          cir_queue[bot_idx].trig_addr[q_idx] = req_addr;
          cir_queue[bot_idx].ld_used[q_idx]   = mreq->isLoadUsed();
        }else if(tl_type == 2) {
          cir_queue[bot_idx].set_flag2[q_idx]  = 1;
          cir_queue[bot_idx].ldbr_type2[q_idx] = ldbr;
          cir_queue[bot_idx].dep_depth2[q_idx] = depth;
          cir_queue[bot_idx].trig_addr2[q_idx] = req_addr;
          cir_queue[bot_idx].ld_used2[q_idx]   = mreq->isLoadUsed();
        }
        fill_ldbuff_mem(brpc, saddr, eaddr, del, req_addr, q_idx, tl_type);
        // move this entry to LRU position
        bot_entry b = cir_queue[bot_idx];
        cir_queue.erase(cir_queue.begin() + bot_idx);
        cir_queue.push_back(bot_entry());
        cir_queue[BOT_SIZE - 1] = b;
      }
    }
  }

}

void MemObj::flush_ldbuff_mem(Addr_t pc) {
  int i = hit_on_ldbuff(pc);
  if(i != -1) {
    load_data_buffer.erase(load_data_buffer.begin() + i);
    load_data_buffer.push_back(load_data_buffer_entry());
  }
}

void MemObj::flush_bot_mem(int idx) {
  for(int i = 0; i < CIR_QUEUE_WINDOW; i++) {
    cir_queue[idx].set_flag[i]  = 0;
    cir_queue[idx].ld_used[i]  = 0;
    cir_queue[idx].ldbr_type[i]  = 0;
    cir_queue[idx].dep_depth[i]  = 0;
    cir_queue[idx].trig_addr[i]  = 0;
    cir_queue[idx].set_flag2[i]  = 0;
    cir_queue[idx].ld_used2[i]  = 0;
    cir_queue[idx].ldbr_type2[i]  = 0;
    cir_queue[idx].dep_depth2[i]  = 0;
    cir_queue[idx].trig_addr2[i]  = 0;
  }
  cir_queue[idx].fetch_count = 0;
  cir_queue[idx].retire_count = 0;

  cir_queue[idx].seq_start_addr = 0;
  cir_queue[idx].start_addr = 0;
  cir_queue[idx].end_addr = 0;
  cir_queue[idx].req_addr = 0;
  cir_queue[idx].ldpc     = 0;

  cir_queue[idx].seq_start_addr2 = 0;
  cir_queue[idx].start_addr2 = 0;
  cir_queue[idx].end_addr2 = 0;
  cir_queue[idx].req_addr2 = 0;
  cir_queue[idx].ldpc2     = 0;

  cir_queue[idx].ldbr           = 0;
  cir_queue[idx].li_data_valid  = false;
  cir_queue[idx].li_data2_valid = false;
  //cir_queue.erase(cir_queue.begin() + idx);
  //cir_queue.push_back(bot_entry());
}

void MemObj::shift_cir_queue(Addr_t pc, int tl_type) {
  //shift the cir queue left by one position and push zero at the end
  int bot_idx         = hit_on_bot(pc);
  if(bot_idx != -1) {
    if(tl_type == 1) {
      cir_queue[bot_idx].set_flag.erase(cir_queue[bot_idx].set_flag.begin());
      cir_queue[bot_idx].set_flag.push_back(0);
      cir_queue[bot_idx].ldbr_type.erase(cir_queue[bot_idx].ldbr_type.begin());
      cir_queue[bot_idx].ldbr_type.push_back(0);
      cir_queue[bot_idx].dep_depth.erase(cir_queue[bot_idx].dep_depth.begin());
      cir_queue[bot_idx].dep_depth.push_back(0);
      cir_queue[bot_idx].trig_addr.erase(cir_queue[bot_idx].trig_addr.begin());
      cir_queue[bot_idx].trig_addr.push_back(0);
    }else if(tl_type == 2){
      cir_queue[bot_idx].set_flag2.erase(cir_queue[bot_idx].set_flag2.begin());
      cir_queue[bot_idx].set_flag2.push_back(0);
      cir_queue[bot_idx].ldbr_type2.erase(cir_queue[bot_idx].ldbr_type2.begin());
      cir_queue[bot_idx].ldbr_type2.push_back(0);
      cir_queue[bot_idx].dep_depth2.erase(cir_queue[bot_idx].dep_depth2.begin());
      cir_queue[bot_idx].dep_depth2.push_back(0);
      cir_queue[bot_idx].trig_addr2.erase(cir_queue[bot_idx].trig_addr2.begin());
      cir_queue[bot_idx].trig_addr2.push_back(0);
    }
  }
  //shift load data buffer as well
  shift_load_data_buffer(pc, tl_type);
}

void MemObj::shift_load_data_buffer(Addr_t pc, int tl_type) {
  //shift the load data buff left by one position and push zero at the end
  int i = hit_on_ldbuff(pc);
  if(i != -1) {
    if(tl_type == 1) {
      load_data_buffer[i].req_addr.erase(load_data_buffer[i].req_addr.begin());
      load_data_buffer[i].req_addr.push_back(0);
      load_data_buffer[i].req_data.erase(load_data_buffer[i].req_data.begin());
      load_data_buffer[i].req_data.push_back(0);
      load_data_buffer[i].marked.erase(load_data_buffer[i].marked.begin());
      load_data_buffer[i].marked.push_back(false);
    } else if(tl_type == 2) {
      load_data_buffer[i].req_addr2.erase(load_data_buffer[i].req_addr2.begin());
      load_data_buffer[i].req_addr2.push_back(0);
      load_data_buffer[i].req_data2.erase(load_data_buffer[i].req_data2.begin());
      load_data_buffer[i].req_data2.push_back(0);
      load_data_buffer[i].marked2.erase(load_data_buffer[i].marked2.begin());
      load_data_buffer[i].marked2.push_back(false);
    }
  }
}
#endif
#endif
void DummyMemObj::doReq(MemRequest *req)
/* req {{{1 */
{
  I(0);
  req->ack();
}
/* }}} */

void DummyMemObj::doReqAck(MemRequest *req)
/* reqAck {{{1 */
{
  (void)req;
  I(0);
}
/* }}} */

void DummyMemObj::doSetState(MemRequest *req)
/* setState {{{1 */
{
  (void)req;
  I(0);
  req->ack();
}
/* }}} */

void DummyMemObj::doSetStateAck(MemRequest *req)
/* setStateAck {{{1 */
{
  (void)req;
  I(0);
}
/* }}} */

void DummyMemObj::doDisp(MemRequest *req)
/* disp {{{1 */
{
  (void)req;
  I(0);
}
/* }}} */

bool DummyMemObj::isBusy(Addr_t addr) const
// Can it accept more requests {{{1
{
  (void)addr;
  return false;
}
// }}}

TimeDelta_t DummyMemObj::ffread(Addr_t addr)
/* fast forward read {{{1 */
{
  (void)addr;
  return 1;  // 1 cycle does everything :)
}
/* }}} */

TimeDelta_t DummyMemObj::ffwrite(Addr_t addr)
/* fast forward write {{{1 */
{
  (void)addr;
  return 1;  // 1 cycle does everything :)
}
/* }}} */

void DummyMemObj::tryPrefetch(Addr_t addr, bool doStats, int degree, Addr_t pref_sign, Addr_t pc, CallbackBase *cb)
/* forward tryPrefetch {{{1 */
{
  (void)addr;
  (void)doStats;
  (void)degree;
  (void)pref_sign;
  (void)pc;
  if (cb) {
    cb->destroy();
  }
}
/* }}} */

/* Optional virtual methods {{{1 */
bool MemObj::checkL2TLBHit(MemRequest *req) {
  // If called, it should be redefined by the object
  (void)req;
  I(0);
  return false;
}
void MemObj::replayCheckLSQ_removeStore(Dinst *dinst) {
  (void)dinst;
  I(0);
}
void MemObj::updateXCoreStores(Addr_t addr) {
  (void)addr;
  I(0);
}
void MemObj::replayflush() { I(0); }
void MemObj::setTurboRatio(float r) {
  (void)r;
  I(0);
}
void MemObj::plug() { I(0); }
void MemObj::setNeedsCoherence() {
  // Only cache uses this
}
void MemObj::clearNeedsCoherence() {}

#if 0
bool MemObj::get_cir_queue(int index, Addr_t pc) {
  I(0);
}
#endif

bool MemObj::Invalid(Addr_t addr) const {
  (void)addr;
  I(0);
  return false;
}
/* }}} */
