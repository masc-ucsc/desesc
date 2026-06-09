// See LICENSE for details.

#include "store_buffer.hpp"

#include "absl/container/flat_hash_map.h"
#include "absl/strings/str_split.h"
#include "config.hpp"
#include "memrequest.hpp"
#include "resource.hpp"

using ownership_doneCB = CallbackMember1<Store_buffer, Addr_t, &Store_buffer::ownership_done>;

Store_buffer::Store_buffer(Hartid_t hid, std::shared_ptr<Gmemory_system> ms) {
  std::vector<std::string> v      = absl::StrSplit(Config::get_string("soc", "core", hid, "il1"), ' ');
  auto                     l1_sec = v[0];
  line_size                       = Config::get_power2(l1_sec, "line_size");

  bool enableDcache = Config::get_bool("soc", "core", hid, "caches");
  if (enableDcache) {
    dl1 = ms->getDL1();
  } else {
    dl1 = nullptr;
  }

  // scb_clean_lines     = 0;
  // scb_lines_num       = 0;
  line_size_addr_bits = log2i(line_size);
  line_size_mask      = line_size - 1;
  /*scb_size=32*/
  scb_size        = Config::get_integer("soc", "core", hid, "scb_size", 1, 2048);
  scb_clean_lines = scb_size;
  // scb_lines_num       = 0;
}

bool Store_buffer::can_accept_st(Addr_t st_addr) const {
  /* scb_clean_lines can be wrtiteback to L1cache and new entry can be accepted*/
  /* scb_clean_lines can be wrtiteback to L1cache; so  new space can be created by deleting clean lines; 34-5<32*/
  // if ((static_cast<int>(scb_lines_map.size()) - scb_clean_lines) < scb_size) {
  int scb_clean = this->get_clean_num();
  if ((static_cast<int>(scb_lines_map.size()) - scb_clean) < scb_size) {
    // printf("Store_buffer::can_accept_st::TRUE return can accept::  addr %llu and scb.size() %zu and scb_clean%d\n",
           // st_addr,
           // scb_lines_map.size(),
           // scb_clean);
    return true;
  } else {
    // printf(
        // "Store_buffer::can_accept_st:: SCB full: size>scb_size::return can  not accept::addr %llu and scb.size() %zu and scb_clean "
        // "%d\n",
        // st_addr,
        // scb_lines_map.size(),
        // scb_clean);
  }

  auto it = scb_lines_map.find(calc_line(st_addr));
  if (it != scb_lines_map.end()) {
    // printf("Store_buffer:can_accept_st::RETURN TRUE addr already in scb  %llu and line_addr %llu\n", st_addr, calc_line(st_addr));
    return true;
  } else {
    // printf(
        // "Store_buffer:can_accept_st::return:: RETURN FALSE :: addr NOT  in scb + SCBFull:size >scb_size::%llu and line_addr %llu\n",
        // st_addr,
        // calc_line(st_addr));
    return false;
  }
  return it != scb_lines_map.end();
}

int Store_buffer::get_clean_num() const {
  // printf("Store_buffer::get_clean():: Entering in scb\n");
  int num = 0;
  for (auto it = scb_lines_map.begin(); it != scb_lines_map.end(); ++it) {
    if (it->second.is_clean()) {
      num++;
    }
  }
  // printf("Store_buffer::get_clean:: After scbclean num is  %d \n", num);
  return num;
}

void Store_buffer::remove_clean() {
  // I(scb_clean_lines);

  // printf("Store_buffer::remove_clean():: Entering in scb\n");
  // int scb_clean = this->get_clean_num();
  // printf("Store_buffer::remove_clean:: Before scb.size() %ld and scb_clean%d\n", scb_lines_map.size(), scb_clean);
  // printf("Store_buffer::remove_clean:: Before scb_clean is  %d \n", scb_clean);
  size_t num = 0;

  absl::erase_if(scb_lines_map, [&num](std::pair<const Addr_t, Store_buffer_line> p) {
    // if (p.second.is_safe()) { stores safe only can be write back to L1cache from scb_spec
    if (p.second.is_clean()) {
      // printf("Store_buffer::remove_clean():: Removing  st_addr_line %llu from scb\n", p.first);
      ++num;
      return true;
    }
    // printf("Store_buffer::remove_clean():: NOT Removing  st_addr_line %llu \n", p.first);
    return false;
  });

  // printf("Store_buffer::remove_clean:: After scb.size() %ld and scb_clean%d\n", scb_lines_map.size(), scb_clean);
  // printf("Store_buffer::remove_clean:: After scb_clean is  %d \n", scb_clean);
  // printf("Store_buffer::remove_clean():: Leaving from scb\n");
}

void Store_buffer::flush_transient() {
  // I(scb_clean_lines);

  // printf("Store_buffer::remove_clean():: Entering in scb\n");
  size_t num       = 0;
  // int    scb_clean = this->get_clean_num();
  // printf("Store_buffer::flush transinet:: Before scb.size() %ld and scb_clean%d\n", scb_lines_map.size(), scb_clean);

  absl::erase_if(scb_lines_map, [&num](std::pair<const Addr_t, Store_buffer_line> p) {
    // if (p.second.is_safe()) { stores safe only can be write back to L1cache from scb_spec
    if (p.second.is_transient()) {
      // printf("Store_buffer::flushtransient:: Removing  st_addr_line %llu from scb\n", p.first);
      ++num;
      return true;
    }
    return false;
  });

  // printf("Store_buffer::flush transinet:: After scb.size() %ld and flushed trunsients num is %ld\n", scb_lines_map.size(), num);
  // printf("Store_buffer::flush_transient: Leaving from scb\n");
}

void Store_buffer::remove_spec_load(Dinst* dinst) {
  /*spec_load removed from scb*/

  // printf("Store_buffer::remove_spec_load:: Entering for specLoad to scb inst  %llu\n", dinst->getID());
  // I(scb_lines_num);
  Addr_t addr      = dinst->getAddr();
  Addr_t addr_line = calc_line(addr);

  // remove_clean();
  // int scb_clean = this->get_clean_num();
  // printf("Store_buffer::remove::spec load addr %llu and addr_line %llu\n", addr, addr_line);
  // printf("Store_buffer::remove(): before scb.size() %ld and scb_clean%d\n", scb_lines_map.size(), scb_clean);

  // Removes the element from the hashmap named 'scb_map' with key erase(key)
  // The erase() method typically returns the number of elements removed (0 or 1 when erasing by key)
  // or an iterator i to the element following the erased one (when erasing by iterator).
  auto it = scb_lines_map.find(addr_line);
  // if (!(it == scb_lines_map.end()) && !it->second.is_waiting_wb()) {
  if (!(it == scb_lines_map.end())) {
    // printf("Store_buffer::removei_spec_load::Found spec load addr %llu and addr_line %llu\n", addr, addr_line);
    scb_lines_map.erase(addr_line);
    // printf("Store_buffer::remove::Removing spec load addr %llu and addr_line %llu\n", addr, addr_line);
    // printf("Store_buffer::remove(): After scb.size() %ld\n", scb_lines_map.size());
  } else {
    // printf("Store_buffer::remove::Found NOT spec load addr %llu and addr_line %llu scb\n", addr, addr_line);
    // printf("Store_buffer::remove(): After scb.size() %ld\n", scb_lines_map.size());
  }
}
bool Store_buffer::is_clean_disp(Dinst* dinst) {
  /*spec_load removed from scb*/

  // printf("Store_buffer::::is_clean_disp:: Entering inst  %llu\n", dinst->getID());
  // I(scb_lines_num);
  Addr_t addr      = dinst->getAddr();
  Addr_t addr_line = calc_line(addr);
  auto   it        = scb_lines_map.find(addr_line);
  if (!(it == scb_lines_map.end()) && it->second.is_clean()) {
    return true;
  } else {
    return false;
  }
}

void Store_buffer::set_clean_scb(Dinst* dinst) {
  /*spec_load removed from scb*/

  // printf("Store_buffer:set_clean_scb:: inst  %llu\n", dinst->getID());
  // int scb_clean = this->get_clean_num();
  // printf("Store_buffer::set_clean_scb::: Before scb.size() %ld and scb_clean%d\n", scb_lines_map.size(), scb_clean);
  // printf("Store_buffer::set_clean_scb:: Before scb_clean is  %d \n", scb_clean);
  // I(scb_lines_num);
  Addr_t addr      = dinst->getAddr();
  Addr_t addr_line = calc_line(addr);
  auto   it        = scb_lines_map.find(addr_line);
  if ((it == scb_lines_map.end())) {
    // not found in scb
    // printf("Store_buffer::set_clean_scb::NOT found dinst  %llu\n", dinst->getID());
    // printf("Store_buffer::set_clean::addr %llu and addr_line %llu\n", addr, addr_line);
  } else {
    // printf("Store_buffer::set_clean::dinst  found  %llu\n", dinst->getID());
    // printf("Store_buffer::set_clean::addr %llu and addr_line %llu\n", addr, addr_line);
    it->second.set_clean();
  }
  // int scb_clean_after = this->get_clean_num();
  // printf("Store_buffer::set_clean_scb::: AFter scb.size() %ld and scb_clean%d\n", scb_lines_map.size(), scb_clean_after);
  // printf("Store_buffer::set_clean_scb:: After scb_clean is  %d \n", scb_clean);
}

void Store_buffer::add_st(Dinst* dinst) {
  auto st_addr = dinst->getAddr();
  // I(can_accept_st(st_addr));
  // printf("Store_buffer::add_st::Entering store add_st in scb for dinst  %llu\n", dinst->getID());

  auto st_addr_line = calc_line(st_addr);
  // printf("Store_buffer::add_st::add_st in scb for st_addr %llu and st_addr_line  %llu\n", st_addr, st_addr_line);
  auto it = scb_lines_map.find(st_addr_line);
  // scb does not has the addr : new entry in map 'scb_map'
  if (it == scb_lines_map.end()) {
    // printf("Store_buffer::add_st::In scb No entry found for store st_addr %llu and st_addr_line %llu\n", st_addr, st_addr_line);
    // if ((static_cast<int>(scb_lines_map.size()) +  >= scb_size) {
    // int scb_clean = this->get_clean_num();
    // printf("Store_buffer::add_st:: Before remove_clean() st_addr %llu and scb.size() %zu and scb_clean  %d\n",
           // st_addr,
           // scb_lines_map.size(),
           // scb_clean);
    if (static_cast<int>(scb_lines_map.size()) > scb_size) {
      // printf("Store_buffer::add_st:: remove_clean st_addr %llu and st_addr_line %llu\n", st_addr, st_addr_line);
      remove_clean();
    }

    Store_buffer_line line;

    line.init(line_size, st_addr_line);
    line.add_st(calc_offset(st_addr));
    I(line.state == Store_buffer_line::State::Uncoherent);

    // printf("Store_buffer::add_st::Inserting new entry for store st_addr  %llu\n", st_addr_line);
    scb_lines_map.insert({st_addr_line, line});
    line.set_waiting_wb();

    if (dinst->isTransient()) {
      it->second.set_transient();
    }

    CallbackBase* cb = ownership_doneCB::create(this, st_addr);
    // if (dl1 && !dinst->isTransient()) {
    /* isspec= transient+non_random_spec*/
    // if (dl1 && !dinst->is_spec()) {
    // CallbackBase* cb = ownership_doneCB::create(this, st_addr);
    if (dl1) {
      // printf(
          // "Store_buffer::add_st::SCB new entry for the store addr +Sending the store to cache for st_addr %llu and st_addr_line  "
          // "%llu\n",
          // st_addr,
          // st_addr_line);
      MemRequest::sendReqWrite(dl1, dinst->has_stats(), st_addr, dinst->getPC(), cb);
    } else {
      // dinst->set_write_scb_r();
      // if !dl1:
      cb->schedule(1);
    }

    // fmt::print("scb::add_st {} no pending st for addr 0x{}\n", dinst->getID(), st_addr);

    //--scb_clean_lines;
    return;
  }
  // scb already have the address beforehand in map 'scb_map': duplicate entry
  // printf("Store_buffer::add_st::SCB already have this addr for store st_addr %llu and st_addr_line %llu\n",
         // st_addr,
         // calc_line(st_addr));
  it->second.add_st(calc_offset(st_addr));
  if (it->second.is_waiting_wb()) {
    // printf(
        // "Store_buffer::add_st::WaitingPending for writeback to SCB from cache + already have this addr for store st_addr %llu and "
        // "st_addr_line %llu\n",
        // st_addr,
        // calc_line(st_addr));
    // fmt::print("scb::add_st {} with pending WB for addr 0x{}\n", dinst->getID(), st_addr);
    return;  // DONE
  }
  // FIX
  auto it_found = scb_lines_map.find(st_addr_line);
  if (it_found != scb_lines_map.end()) {
    // FIXEND

    it->second.set_waiting_wb();
  }
  // if (dl1 && !dinst->is_spec()) {
  CallbackBase* cb = ownership_doneCB::create(this, st_addr);
  // auto *cb = ownership_doneCB::create(this, st_addr);
  if (dl1) {
    // printf(
        // "Store_buffer::add_st::SCB already have this addr+Sending the store to cache for store st_addr %llu and st_addr_line "
        // "%llu\n",
        // st_addr,
        // calc_line(st_addr));
    MemRequest::sendReqWrite(dl1, dinst->has_stats(), st_addr, dinst->getPC(), cb);
    // MemRequest::sendReqWrite(dl1, dinst->has_stats(), st_addr, dinst->getPC(), cb);
  } else {
    cb->schedule(1);
    // dinst->set_write_scb_r();
  }

  // fmt::print("scb::add_st {} clean WB for addr 0x{}\n", dinst->getID(), st_addr);
}

void Store_buffer::ownership_done(Addr_t st_addr) {
  auto st_addr_line = calc_line(st_addr);

  // printf("Store_buffer::ownership_done:: Entering in scb for st_addr  %llu and st_addr_line %llu\n", st_addr, st_addr_line);
  auto it = scb_lines_map.find(st_addr_line);
  if (it != scb_lines_map.end()) {
    // I(it->second.is_waiting_wb());
    it->second.set_clean();
    // printf("Store_buffer::ownership_done:: Leaving from scb for st_addr  %llu\n", st_addr);
  }
}

bool Store_buffer::is_ld_forward(Addr_t addr) const {
  const auto it = scb_lines_map.find(calc_line(addr));
  if (it == scb_lines_map.end()) {
    return false;
  }

  return it->second.is_ld_forward(calc_offset(addr));
}

bool Store_buffer::find(Dinst* dinst) {
  auto st_addr      = dinst->getAddr();
  auto st_addr_line = calc_line(st_addr);
  auto it           = scb_lines_map.find(st_addr_line);
  // end is the iterator position: found if(it!=end position)
  if (it == scb_lines_map.end()) {
    return false;
  }
  return true;
}
