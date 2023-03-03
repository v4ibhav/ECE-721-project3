// See LICENSE for license details.

#include "processor.h"
#include "extension.h"
#include "common.h"
#include "config.h"
#include "sim.h"
#include "htif.h"
#include "disasm.h"
#include <cinttypes>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <assert.h>
#include <limits.h>
#include <stdexcept>
#include <algorithm>
#include "debug.h"

#undef STATE
#define STATE state

extern bool logging_on;

processor_t::processor_t(sim_t* _sim, mmu_t* _mmu, uint32_t _id)
  : sim(_sim), mmu(_mmu), ext(NULL), disassembler(new disassembler_t),
    id(_id), run(false), debug(false), serialized(false)
{
  reset(true);
  mmu->set_processor(this);

  #define DECLARE_INSN(name, match, mask) REGISTER_INSN(this, name, match, mask)
  #include "encoding.h"
  #undef DECLARE_INSN
  build_opcode_map();
}

processor_t::~processor_t()
{
#ifdef RISCV_ENABLE_HISTOGRAM
  if (histogram_enabled)
  {
    fprintf(stderr, "PC Histogram size:%lu\n", pc_histogram.size());
    for(auto iterator = pc_histogram.begin(); iterator != pc_histogram.end(); ++iterator) {
      fprintf(stderr, "%0lx %lu\n", (iterator->first << 2), iterator->second);
    }
  }
#endif

  delete disassembler;
}

void state_t::reset()
{
  // the ISA guarantees on boot that the PC is 0x2000 and the the processor
  // is in supervisor mode, and in 64-bit mode, if supported, with traps
  // and virtual memory disabled.
  sr = SR_S | SR_S64 | SR_U64;
  pc = 0x2000;
  //pc = 0x10000;

  // the following state is undefined upon boot-up,
  // but we zero it for determinism
  XPR.reset();
  FPR.reset();

  epc = 0;
  badvaddr = 0;
  evec = 0;
  ptbr = 0;
  pcr_k0 = 0;
  pcr_k1 = 0;
  cause = 0;
  tohost = 0;
  fromhost = 0;
  count = 0;
  compare = 0;
  fflags = 0;
  frm = 0;

  load_reservation = -1;
}

void state_t::dump(FILE* file)
{
  fprintf(file,"pc       : 0x%8lx\t",pc              );
  fprintf(file,"epc      : 0x%8lx\t",epc             );
  fprintf(file,"badvaddr : 0x%8lx\t",badvaddr        );
  fprintf(file,"evec     : 0x%8lx\t",evec            );
  fprintf(file,"ptbr     : 0x%8lx\t",ptbr            );
  fprintf(file,"\n");
  fprintf(file,"pcr_k0   : 0x%8lx\t",pcr_k0          );
  fprintf(file,"pcr_k1   : 0x%8lx\t",pcr_k1          );
  fprintf(file,"cause    :   %8lu\t",cause           );
  fprintf(file,"tohost   : 0x%8lx\t",tohost          );
  fprintf(file,"fromhost : 0x%8lx\t",fromhost        );
  fprintf(file,"\n");
  fprintf(file,"count    :   %8lu\t",count           );
  fprintf(file,"compare  :   %8u\t" ,compare         );
  fprintf(file,"sr       : 0x%8x\t" ,sr              );
  fprintf(file,"fflags   : 0x%8x\t" ,fflags          );
  fprintf(file,"frm      : 0x%8x\t" ,frm             );
  fprintf(file,"load_resv:   %8lx\t",load_reservation);
  fprintf(file,"\n");
  fprintf(file,"\n");
}

void processor_t::set_debug(bool value)
{
  debug = value;
  if (ext)
    ext->set_debug(value);
}

bool processor_t::get_debug()
{
  return debug;
}

void processor_t::set_checker(bool value)
{
  checker = value;
}

bool processor_t::get_checker()
{
  return checker;
}

void processor_t::set_proc_type(const char* type)
{
  proc_type = type;
}

const char* processor_t::get_proc_type()
{
  return proc_type;
}

void processor_t::set_histogram(bool value)
{
  histogram_enabled = value;
}

void processor_t::reset(bool value)
{

  ifprintf(logging_on,stderr, "******* Resetting core ********** \n");
  if (run == !value)
    return;
  run = !value;

  state.reset(); // reset the core
  set_pcr(CSR_STATUS, state.sr);

  if (ext)
    ext->reset(); // reset the extension
}

//struct serialize_t {};

void processor_t::serialize()
{
  if (serialized)
    serialized = false;
  else
    serialized = true, throw serialize_t();
}

void processor_t::take_interrupt()
{
  int irqs = ((state.sr & SR_IP) >> SR_IP_SHIFT) & (state.sr >> SR_IM_SHIFT);
  if (likely(!irqs) || likely(!(state.sr & SR_EI)))
    return;

  for (int i = 0; ; i++)
    if ((irqs >> i) & 1)
      throw trap_t((1ULL << ((state.sr & SR_S64) ? 63 : 31)) + i);
}

static void commit_log(state_t* state, reg_t pc, insn_t insn)
{
#ifdef RISCV_ENABLE_COMMITLOG
  if (state->sr & SR_EI) {
    uint64_t mask = (insn.length() == 8 ? uint64_t(0) : (uint64_t(1) << (insn.length() * 8))) - 1;
    if (state->log_reg_write.addr) {
      fprintf(stderr, "0x%016" PRIx64 " (0x%08" PRIx64 ") %c%2" PRIu64 " 0x%016" PRIx64 "\n",
              pc,
              insn.bits() & mask,
              state->log_reg_write.addr & 1 ? 'f' : 'x',
              state->log_reg_write.addr >> 1,
              state->log_reg_write.data);
    } else {
      fprintf(stderr, "0x%016" PRIx64 " (0x%08" PRIx64 ")\n", pc, insn.bits() & mask);
    }
  }
  state->log_reg_write.addr = 0;
#endif
}

inline void processor_t::update_histogram(size_t pc)
{
#ifdef RISCV_ENABLE_HISTOGRAM
  size_t idx = pc >> 2;
  pc_histogram[idx]++;
#endif
}

#ifdef RISCV_MICRO_CHECKER
inline reg_t processor_t::get_rs(reg_t rs, reg_t pc, operand_t type){
      reg_t rdata = STATE.XPR[rs]; /* value is a func with side-effects */
      if(get_checker()){
        pipe->push_operand_actual(rs, type, rdata, pc);
      }
      return rdata;
}

inline reg_t processor_t::get_frs(reg_t rs, reg_t pc, operand_t type){
      reg_t rdata = STATE.FPR[rs]; /* value is a func with side-effects */
      if(get_checker()){
        pipe->push_operand_actual((rs + NXPR),type, rdata, pc);
      }
      return rdata;
}
#endif

//Scope of this function is just this file
static reg_t execute_insn(processor_t* p, reg_t pc, insn_fetch_t fetch)
{
  //#ifdef RISCV_MICRO_CHECKER
  //  if(p->get_checker()){
  //    p->get_pipe()->start();
  //  }
  //#endif
  //TODO: Push to debug buffer current PC, RS1, RS2 and all immediate values
  reg_t npc = fetch.func(p, fetch.insn, pc);
  //TODO: Push to debug buffer RD value and next PC
  commit_log(p->get_state(), pc, fetch.insn);
  p->update_histogram(pc);
  #ifdef RISCV_MICRO_CHECKER
    if(p->get_checker()){
	    p->get_pipe()->push_instr_actual(fetch.insn, 0, 0, pc, npc, 0, 0);
	    p->get_pipe()->push_state_actual(p->get_state(),false);
    }
  #endif
  return npc;
}

//Scope of this function is just this file
static void update_timer(state_t* state, size_t instret)
{
  uint64_t count0 = (uint64_t)(uint32_t)state->count;
  state->count += instret;
  uint64_t before = count0 - state->compare;
  if (int64_t(before ^ (before + instret)) < 0)
    state->sr |= (1 << (IRQ_TIMER + SR_IP_SHIFT));
}

//Scope of this function is just this file
static size_t next_timer(state_t* state)
{
  ifprintf(logging_on,stderr,"ISA_SIM next timer: %u\n",(state->compare - (uint32_t)state->count));
  return state->compare - (uint32_t)state->count;
}

void processor_t::step(size_t n,size_t& instret)
{
  //size_t instret = 0;
  instret = 0;
  reg_t pc = state.pc;
  mmu_t* _mmu = mmu;

  if (unlikely(!run || !n))
    return;
  n = std::min(n, next_timer(&state) | 1U);

  insn_fetch_t fetch;

  try
  {
    take_interrupt();

    if (unlikely(debug))
    {
      while (instret < n)
      {

        // Increment before executing so that excepting instructions are 
        // also counted, even instructions excepting at FETCH since these
        // are NOPs with exceptions in the micro_sim.
        instret++;
        // Entry needs to be started here as even fetch migth cause exceptions.
        #ifdef RISCV_MICRO_CHECKER
          if(get_checker()){
            get_pipe()->start();
          }
        #endif
        fetch = mmu->load_insn(pc);
        #ifdef RISCV_MICRO_DEBUG
          if(logging_on){
		        ifprintf(logging_on,stderr, "Commit Count: %lu ",get_state()->count);
            disasm(fetch.insn);
          }
        #endif

        pc = execute_insn(this, pc, fetch);

        ifprintf(logging_on,stderr,"RS1: %" PRIu64 " RS2: %" PRIu64 " RD: %" PRIu64 " STATUS: %u\n",STATE.XPR[fetch.insn.rs1()],STATE.XPR[fetch.insn.rs2()],STATE.XPR[fetch.insn.rd()],STATE.sr);
      }
    }
    else while (instret < n)
    {
      size_t idx = _mmu->icache_index(pc);
      auto ic_entry = _mmu->access_icache(pc);

      #ifdef RISCV_MICRO_CHECKER 
        #define ICACHE_ACCESS(idx) { \
          instret++; \
          if(get_checker()){ \
            get_pipe()->start(); \
          } \
          fetch = ic_entry->data; \
          ic_entry++; \
          if(logging_on){ \
            disasm(fetch.insn,pc); \
          } \
          pc = execute_insn(this, pc, fetch); \
          ifprintf(logging_on,stderr,"RS1: %" PRIu64 " RS2: %" PRIu64 " RD: %" PRIu64 " STATUS: %u\n",STATE.XPR[fetch.insn.rs1()],STATE.XPR[fetch.insn.rs2()],STATE.XPR[fetch.insn.rd()],STATE.sr); \
          if (instret == n) break; \
          if (idx == mmu_t::ICACHE_ENTRIES-1) break; \
          if (unlikely(ic_entry->tag != pc)) break; \
        }
      #else
        #define ICACHE_ACCESS(idx) { \
          fetch = ic_entry->data; \
          ic_entry++; \
          if(logging_on){ \
            disasm(fetch.insn,pc); \
          } \
          pc = execute_insn(this, pc, fetch); \
          ifprintf(logging_on,stderr,"RS1: %" PRIu64 " RS2: %" PRIu64 " RD: %" PRIu64 " STATUS: %u\n",STATE.XPR[fetch.insn.rs1()],STATE.XPR[fetch.insn.rs2()],STATE.XPR[fetch.insn.rd()],STATE.sr); \
          instret++; \
          if (instret == n) break; \
          if (idx == mmu_t::ICACHE_ENTRIES-1) break; \
          if (unlikely(ic_entry->tag != pc)) break; \
        }
      #endif

      switch (idx) {
        #include "icache.h"
      }
      ifprintf(logging_on,stderr,"Breaking out of ICACHE construct INSTRET = %lu\n",instret);
    }
  }
  catch(trap_t& t)
  {
    // Push instruction if it causes an exception.
    // Otherwise instruction will be pushed in execute_insn().
    #ifdef RISCV_MICRO_CHECKER
      if(get_checker()){
	      get_pipe()->push_instr_actual(fetch.insn, 0, 0, pc, pc, 0, 0);
      }
    #endif

    // Take the trap - Get PC on handler
    pc = take_trap(t, pc);

    #ifdef RISCV_MICRO_CHECKER
      if(get_checker()){
	      get_pipe()->push_exception_actual(pc);
	      get_pipe()->push_state_actual(&state,false);
      }
    #endif
  }
  catch(serialize_t& s) {
    // Push instruction if it causes an exception.
    // Otherwise instruction will be pushed in execute_insn().
    #ifdef RISCV_MICRO_CHECKER
      if(get_checker()){
	      get_pipe()->push_instr_actual(fetch.insn, 0, 0, pc, pc, 0, 0);
	      get_pipe()->push_state_actual(&state,false);
      }
    #endif
  }

  state.pc = pc;
  update_timer(&state, instret);
}

reg_t processor_t::take_trap(trap_t& t, reg_t epc)
{
  //TODO: Add this back
  //if (debug)
    ifprintf(logging_on,stderr, "core %3d: exception %s, epc 0x%016" PRIx64 "\n",
            id, t.name(), epc);

  // switch to supervisor, set previous supervisor bit, disable interrupts
  set_pcr(CSR_STATUS, (((state.sr & ~SR_EI) | SR_S) & ~SR_PS & ~SR_PEI) |
                      ((state.sr & SR_S) ? SR_PS : 0) |
                      ((state.sr & SR_EI) ? SR_PEI : 0));

  yield_load_reservation();
  state.cause = t.cause();
  state.epc = epc;
  t.side_effects(&state); // might set badvaddr etc.
  return state.evec;
}

void processor_t::deliver_ipi()
{
  if (run)
    set_pcr(CSR_CLEAR_IPI, 1);
}

void processor_t::disasm(insn_t insn)
{
  uint64_t bits = insn.bits() & ((1ULL << (8 * insn_length(insn.bits()))) - 1);
  fprintf(stderr, "core %3d: 0x%016" PRIx64 " (0x%08" PRIx64 ") %s\n",
          id, state.pc, bits, disassembler->disassemble(insn).c_str());
}

void processor_t::disasm(insn_t insn,reg_t pc)
{
  uint64_t bits = insn.bits() & ((1ULL << (8 * insn_length(insn.bits()))) - 1);
  fprintf(stderr, "core %3d: 0x%016" PRIx64 " (0x%08" PRIx64 ") %s\n",
          id, pc, bits, disassembler->disassemble(insn).c_str());
}

void processor_t::set_pipe(debug_buffer_t* _pipe)
{
  this->pipe = _pipe;
}


void processor_t::set_pcr(int which, reg_t val)
{
  switch (which)
  {
    case CSR_FFLAGS:
      state.fflags = val & (FSR_AEXC >> FSR_AEXC_SHIFT);
      break;
    case CSR_FRM:
      state.frm = val & (FSR_RD >> FSR_RD_SHIFT);
      break;
    case CSR_FCSR:
      state.fflags = (val & FSR_AEXC) >> FSR_AEXC_SHIFT;
      state.frm = (val & FSR_RD) >> FSR_RD_SHIFT;
      break;
    case CSR_STATUS:
      state.sr = (val & ~SR_IP) | (state.sr & SR_IP);
#ifndef RISCV_ENABLE_64BIT
      state.sr &= ~(SR_S64 | SR_U64);
#endif
#ifndef RISCV_ENABLE_FPU
      state.sr &= ~SR_EF;
#endif
      if (!ext)
        state.sr &= ~SR_EA;
      state.sr &= ~SR_ZERO;
      rv64 = (state.sr & SR_S) ? (state.sr & SR_S64) : (state.sr & SR_U64);
      mmu->flush_tlb();
      break;
    case CSR_EPC:
      state.epc = val;
      break;
    case CSR_EVEC:
      state.evec = val & ~3;
      break;
    case CSR_COUNT:
      state.count = val;
      break;
    case CSR_COUNTH:
      state.count = (val << 32) | (uint32_t)state.count;
      break;
    case CSR_COMPARE:
      serialize();
      set_interrupt(IRQ_TIMER, false);
      state.compare = val;
      break;
    case CSR_PTBR:
      state.ptbr = val & ~(PGSIZE-1);
      break;
    case CSR_SEND_IPI:
      sim->send_ipi(val);
      break;
    case CSR_CLEAR_IPI:
      set_interrupt(IRQ_IPI, val & 1);
      break;
    case CSR_SUP0:
      state.pcr_k0 = val;
      break;
    case CSR_SUP1:
      state.pcr_k1 = val;
      break;
    case CSR_TOHOST:
      if (state.tohost == 0)
        state.tohost = val;
      break;
    case CSR_FROMHOST:
      set_fromhost(val);
      break;
  }
}

void processor_t::set_fromhost(reg_t val)
{
  set_interrupt(IRQ_HOST, val != 0);
  state.fromhost = val;
  ifprintf(logging_on,stderr,"%s setting FROMHOST to %lu  STATUS = %u\n",proc_type,val,state.sr);
}

reg_t processor_t::get_pcr(int which)
{
  switch (which)
  {
    case CSR_FFLAGS:
      require_fp;
      return state.fflags;
    case CSR_FRM:
      require_fp;
      return state.frm;
    case CSR_FCSR:
      require_fp;
      return (state.fflags << FSR_AEXC_SHIFT) | (state.frm << FSR_RD_SHIFT);
    case CSR_STATUS:
      return state.sr;
    case CSR_EPC:
      return state.epc;
    case CSR_BADVADDR:
      return state.badvaddr;
    case CSR_EVEC:
      return state.evec;
    case CSR_CYCLE:
    case CSR_TIME:
    case CSR_INSTRET:
    case CSR_COUNT:
      serialize();
      return state.count;
    case CSR_CYCLEH:
    case CSR_TIMEH:
    case CSR_INSTRETH:
    case CSR_COUNTH:
      if (rv64)
        break;
      serialize();
      return state.count >> 32;
    case CSR_COMPARE:
      return state.compare;
    case CSR_CAUSE:
      return state.cause;
    case CSR_PTBR:
      return state.ptbr;
    case CSR_SEND_IPI:
    case CSR_CLEAR_IPI:
      return 0;
    case CSR_ASID:
      return 0;
    case CSR_FATC:
      mmu->flush_tlb();
      return 0;
    case CSR_HARTID:
      return id;
    case CSR_IMPL:
      return 1;
    case CSR_SUP0:
      return state.pcr_k0;
    case CSR_SUP1:
      return state.pcr_k1;
    case CSR_TOHOST:
      sim->get_htif()->tick(); // not necessary, but faster
      return state.tohost;
    case CSR_FROMHOST:
      sim->get_htif()->tick(); // not necessary, but faster
      return state.fromhost;
    case CSR_UARCH0:
    case CSR_UARCH1:
    case CSR_UARCH2:
    case CSR_UARCH3:
    case CSR_UARCH4:
    case CSR_UARCH5:
    case CSR_UARCH6:
    case CSR_UARCH7:
    case CSR_UARCH8:
    case CSR_UARCH9:
    case CSR_UARCH10:
    case CSR_UARCH11:
    case CSR_UARCH12:
    case CSR_UARCH13:
    case CSR_UARCH14:
    case CSR_UARCH15:
      return 0;
  }
  throw trap_illegal_instruction();
}

void processor_t::set_interrupt(int which, bool on)
{
  uint32_t mask = (1 << (which + SR_IP_SHIFT)) & SR_IP;
  if (on)
    state.sr |= mask;
  else
    state.sr &= ~mask;
}

reg_t illegal_instruction(processor_t* p, insn_t insn, reg_t pc)
{
  throw trap_illegal_instruction();
}

insn_func_t processor_t::decode_insn(insn_t insn)
{
  size_t mask = opcode_map.size()-1;
  insn_desc_t* desc = opcode_map[insn.bits() & mask]; 

  while ((insn.bits() & desc->mask) != desc->match)
    desc++;

  return rv64 ? desc->rv64 : desc->rv32;
}

void processor_t::register_insn(insn_desc_t desc)
{
  assert(desc.mask & 1);
  instructions.push_back(desc);
}

void processor_t::build_opcode_map()
{
  size_t buckets = -1;
  for (auto& inst : instructions)
    while ((inst.mask & buckets) != buckets)
      buckets /= 2;
  buckets++;

  struct cmp {
    decltype(insn_desc_t::match) mask;
    cmp(decltype(mask) mask) : mask(mask) {}
    bool operator()(const insn_desc_t& lhs, const insn_desc_t& rhs) {
      if ((lhs.match & mask) != (rhs.match & mask))
        return (lhs.match & mask) < (rhs.match & mask);
      return lhs.match < rhs.match;
    }
  };
  std::sort(instructions.begin(), instructions.end(), cmp(buckets-1));

  opcode_map.resize(buckets);
  opcode_store.resize(instructions.size() + 1);

  size_t j = 0;
  for (size_t b = 0, i = 0; b < buckets; b++)
  {
    opcode_map[b] = &opcode_store[j];
    while (i < instructions.size() && b == (instructions[i].match & (buckets-1)))
      opcode_store[j++] = instructions[i++];
  }

  assert(j == opcode_store.size()-1);
  opcode_store[j].match = opcode_store[j].mask = 0;
  opcode_store[j].rv32 = &illegal_instruction;
  opcode_store[j].rv64 = &illegal_instruction;
}

void processor_t::register_extension(extension_t* x)
{
  for (auto insn : x->get_instructions())
    register_insn(insn);
  build_opcode_map();
  for (auto disasm_insn : x->get_disasms())
    disassembler->add_insn(disasm_insn);
  if (ext != NULL)
    throw std::logic_error("only one extension may be registered");
  ext = x;
  x->set_processor(this);
}
