#include <stdint.h>
#include <unistd.h>
#include <sys/mman.h>
#include <stdlib.h>
#include <stdio.h>
#include <pthread.h>
#include <sched.h>

#include <sys/syscall.h>
#include <unistd.h>

#ifndef NR_riscv_flush_icache
#ifndef NR_arch_specific_syscall
#define NR_arch_specific_syscall 244
#endif
#define NR_riscv_flush_icache (NR_arch_specific_syscall + 15)
#endif

#define SYS_RISCV_FLUSH_ICACHE_LOCAL 1UL
#define SYS_RISCV_FLUSH_ICACHE_ALL   0UL

static long sys_flush_icache(uintptr_t start, uintptr_t end , uintptr_t flags) {
  return syscall(NR_riscv_flush_icache, start, end, flags);
}

static uint32_t* get_jit_page(int pages) {
  int len = getpagesize() * pages;
  void* addr = mmap(NULL, len, PROT_READ|PROT_WRITE|PROT_EXEC, MAP_PRIVATE|MAP_ANON, -1, 0);
  if (addr == NULL) {
    printf("No jit page\n");
    exit(1);
  }
  return (uint32_t*)addr;
}

static uint32_t patch(uint32_t target, uint32_t msb, uint32_t lsb, uint32_t val) {
  unsigned nbits = msb - lsb + 1;
  unsigned mask = (1U << nbits) - 1;
  val <<= lsb;
  mask <<= lsb;
  target &= ~mask;
  target |= val;
  return target;
}

static void auipc(uint32_t* adr, int32_t imm) {
  int32_t upperImm = imm >> 12;
  uint32_t insn = 0;
  insn = patch(insn, 6, 0, 23); // 0b0010111
  insn = patch(insn, 7 + 4, 7, 5); // t0
  upperImm &= 0x000fffff;
  insn = patch(insn, 31, 12, upperImm);
  // store
  *adr = insn;
}

static void auipc_fix(uint32_t* adr, int64_t dest, int64_t pc) {
  int64_t distance = dest - pc;
  auipc(adr, (int32_t)distance + 0x800);
}

void ld(uint32_t* adr, int32_t offset) {
  unsigned insn = 0;
  int32_t val = offset & 0xfff;
  insn = patch(insn, 6, 0, 3);
  insn = patch(insn, 14, 12, 3);
  insn = patch(insn, 15 + 4, 15, 5);
  insn = patch(insn, 7 + 4, 7, 5);
  insn = patch(insn, 31, 20, val);
  // store
  *adr = insn;
}

static void ld_fix(uint32_t* adr, int64_t dest, int64_t pc) {
  int64_t distance = dest - pc;
  ld(adr, ((int32_t)distance << 20) >> 20);
}

static void jump_t0(uint32_t* adr, int64_t dest, int64_t pc) {
  int64_t distance = dest - pc;
  int32_t offset = ((int32_t)distance << 20) >> 20;
  uint32_t insn = 0;
  insn = patch(insn, 6, 0, 103); // 0b1100111
  insn = patch(insn, 7 + 4, 7, 0); // ra
  insn = patch(insn, 14, 12, 0); //0b000
  insn = patch(insn, 15 + 4, 15, 5); // t0
  uint32_t val = offset & 0xfffu;
  insn = patch(insn, 31, 20, val);
  // store
  *adr = insn;
}

#define NOP      0x00000013 // addi x0, x0, 0
#define AUIPC    0x00b00513 // auipc t0, imm
#define CALL_a0  0x000500e7 // jalr x1, 0(a0)
#define CALL_t0  0x000280e7 // jalr x1, 0(t0)
#define JUMP     0x00028067 // jalr x0, 0(t0)
#define RET      0x00008067 // jalr x0, 0(x1)
#define LD       0x0002b283 // ld   t0, 0(t0)
#define ILL      0xffffffff //
#define AD_SP    0xff810113 // addi sp,sp,-8
#define AD_SP2   0xff010113 // addi sp,sp,-16
#define ST_RA    0x00113023 // sd ra,0(sp)
#define ST_A0    0x00a13423 // sd a0,8(sp)
#define LD_RA    0x00013083 // ld ra,0(sp)
#define LD_A0    0x00813503 // ld a0,8(sp)
#define RE_SP    0x00810113 // addi sp,sp,8
#define RE_SP2   0x01010113 // addi sp,sp,16
#define FE_I     0x0000100f // fence.i

static uint32_t traceable[] =
{
// Call trace
  AUIPC,    // 0
  NOP,      // 1 | J T0
// This function just call another functions
  AD_SP,    // 2
  ST_RA,
  CALL_a0,
  LD_RA,    // 5 
  RE_SP,
  RET,      // 7
// End of function
  AD_SP2,   // 8
  ST_RA,
  ST_A0,
  AUIPC,    // 11
  LD,
  CALL_t0,  // 13 calls to *trace_to_address
  LD_RA,    
  LD_A0,    
  RE_SP2,   // restore sp
  AUIPC,    // 17
  JUMP,     // 18 jump back
  FE_I,     // 19
  RET,      // 20
  ILL
};

void insert(uint32_t* dst, uint32_t* src) {
  while (*src != 0xffffffff) {
    *dst++ = *src++;
  }
}

typedef uint64_t (*basic_func)();
typedef uint64_t (*callee_func)(basic_func);

static callee_func caller_entry = 0;

static uint32_t * volatile * trace_to_address = 0;

static volatile uint32_t *func_patch_adr = 0;

static uint32_t jalr_patch_data = 0;

uint64_t mytrace() {
  printf("tracing\n");
  return 0;
}

uint64_t mycallee_callee() {
  printf("callee_callee!!\n");
  return 0;
}

void fencei() {
  __asm__ __volatile__ (
    "   fence.i \n\t \n\t"
    : : : "memory" );
}

void fence() {
  __atomic_thread_fence(__ATOMIC_SEQ_CST);
}

static volatile int thread_run = 1;

static void* runner(void* arg) {
  while (thread_run) {
    caller_entry(&mycallee_callee);
  }
  return 0;
}

int main() {
  pthread_t jit_thread;

  uint32_t* callee = get_jit_page(1);
  insert(callee, traceable);
  
  trace_to_address = (uint32_t **)(callee + 32);

  func_patch_adr = &callee[1]; // NOP

  // fix relocations
  { // entry auipc + nop
    uint32_t* trace_bridge_start = callee + 8;
    auipc_fix(callee,         (int64_t)trace_bridge_start, (int64_t)callee); // trace entry auipc imm fixed -> trace bridge
    jump_t0(&jalr_patch_data, (int64_t)trace_bridge_start, (int64_t)callee); // instructions to patch with correct imm12
  }
  { // trace bridge jump to trace_to_address
    uint32_t* trace_auipc = callee + 11;
    auipc_fix(trace_auipc , (int64_t)trace_to_address, (int64_t)trace_auipc); // trace bridge auipc imm fixed -> trace_to_address
    ld_fix(trace_auipc + 1, (int64_t)trace_to_address, (int64_t)trace_auipc); // trace bridge auipc imm fixed -> trace_to_address
  }
  { // jump back auipc
    uint32_t* back_auipc = callee + 17;
    uint32_t* func_start = callee + 2;
    auipc_fix(back_auipc  , (int64_t)func_start, (int64_t)back_auipc); // back auipc imm fixed -> trace_to_address
    jump_t0(back_auipc + 1, (int64_t)func_start, (int64_t)back_auipc); // instructions to patch with correct imm12
  }
  
  *trace_to_address = &callee[19]; // fence.i + return
  fencei();
  
  caller_entry = (callee_func)callee;

  // Test

  // orginal
  printf("No trace, NOP, data = loop\n");
  caller_entry(&mycallee_callee);
  
  // only patched no -> jalr
  *func_patch_adr = jalr_patch_data;
  fencei();
  printf("No trace, JUMP, data = loop\n");
  caller_entry(&mycallee_callee);
  
  // data ret -> mytrace
  *trace_to_address = (uint32_t *)mytrace;
  fence();
  printf("Trace, JUMP, data = mytrace\n");
  caller_entry(&mycallee_callee);

  // So we havn't crashed yet.
  *trace_to_address = &callee[19]; // fence.i + return
  *func_patch_adr = NOP;
  fence();
  pthread_create(&jit_thread, 0, runner, 0);

  for (int i = 0 ; i < 10000; i++) {
    // data ret -> mytrace
    printf("Trace ON\n");
    *trace_to_address = (uint32_t *)mytrace;
    *func_patch_adr = jalr_patch_data;
    fence();
    sys_flush_icache((uintptr_t)callee, (uintptr_t)callee+64, SYS_RISCV_FLUSH_ICACHE_ALL);
    printf("Trace ON: done\n");

    printf("Trace OFF\n");
    *trace_to_address = &callee[19]; // fence.i + return
    *func_patch_adr = NOP;
    printf("Trace OFF: done\n");
    fence();
  }
  thread_run = 0;
  void* tmp;
  pthread_join(jit_thread, &tmp);

  return 0;
}
