#/rivos/riscv-gnu-toolchain/bin/riscv64-unknown-linux-gnu-gcc -march=rv64gcv_zba_zbb_zbs_zicbom_zicboz_zicbop_zihintpause_zvknha_zvknhb_zacas -O3 -c -o const.o const.s

#CC=/usr/bin/riscv64-linux-gnu-gcc
CFLAGS=-O3 -march=rv64gc_v
DEPS = copy.h copy.S test.c
OBJ = copy.o test.o

test_copy: copy.o test.o
	$(CC) -o $@ $^ $(CFLAGS)

copy.o: copy.S $(DEPS)
	$(CC) -c -o $@ $< $(CFLAGS)

test.o: test.c $(DEPS)
	$(CC) -c -o $@ $< $(CFLAGS)

.PHONY: clean

clean:
	rm -f test.o
	rm -f copy.o
	rm -f test_copy
