#include <stdint.h>

#include <stdio.h>
static void log_out(const char* v) {
  printf("%s\n", v);
}

#include <unistd.h>
#include <sys/mman.h>
#include <stdlib.h>
static unsigned char* get_jit_page() {
  int len = getpagesize();
  void* addr = mmap(NULL, len, PROT_READ|PROT_WRITE|PROT_EXEC, MAP_PRIVATE|MAP_ANON, -1, 0);
  if (addr == NULL) {
    log_out("No jit page");
    exit(1);
  }
  return (unsigned char*)addr;
}

static unsigned char jit[] = 
{ 0x83, 0x35, 0x05, 0x00, // ld	    a1, 0(a0)
  0xe3, 0x8e, 0x05, 0xfe, // beq	  a1, x0, -4
  0x13, 0x05, 0xa0, 0x02, // addi	  a0, x0, 42
  0x67, 0x80, 0x00, 0x00, // jalr zero, 0(ra)
  0Xff, 0xff, 0xff, 0xff}; // ILL

static unsigned char jitf[] = 
{ 0x83, 0x35, 0x05, 0x00,  // ld	    a1, 0(a0)
  0xe3, 0x8e, 0x05, 0xfe,  // beq	  a1, x0, -4
  0x0f, 0x10, 0x00, 0x00,  // fence.i
  0x13, 0x05, 0xa0, 0x02,  // addi	  a0, x0, 42
  0x67, 0x80, 0x00, 0x00,  // jalr zero, 0(ra)
  0Xff, 0xff, 0xff, 0xff}; // ILL
  
static unsigned int addi_13 = 0x00d00513; /* addi a0, x0, 13 */
static unsigned int addi_42 = 0x02a00513; /* addi a0, x0, 42 */
static unsigned int ill     = 0xffffffff; /* ill */

typedef unsigned long (*jit_func)(volatile unsigned long*);
  
static volatile unsigned long wait1 = 0;
static volatile unsigned long wait2 = 0;
static jit_func f1 = 0;
static jit_func f2 = 0;

static volatile unsigned long f1_v42 = 0;
static volatile unsigned long f1_v13 = 0;
static volatile unsigned long f2_v42 = 0;
static volatile unsigned long f2_v13 = 0;

static unsigned long sum_v() {
  return f1_v42 + f1_v13 + f2_v42 + f2_v13;
}

static unsigned long sum_v_f1() {
  return f1_v42 + f1_v13;
}

static unsigned long sum_v_f2() {
  return f2_v42 + f2_v13;
}

static void *runner(void *arg) {
  volatile unsigned long* cont = (volatile unsigned long*)arg;
  unsigned long res;
  while (*cont != 0) {
    res = f1(&wait1);
    if (res == 42) ++f1_v42;
    else if (res == 13) ++f1_v13;
    else return (void*)1;

    res = f2(&wait2);
    if (res == 42) ++f2_v42;
    else if (res == 13) ++f2_v13;
    else return (void*)1;
  }
  return 0;
}

void insert(unsigned char* dst, unsigned char* src) {
  while (*src != 0xff && *(src+1) != 0xff) {
    *dst++ = *src++;
  }
}

#include <pthread.h>
#include <sched.h>
int main() {
  volatile unsigned long loop = 1;
  pthread_t jit_thread;
  unsigned char* exec = get_jit_page();

  insert(exec, jit);
  f1 = (jit_func)exec;
  volatile unsigned int* f1_cmodx_adr = ((volatile unsigned int*)&exec[8]);

  exec += 64 + sizeof(jit);
  exec = (unsigned char*)(((unsigned long)exec) & ~63ul);

  insert(exec, jitf);
  f2 = (jit_func)exec;
  volatile unsigned int* f2_cmodx_adr = ((volatile unsigned int*)&exec[8+4]);
  
  pthread_create(&jit_thread, 0, runner, (void*)&loop);
  unsigned long tmp;
  while (sum_v() < 10000) {
    tmp = sum_v_f1();
    *f1_cmodx_adr = addi_13;
    *f2_cmodx_adr = ill;
    wait1 = 1;
    *f2_cmodx_adr = addi_42;
    while (sum_v_f1() == tmp) {}
    wait1 = 0;

    tmp = sum_v_f2();
    *f2_cmodx_adr = addi_13;
    *f1_cmodx_adr = ill;
    wait2 = 1;
    *f1_cmodx_adr = addi_42;
    while (sum_v_f2() == tmp) {}
    wait2 = 0;
  }
  loop = 0;
  *f1_cmodx_adr = addi_13;
  *f2_cmodx_adr = addi_13;
  wait1 = 1;
  wait2 = 1;
  pthread_join(jit_thread, (void*)&tmp);
  if (tmp != 0) {
    printf("Failed\n");
  } else {
    printf("f1_v42: %lu, f1_v13: %lu, f2_v42: %lu, f2_v13: %lu\n", f1_v42, f1_v13, f2_v42, f2_v13);
  }
  return 0;
}
