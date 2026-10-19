extra-y := vmlinux.lds
extra-y += vmlinux-fuzzing-postlink.lds

KASAN_SANITIZE_init.o := n
KASAN_SANITIZE_stacktrace.o := n

obj-y = setup.o threads.o irq.o time.o syscalls.o misc.o console.o \
	syscalls_32.o init.o stacktrace.o

ifdef CONFIG_LKL_HOST_CPU
obj-y += cpu_host.o
else
obj-y += cpu.o
endif
