#include <signal.h>
#include <ucontext.h>
#include <stdio.h>
#include <stdint.h>
#include <unistd.h>
#include <sys/syscall.h>

static void h(int s, siginfo_t *si, void *uc_) {
    ucontext_t *uc = uc_;
    uint8_t *p = (uint8_t*)uc->uc_mcontext.__reserved;
    char buf[4096]; int n=0;
    n += snprintf(buf+n, sizeof buf-n, "ctx chain:");
    for (int i=0;i<16;i++){
        uint32_t m=*(uint32_t*)p, sz=*(uint32_t*)(p+4);
        n += snprintf(buf+n, sizeof buf-n, " [+%04x] magic=%08x size=%u", (int)((uint8_t*)p-(uint8_t*)uc->uc_mcontext.__reserved), m, sz);
        if(!m||!sz||sz&0xf) break;
        p += sz;
    }
    write(2, buf, n); write(2, "\n", 1);
}
int main(){
    struct sigaction sa={0}; sa.sa_sigaction=h; sa.sa_flags=SA_SIGINFO;
    sigemptyset(&sa.sa_mask);
    if(sigaction(SIGURG,&sa,0)){perror("sigaction");return 1;}
    syscall(SYS_tgkill, getpid(), syscall(SYS_gettid), SIGURG);
    printf("survived\n");
    return 0;
}
