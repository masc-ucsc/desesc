// See LICENSE for details.

#pragma once

#include <cstdint>
#include <vector>

#include "absl/container/flat_hash_map.h"
#include "dinst.hpp"
#include "callback.hpp"
#include "opcode.hpp"

class MemObj;  // To break circular dependencies

class Store_buffer_line {
public:
  // NOTE: Invalid not used because when invalid it is removed from the map
  enum class State { Uncoherent, Modified, Invalid, Clean };  // UMIC

  State state;

  std::vector<bool> word_present;  // FIXME: dinst does byte info

  Addr_t line_addr;

  Store_buffer_line() { state = State::Invalid; }

  void init(size_t line_size) {
    I(state == State::Invalid);
    word_present.assign(line_size >> 2, false);
    state = State::Invalid;
  }
  void set_waiting_wb() { state = State::Uncoherent; }

  void add_st(Addr_t addr_off) {
    I((addr_off >> 2) < word_present.size());  // pass only the line offset
    word_present[addr_off >> 2] = true;
  }

  bool is_ld_forward(Addr_t addr_off) const { return word_present[addr_off >> 2]; }

  void set_clean() { state = State::Clean; }

  bool is_clean() const { return state == State::Clean; }
  bool is_waiting_wb() const { return state == State::Uncoherent; }
};

class Store_buffer {
protected:
  MemObj* dl1;

  // FA structure, so a map is fine
  absl::flat_hash_map<Addr_t, Store_buffer_line> lines;

  size_t scb_size;
  size_t scb_clean_lines;
  size_t line_size;
  size_t line_size_addr_bits;
  size_t line_size_mask;

  Addr_t calc_line(Addr_t addr) const { return addr >> line_size_addr_bits; }
  Addr_t calc_offset(Addr_t addr) const { return addr & line_size_mask; }

  void remove_clean();

public:
  void ownership_done(Addr_t addr);
  using ownership_doneCB = CallbackMember1<Store_buffer, Addr_t, &Store_buffer::ownership_done>;

  Store_buffer(Hartid_t hid);
  ~Store_buffer() {}

  bool can_accept_st(Addr_t st_addr) const;
  void add_st(Dinst* dinst);

  bool is_ld_forward(Addr_t ld_addr) const;
};
