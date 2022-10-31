/*
 * Top-level driver
 *
 * Copyright (C) 2018,2019, Esperanto Technologies Inc.
 *
 * Licensed under the Apache License, Version 2.0 (the "License")
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *   http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */
#include "dromajo.h"

#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <getopt.h>
#include <inttypes.h>
#include <net/if.h>
#include <signal.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <termios.h>
#include <time.h>
#include <unistd.h>

//#define REGRESS_COSIM 1
#ifdef REGRESS_COSIM
#include "dromajo_cosim.h"
#endif

#define BRANCHPROF
#ifdef BRANCHPROF
        FILE* pc_trace;
        
void print_branch_info (uint64_t last_pc, uint32_t insn_raw)
{
        static uint64_t last_last_pc;
	static uint8_t branch_flag = 0;
	//for (int i = 0; i < m->ncpus; ++i)
	{
 		
 		if (branch_flag)
 		{
 			if (last_pc-last_last_pc == 4)
 				fprintf (pc_trace, "%32s\n", "Not Taken Branch");
 			else
 				fprintf (pc_trace, "%32s\n", "Taken Branch");
 			branch_flag = 0;
 		}
 			
 		fprintf (pc_trace, "%20lx\t|%20x\t", last_pc, insn_raw);
 		if (insn_raw < 0x100)
 		{
 			fprintf (pc_trace, "\t|");
 		}
 		else
 		{
 			fprintf (pc_trace, "|");
 		}
 					
 		if ( ((insn_raw & 0x7fff) == 0x73)) 
 		{
 			if (( ((insn_raw & 0xffffff80) == 0x0)) ) 				// ECall
 			{
 				fprintf (pc_trace, "%32s\n", "ECALL type");
 			}
 			else if ( (insn_raw == 0x100073) || (insn_raw == 0x200073) || (insn_raw == 0x30200073) || (insn_raw == 0x7b200073))		//EReturn
 			{
 				fprintf (pc_trace, "%32s\n", "ERET type");
 			}
 		}
 		
 		else if (((insn_raw & 0x70) == 0x60))
 		{
 			if (((insn_raw & 0xf) == 0x3))
 			{
 				branch_flag = 1;
 			}
 			else // Jump
 			{
 				if((insn_raw & 0xf) == 0x7)
 				{
 					if  (((insn_raw & 0xf80)>>7) == 0x0)    
 					{
 						fprintf (pc_trace, "%32s\n", "Return");
 					}
 					else
 					{
 						fprintf (pc_trace, "%32s\n", "Reg based Fxn Call");
 					}
 				}
 				else
 				{
 					fprintf (pc_trace, "%32s\n", "PC relative Fxn Call");
 				}
 			}
 		}
 		else // Non CTI 
 		{
 			fprintf (pc_trace, "%32s\n", "Non - CTI");
 		}
 			
 		//fprintf (pc_trace, "\n");
 		last_last_pc = last_pc;
 	}
}
#endif

#ifdef SIMPOINT_BB
FILE *simpoint_bb_file = nullptr;

int simpoint_step(RISCVMachine *m, int hartid) {
    assert(hartid == 0);  // Only single core for simpoint creation

    static uint64_t ninst = 0;  // ninst in BB
    ninst++;

    if (simpoint_bb_file == 0) {  // Creating checkpoints mode

        assert(!m->common.simpoints.empty());

        auto &sp = m->common.simpoints[m->common.simpoint_next];
        if (ninst > sp.start) {
            char str[100];
            sprintf(str, "sp%d", sp.id);
            virt_machine_serialize(m, str);

            m->common.simpoint_next++;
            if (m->common.simpoint_next == m->common.simpoints.size()) {
                return 0;  // notify to terminate nicely
            }
        }
        return 1;
    }

    // Creating bb trace mode
    assert(m->common.simpoints.empty());

    uint64_t                                 pc            = virt_machine_get_pc(m, hartid);
    static uint64_t                          next_bbv_dump = UINT64_MAX;
    static std::unordered_map<uint64_t, int> bbv;
    static std::unordered_map<uint64_t, int> pc2id;
    static int                               next_id = 1;
    if (m->common.maxinsns <= next_bbv_dump) {
        if (m->common.maxinsns > SIMPOINT_SIZE)
            next_bbv_dump = m->common.maxinsns - SIMPOINT_SIZE;
        else
            next_bbv_dump = 0;

        if (bbv.size()) {
            fprintf(simpoint_bb_file, "T");
            for (const auto ent : bbv) {
                auto it = pc2id.find(ent.first);
                int  id = 0;
                if (it == pc2id.end()) {
                    id = next_id;
                    next_id++;
                    pc2id[ent.first] = next_id;
                } else {
                    id = it->second;
                }

                fprintf(simpoint_bb_file, ":%d:%d ", id, ent.second);
            }
            fprintf(simpoint_bb_file, "\n");
            fflush(simpoint_bb_file);
            bbv.clear();
        }
    }

    static uint64_t last_pc = 0;
    if ((last_pc + 2) != pc && (last_pc + 4) != pc) {
        bbv[last_pc] += ninst;
        // fprintf(simpoint_bb_file,"xxxBB 0x%" PRIx64 " %d\n", pc, ninst);
        ninst = 0;
    }
    last_pc = pc;

    return 1;
}
#endif

int iterate_core(RISCVMachine *m, int hartid) {
    if (m->common.maxinsns-- <= 0)
        /* Succeed after N instructions without failure. */
        return 0;

    RISCVCPUState *cpu = m->cpu_state[hartid];

    /* Instruction that raises exceptions should be marked as such in
     * the trace of retired instructions.
     */
    uint64_t last_pc  = virt_machine_get_pc(m, hartid);
    int      priv     = riscv_get_priv_level(cpu);
    uint32_t insn_raw = -1;
    (void)riscv_read_insn(cpu, &insn_raw, last_pc);
 
#ifdef BRANCHPROF
	for (int i = 0; i < m->ncpus; ++i)
	{
		print_branch_info (last_pc, insn_raw);
	}
#endif // BRANCHPROF

    int keep_going = virt_machine_run(m, hartid);
    if (last_pc == virt_machine_get_pc(m, hartid))
        return 0;

    if (m->common.trace) {
        --m->common.trace;
        return keep_going;
    }

    fprintf(dromajo_stderr,
            "%d %d 0x%016" PRIx64 " (0x%08x)",
            hartid,
            priv,
            last_pc,
            (insn_raw & 3) == 3 ? insn_raw : (uint16_t)insn_raw);

    int iregno = riscv_get_most_recently_written_reg(cpu);
    int fregno = riscv_get_most_recently_written_fp_reg(cpu);

    if (cpu->pending_exception != -1)
        fprintf(dromajo_stderr,
                " exception %d, tval %016" PRIx64,
                cpu->pending_exception,
                riscv_get_priv_level(cpu) == PRV_M ? cpu->mtval : cpu->stval);
    else if (iregno > 0)
        fprintf(dromajo_stderr, " x%2d 0x%016" PRIx64, iregno, virt_machine_get_reg(m, hartid, iregno));
    else if (fregno >= 0)
        fprintf(dromajo_stderr, " f%2d 0x%016" PRIx64, fregno, virt_machine_get_fpreg(m, hartid, fregno));
    else
        for (int i = 31; i >= 0; i--)
            if (cpu->most_recently_written_vregs[i]) {
                fprintf(dromajo_stderr, " v%2d 0x", i);
                for (int j = VLEN / 8 - 1; j >= 0; j--) {
                    fprintf(dromajo_stderr, "%02" PRIx8, cpu->v_reg[i][j]);
                }
            }


    putc('\n', dromajo_stderr);

    return keep_going;
}

static double execution_start_ts;
static uint64_t *execution_progress_meassure;


static void sigintr_handler(int dummy) {
    double t = get_current_time_in_seconds();
    fprintf(dromajo_stderr, "Simulation speed: %5.2f MIPS (single-core)\n",
            1e-6 * *execution_progress_meassure / (t - execution_start_ts));
    exit(1);
}

int main(int argc, char **argv) {
    const char *port_name = NULL;
    int         port_num = 0;
    for (;;) {
        int option_index = 0;
        // clang-format off
        static struct option long_options[] = {
            {"gdbinit",                     required_argument, 0,  'G' } // CFG
        };
        // clang-format on

        int c = getopt_long(argc, argv, "", long_options, &option_index);
        if (c == -1) {
            break;
        }
        switch (c) {
            case 'G':
                port_name = strdup(optarg);
                port_num  = atoi(port_name);
                break;
            default: break;
        }
    };

#ifdef REGRESS_COSIM
    dromajo_cosim_state_t *costate = 0;
    costate                        = dromajo_cosim_init(argc, argv);

    if (!costate)
        return 1;

    while (!dromajo_cosim_step(costate, 0, 0, 0, 0, 0, false))
        ;
    dromajo_cosim_fini(costate);
#else

    RISCVMachine *m = virt_machine_main(argc, argv);
    if (!m)
        return 1;

    if (port_num)
      gdb_stub(m, port_num);

#ifdef SIMPOINT_BBhartid
    if (m->common.simpoints.empty()) {
        simpoint_bb_file = fopen("dromajo_simpoint.bb", "w");
        if (simpoint_bb_file == nullptr) {
            fprintf(dromajo_stderr, "\nerror: could not open dromajo_simpoint.bb for dumping trace\n");
            exit(-3);
        }
    }
#endif

#ifdef BRANCHPROF
        pc_trace = fopen("pc_trace.txt", "w+");
        if (pc_trace == nullptr) 
        {
            	fprintf(dromajo_stderr, "\nerror: could not open pc_trace.txt for dumping trace\n");
            	exit(-3);
        }
        else
        {
        	fprintf(dromajo_stderr, "\nOpened dromajo_simpoint.bb for dumping trace\n");
        	fprintf (pc_trace, "%20s\t\t|%20s\t|%32s\n", "PC", "Instruction", "Instructiontype");
        }
    
#endif

    execution_start_ts = get_current_time_in_seconds();
    execution_progress_meassure = &m->cpu_state[0]->minstret;
    signal(SIGINT, sigintr_handler);

    int keep_going;
    do {
      keep_going = 0;
      for (int i = 0; i < m->ncpus; ++i) keep_going |= iterate_core(m, i);
#ifdef SIMPOINT_BB
      if (roi_region) {
        if (!simpoint_step(m, 0))
          break;
      }
#endif
/*#ifdef BRANCHPROF
	for (int i = 0; i < m->ncpus; ++i)
	{
		uint64_t pc            = virt_machine_get_pc(m, i);
 		fprintf (pc_trace, "pc = %"PRIu64"\n", pc);
 	}
#endif*/
    } while (keep_going);

    double t = get_current_time_in_seconds();

    for (int i = 0; i < m->ncpus; ++i) {
      int benchmark_exit_code = riscv_benchmark_exit_code(m->cpu_state[i]);
      if (benchmark_exit_code != 0) {
        fprintf(dromajo_stderr, "\nBenchmark exited with code: %i \n", benchmark_exit_code);
        return 1;
      }
    }

    fprintf(dromajo_stderr, "Simulation speed: %5.2f MIPS (single-core)\n",
            1e-6 * *execution_progress_meassure / (t - execution_start_ts));

    fprintf(dromajo_stderr, "\nPower off.\n");

    virt_machine_end(m);
#ifdef BRANCHPROF
    fclose (pc_trace);
#endif

#endif

#ifdef LIVECACHE
#if 0
    // LiveCache Dump
    uint64_t addr_size;
    uint64_t *addr = m->llc->traverse(addr_size);

    for (uint64_t i = 0u; i < addr_size; ++i) {
        printf("addr:%llx %s\n", (unsigned long long)addr[i], (addr[i] & 1) ? "ST" : "LD");
    }
#endif
    delete m->llc;
#endif

    return 0;
}


