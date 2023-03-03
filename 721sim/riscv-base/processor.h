// See LICENSE for license details.
#ifndef _RISCV_PROCESSOR_H
#define _RISCV_PROCESSOR_H

#include "decode.h"
#include "config.h"
#include <cstring>
#include <cstdio>
#include <vector>
#include <map>

class processor_t;
class mmu_t;
typedef reg_t (*insn_func_t)(processor_t*, insn_t, reg_t);
class sim_t;
class trap_t;
class extension_t;
class disassembler_t;
class debug_buffer_t;

struct serialize_t {};

struct insn_desc_t
{
  uint32_t match;
  uint32_t mask;
  insn_func_t rv32;
  insn_func_t rv64;
};

struct commit_log_reg_t
{
  reg_t addr;
  reg_t data;
};

// architectural state of a RISC-V hart
struct state_t
{
  void reset();
  void dump(FILE* file);

  reg_t pc;
  regfile_t<reg_t, NXPR, true> XPR;
  regfile_t<freg_t, NFPR, false> FPR;

  // control and status registers
  reg_t epc;
  reg_t badvaddr;
  reg_t evec;
  reg_t ptbr;
  reg_t pcr_k0;
  reg_t pcr_k1;
  reg_t cause;
  reg_t tohost;
  reg_t fromhost;
  reg_t count;
  uint32_t compare;
  uint32_t sr; // only modify the status register using set_pcr()
  uint32_t fflags;
  uint32_t frm;

  reg_t load_reservation;

#ifdef RISCV_ENABLE_COMMITLOG
  commit_log_reg_t log_reg_write;
#endif
};

// this class represents one processor in a RISC-V machine.
class processor_t
{
public:
  processor_t();
  processor_t(sim_t* _sim, mmu_t* _mmu, uint32_t _id);
  virtual ~processor_t();

  void set_debug(bool value);
  bool get_debug();
  void set_checker(bool value);
  bool get_checker();
  void set_proc_type(const char*);
  const char* get_proc_type();
  void set_histogram(bool value);
  void reset(bool value);
  virtual void step(size_t n,size_t& instret); // run for n cycles
  void deliver_ipi(); // register an interprocessor interrupt
  bool running() { return run; }
  void set_pcr(int which, reg_t val);
  void set_fromhost(reg_t val);
  void set_interrupt(int which, bool on);
  reg_t get_pcr(int which);
  mmu_t* get_mmu() { return mmu; }
  state_t* get_state() { return &state; }
  extension_t* get_extension() { return ext; }
  void yield_load_reservation() { state.load_reservation = (reg_t)-1; }
  virtual void update_histogram(size_t pc);

  void register_insn(insn_desc_t);
  void register_extension(extension_t*);
 #ifdef RISCV_MICRO_CHECKER
  virtual reg_t get_rs(reg_t rs, reg_t pc, operand_t type);
  virtual reg_t get_frs(reg_t rs, reg_t pc, operand_t type);
#endif

 debug_buffer_t* get_pipe(){return pipe;}

protected:
  sim_t* sim;
  mmu_t* mmu; // main memory is always accessed via the mmu
  extension_t* ext;
  disassembler_t* disassembler;
  state_t state;
  uint32_t id;
  bool run; // !reset
  bool debug;
  bool checker;
  const char* proc_type;
  bool histogram_enabled;
  bool rv64;
  bool serialized;

  debug_buffer_t* pipe;

  std::map<size_t,size_t> pc_histogram;

  void serialize(); // collapse into defined architectural state
  void take_interrupt(); // take a trap if any interrupts are pending
  virtual reg_t take_trap(trap_t& t, reg_t epc); // take an exception
  virtual void disasm(insn_t insn); // disassemble and print an instruction
  virtual void disasm(insn_t insn,reg_t pc); // disassemble and print an instruction
  virtual void set_pipe(debug_buffer_t* _pipe);
  virtual inline reg_t get_pc(){return state.pc;}

  friend class sim_t;
  friend class mmu_t;
  friend class extension_t;

private:
  void build_opcode_map();
  insn_func_t decode_insn(insn_t insn);
  std::vector<insn_desc_t> instructions;
  std::vector<insn_desc_t*> opcode_map;
  std::vector<insn_desc_t> opcode_store;

};

reg_t illegal_instruction(processor_t* p, insn_t insn, reg_t pc);

#define REGISTER_INSN(proc, name, match, mask) \
  extern reg_t rv32_##name(processor_t*, insn_t, reg_t); \
  extern reg_t rv64_##name(processor_t*, insn_t, reg_t); \
  proc->register_insn((insn_desc_t){match, mask, rv32_##name, rv64_##name});

#endif
