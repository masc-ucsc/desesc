// See LICENSE for details.

#include "store_buffer.hpp"

#include "MemRequest.h"
#include "config.hpp"

Store_buffer::Store_buffer(Hartid_t hid) {
  auto l1_sec = Config::get_string("soc", "core", hid, "dl1");

  line_size           = Config::get_power2(l1_sec, "line_size");
  scb_clean_lines     = 0;
  line_size_addr_bits = log2i(line_size);
  line_size_mask      = line_size - 1;
  scb_size            = Config::get_integer(l1_sec, "scb_size", 1, 2048);
}

bool Store_buffer::can_accept_st(Addr_t st_addr) const {
  if ((lines.size() - scb_clean_lines) < scb_size)
    return true;

  auto it = lines.find(calc_line(st_addr));
  return it != lines.end();
}

void Store_buffer::remove_clean() {
  I(scb_clean_lines);

  // FIXME: instead of removing all, randomly remove one
  for (auto it = lines.begin(); it != lines.end(); ++it) {
    if (it->second.is_clean()) {
      I(scb_clean_lines);
      --scb_clean_lines;

      lines.erase(it++);
    }
  }
}

void Store_buffer::add_st(Dinst *dinst) {
  Addr_t st_addr = dinst->getAddr();
  I(can_accept_st(st_addr));

  auto st_addr_line = calc_line(st_addr);

  auto it = lines.find(st_addr_line);
  if (it == lines.end()) {
    if ((lines.size() + scb_clean_lines) >= scb_size) {
      remove_clean();
    }

    Store_buffer_line line;

    line.init(line_size);
    line.add_st(calc_offset(st_addr));
    I(line.state == Store_buffer_line::State::Uncoherent);

    lines.insert({st_addr_line, line});

    line.set_waiting_wb();

    CallbackBase *cb = ownership_doneCB::create(this, st_addr);
    MemRequest::sendReqWrite(dl1, dinst->has_stats(), st_addr, dinst->getPC(), cb);
    return;
  }

  it->second.add_st(calc_offset(st_addr));
  if (it->second.is_waiting_wb())
    return;  // DONE

  it->second.set_waiting_wb();
  MemRequest::sendReqWrite(dl1, dinst->has_stats(), st_addr, dinst->getPC(), ownership_doneCB::create(this, st_addr));
}

void Store_buffer::ownership_done(Addr_t st_addr) {
  auto st_addr_line = calc_line(st_addr);

  auto it = lines.find(st_addr_line);
  I(it != lines.end());

  ++scb_clean_lines;
  it->second.set_clean();
}

bool Store_buffer::is_ld_forward(Addr_t addr) const {
  const auto it = lines.find(calc_line(addr));
  if (it == lines.end())
    return false;

  return it->second.is_ld_forward(calc_offset(addr));
}
