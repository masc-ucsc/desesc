// See LICENSE for details.

#include "emul_dromajo.hpp"

#include <stdio.h>

Emul_dromajo::Emul_dromajo(const std::string &sect) : Emul_base(sect)
{
    fmt::print("emul_dromajo successfully constructed! found section {}\n", sect);
}

Emul_dromajo::~Emul_dromajo()
/* Emul_dromajo destructor */
{}

Dinst *Emul_dromajo::peek(Hartid_t fid) {
    uint64_t last_pc = virt_machine_get_pc(machine, fid);
    uint32_t insn_raw = -1;
    (void)riscv_read_insn(machine->cpu_state[fid], &insn_raw, last_pc);
    Instruction esesc_insn;

    Opcode opcode  = (Opcode)(insn_raw & 0x7F);
    RegType src1   = (RegType)((insn_raw >> 15) & 0x1F);
    RegType src2   = (RegType)((insn_raw >> 20) & 0x1F);
    RegType dst1   = (RegType)((insn_raw >> 7)  & 0x1F);
    RegType dst2   = LREG_R0;	// risc-v cant have a second rd(?)

    esesc_insn.set(opcode, src1, src2, dst1, dst2);
    Addr_t address = 0; // is this just for jumps/branches or do we care about it for mem access too?
    return Dinst::create(&esesc_insn, last_pc, address, fid, false); // do we keep stats???
}

void Emul_dromajo::execute(Hartid_t fid) {
    // no trace generated, only instruction executed
    // do we care about instruction traces??
    virt_machine_run(machine, fid);
}

Hartid_t Emul_dromajo::get_num() const { return num; }

bool Emul_dromajo::is_sleeping(Hartid_t fid) const { fmt::print("called is_sleeping on hartid {}\n", fid); return true; }

// TO-DO: Config::init(dromajo.cfg) ... as started below:
// auto bootroom  = Config::get_string("emul", "bootroom");
