#include <signal.h>
#include <ucontext.h>
#include <stdio.h>
#include <stdint.h>
#include <unistd.h>
#include <sys/syscall.h>

static void h(int s, siginfo_t *si, void *uc_) {
    ucontext_t *uc = uc_;
    uint8_t *p = (uint8_t*)uc->uc_mcontext.__reserved;
    for (int i=0;i<64;i++){
        uint32_t m=*(uint32_t*)p, sz=*(uint32_t*)(p+4);
        if(m==0x46508001u){
            // markers at candidate offsets
            *(uint64_t*)(p+0x100) = 0x4141414141414141ULL; // record+0x100
            *(uint64_t*)(p+0x108) = 0x4242424242424242ULL; // record+0x108
            *(uint64_t*)(p+0x10)  = 0x4343434343434343ULL; // res[0]
            return;
        }
        if(!sz||sz&0xf) return;
        p += sz + 8;
    }
}
int main(){
    struct sigaction sa={0}; sa.sa_sigaction=h; sa.sa_flags=SA_SIGINFO;
    sigemptyset(&sa.sa_mask);
    if(sigaction(SIGURG,&sa,0)){perror("sigaction");return 1;}
    syscall(SYS_tgkill, getpid(), syscall(SYS_gettid), SIGURG);
    // after sigreturn: dump v14, v15, v16
    uint64_t lo, hi;
    __asm__ volatile("mov %0, v14.d[0]; mov %1, v14.d[1]" : "=r"(lo),"=r"(hi));
    printf("v14 = %016llx %016llx\n",(unsigned long long)hi,(unsigned long long)lo);
    __asm__ volatile("mov %0, v15.d[0]; mov %1, v15.d[1]" : "=r"(lo),"=r"(hi));
    printf("v15 = %016llx %016llx\n",(unsigned long long)hi,(unsigned long long)lo);
    __asm__ volatile("mov %0, v16.d[0]; mov %1, v16.d[1]" : "=r"(lo),"=r"(hi));
    printf("v16 = %016llx %016llx\n",(unsigned long long)hi,(unsigned long long)lo);
    printf("survived\n");
    return 0;
}
