#include <stdio.h>
#include <stdint.h>
#include <unistd.h>
#include <sys/syscall.h>
#include <sys/types.h>
#include <sched.h>
#include <errno.h>
#include <string.h>
#include <linux/keyctl.h>
#include <sys/uio.h>
#include <sys/wait.h>

#ifndef SYS_io_uring_setup
#define SYS_io_uring_setup 425
#endif
#ifndef SYS_bpf
#define SYS_bpf 321
#endif
#ifndef SYS_userfaultfd
#define SYS_userfaultfd 323
#endif
#ifndef SYS_kcmp
#define SYS_kcmp 312
#endif
#ifndef SYS_perf_event_open
#define SYS_perf_event_open 241
#endif
#ifndef SYS_process_vm_readv
#define SYS_process_vm_readv 310
#endif
#ifndef SYS_open_tree
#define SYS_open_tree 428
#endif
#ifndef SYS_fsopen
#define SYS_fsopen 430
#endif
#ifndef SYS_clone3
#define SYS_clone3 435
#endif
#ifndef SYS_keyctl
#define SYS_keyctl 219
#endif
#ifndef SYS_memfd_create
#define SYS_memfd_create 279
#endif
#ifndef SYS_memfd_secret
#define SYS_memfd_secret 447
#endif

static void t(const char *n, long r) {
    if (r >= 0) { printf("[OPEN]   %s -> %ld\n", n, r); if (r > 2) close((int)r); }
    else printf("[errno=%3ld] %s (%s)\n", (long)-r, n, strerror(-r));
}

int main(void) {
    printf("uid=%d\n", getuid());
    /* io_uring */
    t("io_uring_setup", syscall(SYS_io_uring_setup, 8, 0));
    /* bpf - minimal prog load attempt */
    { char buf[64]; memset(buf,0,sizeof(buf)); long r = syscall(SYS_bpf, 5 /*BPF_PROG_LOAD*/, buf, 63); t("bpf", r); }
    /* userfaultfd */
    t("userfaultfd", syscall(SYS_userfaultfd, 0));
    /* unshare userns via clone3? use unshare */
    { long r = syscall(SYS_unshare, 0x10000000 /*CLONE_NEWUSER*/); printf("[%s] unshare(NEWUSER) -> %ld\n", r==0?"OPEN":"deny", r); }
    /* perf_event_open basic */
    { char pe[120]; memset(pe,0,sizeof(pe));
      *(uint32_t*)pe = 1; *(uint32_t*)(pe+4) = sizeof(pe); *(uint64_t*)(pe+8)=0x0;
      long r = syscall(SYS_perf_event_open, pe, -1, 0, -1, 0);
      t("perf_event_open", r); }
    /* keyctl join session keyring */
    t("keyctl(JOIN)", syscall(SYS_keyctl, 1 /*JOIN*/, "poc_probe", 0,0,0));
    /* memfd */
    t("memfd_create", syscall(SYS_memfd_create, "x", 0));
    t("memfd_secret", syscall(SYS_memfd_secret, 0));
    /* fsopen (new mount api - sometimes abused) */
    t("fsopen", syscall(SYS_fsopen, "ext4", 0));
    /* open_tree */
    t("open_tree", syscall(SYS_open_tree, -100 /*AT_FDCWD*/, "/proc", 0));
    /* kcmp self */
    t("kcmp", syscall(SYS_kcmp, getpid(), getpid(), 0, 0, 0));
    return 0;
}
