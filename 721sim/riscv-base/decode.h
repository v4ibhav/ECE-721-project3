// See LICENSE for license details.

#ifndef _RISCV_DECODE_H
#define _RISCV_DECODE_H

//#define RISCV_MICRO_CHECKER

#if (-1 != ~0) || ((-1 >> 1) != -1)
# error spike requires a two''s-complement c++ implementation
#endif

#include <cstdint>
#include <string.h>
#include "encoding.h"
#include "config.h"
#include "common.h"
#include <cinttypes>

typedef enum {
  RSRC1_OPERAND,
  RSRC2_OPERAND,
  RSRC3_OPERAND,
  RDST_OPERAND,
  MSRC_OPERAND,
  MDST_OPERAND,
  RSRC_A_OPERAND
} operand_t;

typedef int64_t sreg_t;
typedef uint64_t reg_t;
typedef uint64_t freg_t;
typedef uint64_t cycle_t;
typedef uint32_t word_t;
typedef int32_t  sword_t;

#define PRIsreg PRIi64 
#define PRIreg  PRIu64 
#define PRIxreg PRIx64 
#define PRIfreg PRIu64 
#define PRIcycle PRIu64 
#define PRIword  PRIu32 

const int NXPR = 32;
const int NFPR = 32;

#define FP_RD_NE  0
#define FP_RD_0   1
#define FP_RD_DN  2
#define FP_RD_UP  3
#define FP_RD_NMM 4

#define FSR_RD_SHIFT 5
#define FSR_RD   (0x7 << FSR_RD_SHIFT)

#define FPEXC_NX 0x01
#define FPEXC_UF 0x02
#define FPEXC_OF 0x04
#define FPEXC_DZ 0x08
#define FPEXC_NV 0x10

#define FSR_AEXC_SHIFT 0
#define FSR_NVA  (FPEXC_NV << FSR_AEXC_SHIFT)
#define FSR_OFA  (FPEXC_OF << FSR_AEXC_SHIFT)
#define FSR_UFA  (FPEXC_UF << FSR_AEXC_SHIFT)
#define FSR_DZA  (FPEXC_DZ << FSR_AEXC_SHIFT)
#define FSR_NXA  (FPEXC_NX << FSR_AEXC_SHIFT)
#define FSR_AEXC (FSR_NVA | FSR_OFA | FSR_UFA | FSR_DZA | FSR_NXA)

#define insn_size 4
#define insn_length(x) \
  (((x) & 0x03) < 0x03 ? 2 : \
   ((x) & 0x1f) < 0x1f ? 4 : \
   ((x) & 0x3f) < 0x3f ? 6 : \
   8)

typedef uint64_t insn_bits_t;
class insn_t
{
public:
  insn_t() = default;
  insn_t(insn_bits_t bits) : b(bits) {}
  insn_bits_t bits() { return b; }
  int length() { return insn_length(b); }
  int64_t i_imm() { return int64_t(b) >> 20; }
  int64_t s_imm() { return x(7, 5) + (xs(25, 7) << 5); }
  int64_t sb_imm() { return (x(8, 4) << 1) + (x(25,6) << 5) + (x(7,1) << 11) + (imm_sign() << 12); }
  int64_t u_imm() { return int64_t(b) >> 12 << 12; }
  int64_t uj_imm() { return (x(21, 10) << 1) + (x(20, 1) << 11) + (x(12, 8) << 12) + (imm_sign() << 20); }
  uint64_t rd() { return x(7, 5); }
  uint64_t rs1() { return x(15, 5); }
  uint64_t rs2() { return x(20, 5); }
  uint64_t rs3() { return x(27, 5); }
  uint64_t rm() { return x(12, 3); }
  uint64_t csr() { return x(20, 12); }
  uint64_t opcode() {return x(0, 7); }
  uint64_t funct3() {return x(12, 3); }
  uint64_t funct7() {return x(25, 7); }
  uint64_t funct12() {return x(20, 12); }
  uint64_t funct5() {return x(27, 5); }
  uint64_t fmt() {return x(25, 2); }
  uint64_t shamt() {return x(20, 6); }
  uint32_t ldst_size() {return (1 << x(12,2)); }
  bool     ldst_sign() {return !x(14,1); }  // unsigned if 1 signed if 0

private:
  insn_bits_t b;
  uint64_t x(int lo, int len) { return (b >> lo) & ((insn_bits_t(1) << len)-1); }
  uint64_t xs(int lo, int len) { return int64_t(b) << (64-lo-len) >> (64-len); }
  uint64_t imm_sign() { return xs(63, 1); }
};

template <class T, size_t N, bool zero_reg>
class regfile_t
{
public:
  void reset()
  {
    memset(data, 0, sizeof(data));
  }
  void write(size_t i, T value)
  {
    if (!zero_reg || i != 0)
      data[i] = value;
  }
  const T& operator [] (size_t i) const
  {
    return data[i];
  }
private:
  T data[N];
};

class processor_t;
typedef reg_t (*insn_func_t)(processor_t*, insn_t, reg_t);
struct insn_fetch_t
{
  insn_func_t func;
  insn_t insn;
};


// helpful macros, etc

#define INCREMENT_PC(value) (value + 4) 

#define MMU (*p->get_mmu())
#define STATE (*p->get_state())
#define PIPE (*p->get_pipe())
#define RS1 STATE.XPR[insn.rs1()]
#define RS2 STATE.XPR[insn.rs2()]
#define WRITE_RD(value) STATE.XPR.write(insn.rd(), value)

#ifdef RISCV_ENABLE_COMMITLOG
  #undef WRITE_RD 
  #define WRITE_RD(value) ({ \
        reg_t wdata = value; /* value is a func with side-effects */ \
        STATE.log_reg_write = (commit_log_reg_t){insn.rd() << 1, wdata}; \
        STATE.XPR.write(insn.rd(), wdata); \
      })
#endif

#ifdef RISCV_MICRO_CHECKER
  #undef RS1
  #define RS1 p->get_rs(insn.rs1(), pc, RSRC1_OPERAND)
  #undef RS2
  #define RS2 p->get_rs(insn.rs2(), pc, RSRC2_OPERAND)
  #undef WRITE_RD 
  #define WRITE_RD(value) ({ \
        reg_t wdata = value; /* value is a func with side-effects */ \
        if((insn.rd() != 0) && p->get_checker()){ /* Don't push if destination is ZERO register */  \
          PIPE.push_operand_actual(insn.rd(),RDST_OPERAND, wdata, pc); \
        } \
        STATE.XPR.write(insn.rd(), wdata); \
      })
#endif

#define FRS1 STATE.FPR[insn.rs1()]
#define FRS2 STATE.FPR[insn.rs2()]
#define FRS3 STATE.FPR[insn.rs3()]
#define WRITE_FRD(value) STATE.FPR.write(insn.rd(), value)
 
#ifdef RISCV_ENABLE_COMMITLOG
  #undef WRITE_FRD 
  #define WRITE_FRD(value) ({ \
        freg_t wdata = value; /* value is a func with side-effects */ \
        STATE.log_reg_write = (commit_log_reg_t){(insn.rd() << 1) | 1, wdata}; \
        STATE.FPR.write(insn.rd(), wdata); \
      })
#endif
 
#ifdef RISCV_MICRO_CHECKER
  #undef FRS1
  #define FRS1 p->get_frs(insn.rs1(), pc, RSRC1_OPERAND)
  #undef FRS2
  #define FRS2 p->get_frs(insn.rs2(), pc, RSRC2_OPERAND)
  #undef FRS3
  #define FRS3 p->get_frs(insn.rs3(), pc, RSRC3_OPERAND)
  #undef WRITE_FRD 
  #define WRITE_FRD(value) ({ \
        reg_t wdata = value; /* value is a func with side-effects */ \
        /* Push for all destination registers, even F0 */ \
        if(p->get_checker()){ /* Don't push if destination is ZERO register */  \
          PIPE.push_operand_actual((insn.rd() + NXPR),RDST_OPERAND, wdata, pc); \
        } \
        STATE.FPR.write(insn.rd(), wdata); \
      })
#endif


#define SHAMT (insn.i_imm() & 0x3F)
#define BRANCH_TARGET (pc + insn.sb_imm())
#define JUMP_TARGET (pc + insn.uj_imm())
#define RM ({ int rm = insn.rm(); \
              if(rm == 7) rm = STATE.frm; \
              if(rm > 4) throw trap_illegal_instruction(); \
              rm; })

#define xpr64 (xlen == 64)

#define require_supervisor if(unlikely(!(STATE.sr & SR_S))) throw trap_privileged_instruction()
#define require_xpr64 if(unlikely(!xpr64)) throw trap_illegal_instruction()
#define require_xpr32 if(unlikely(xpr64)) throw trap_illegal_instruction()
#ifndef RISCV_ENABLE_FPU
# define require_fp throw trap_illegal_instruction()
#else
# define require_fp if(unlikely(!(STATE.sr & SR_EF))) throw trap_fp_disabled()
#endif
#define require_accelerator if(unlikely(!(STATE.sr & SR_EA))) throw trap_accelerator_disabled()

#define cmp_trunc(reg) (reg_t(reg) << (64-xlen))
#define set_fp_exceptions ({ STATE.fflags |= softfloat_exceptionFlags; \
                             softfloat_exceptionFlags = 0; })

#define sext32(x) ((sreg_t)(int32_t)(x))
#define zext32(x) ((reg_t)(uint32_t)(x))
#define sext_xlen(x) (((sreg_t)(x) << (64-xlen)) >> (64-xlen))
#define zext_xlen(x) (((reg_t)(x) << (64-xlen)) >> (64-xlen))

#define set_pc(x) (npc = sext_xlen(x))

#define validate_csr(which, write) ({ \
  unsigned my_priv = (STATE.sr & SR_S) ? 1 : 0; \
  unsigned read_priv = ((which) >> 10) & 3; \
  unsigned write_priv = (((which) >> 8) & 3); \
  if (read_priv == 3) read_priv = write_priv, write_priv = -1; \
  if (my_priv < ((write) ? write_priv : read_priv)) \
    throw trap_privileged_instruction(); \
  (which); })

#endif
