#define __ARCH_WANT_SYSCALL_NO_AT
#define __ARCH_WANT_SYSCALL_DEPRECATED
#define __ARCH_WANT_SYSCALL_NO_FLAGS
#define __ARCH_WANT_RENAMEAT
#define __ARCH_WANT_NEW_STAT
#define __ARCH_WANT_SET_GET_RLIMIT
#define __ARCH_WANT_TIME32_SYSCALLS

#include <asm/bitsperlong.h>

#if __BITS_PER_LONG == 64
#define __ARCH_WANT_SYS_NEWFSTATAT
#endif

#include <asm-generic/unistd.h>

#define __NR_virtio_mmio_device_add		(__NR_arch_specific_syscall + 0)
#define __NR_new_thread_group_leader	(__NR_arch_specific_syscall + 1)
#define __NR_aster_nlmsg_create		(__NR_arch_specific_syscall + 2)
#define __NR_aster_nlmsg_send		(__NR_arch_specific_syscall + 3)
#define __NR_aster_nlmsg_recv		(__NR_arch_specific_syscall + 4)
#define __NR_aster_nlmsg_quiesce	(__NR_arch_specific_syscall + 5)
#define __NR_aster_nlmsg_release	(__NR_arch_specific_syscall + 6)
#define __NR_aster_nlmsg_join		(__NR_arch_specific_syscall + 7)
