#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <errno.h>
#include <unistd.h>
#include <sys/syscall.h>
#include <sys/mman.h>
#include <sys/ioctl.h>

#define SYS_perf_event_open 241
#define PERF_TYPE_SOFTWARE 1
#define PERF_COUNT_SW_CPU_CLOCK 0
#define PERF_COUNT_SW_PAGE_FAULTS 2
#define PERF_SAMPLE_IP 1ULL
#define PERF_SAMPLE_ADDR (1ULL<<4)
#define PERF_SAMPLE_PHYS_ADDR (1ULL<<19)
#define PERF_RECORD_SAMPLE 9
#define PERF_EVENT_IOC_ENABLE 0x2400
#define PERF_EVENT_IOC_DISABLE 0x2401
#define PERF_EVENT_IOC_RESET 0x2403

struct perf_event_attr_x {
  uint32_t type, size;
  uint64_t config;
  uint64_t sample_period;
  uint64_t sample_type;
  uint64_t read_format;
  uint64_t flags; /* bitfield, 64 bits total for disabled..clock */
  uint64_t wakeup;
  uint32_t bp_type;
  uint64_t bp_addr_or_config1;
  uint64_t bp_len_or_config2;
  uint64_t sample_regs_user;
  uint32_t sample_stack_user;
  int32_t clockid;
  uint64_t sample_regs_intr;
  uint32_t aux_watermark;
  uint16_t sample_max_stack;
  uint16_t __reserved_2;
};

static void report(const char *name, int fd, int err) {
  printf("[%s] %s -> fd=%d errno=%d (%s)\n", fd >= 0 ? "OPEN" : "deny", name, fd, err, strerror(err));
}

int main(void) {
  printf("uid=%d\n", getuid());

  /* 1. exploit-exact: SW PAGE_FAULTS + IP|ADDR|PHYS_ADDR, pid=0 cpu=-1 */
  struct perf_event_attr_x pe; memset(&pe, 0, sizeof(pe));
  pe.size = sizeof(pe);
  pe.type = PERF_TYPE_SOFTWARE;
  pe.config = PERF_COUNT_SW_PAGE_FAULTS;
  pe.sample_type = PERF_SAMPLE_IP | PERF_SAMPLE_ADDR | PERF_SAMPLE_PHYS_ADDR;
  pe.sample_period = 1;
  pe.flags = 0;
  /* exclude_kernel=bit4? flags bitfield: disabled bit0, inherit bit1,
     pinned bit2, exclusive bit3, exclude_user bit4, exclude_kernel bit5,
     exclude_hv bit6, exclude_idle bit7, mmap bit8, comm bit9 ... */
  pe.flags = (1ULL<<5) | (1ULL<<6) | 1; /* disabled | exclude_kernel | exclude_hv */
  int fd = syscall(SYS_perf_event_open, &pe, 0, -1, -1, 0);
  report("sw_pagefault_phys self", fd, errno);

  /* 2. simpler: SW CPU_CLOCK, self, no phys */
  memset(&pe, 0, sizeof(pe));
  pe.size = sizeof(pe);
  pe.type = PERF_TYPE_SOFTWARE;
  pe.config = PERF_COUNT_SW_CPU_CLOCK;
  pe.sample_type = PERF_SAMPLE_IP;
  pe.sample_period = 1;
  pe.flags = 1; /* disabled */
  fd = syscall(SYS_perf_event_open, &pe, 0, -1, -1, 0);
  report("sw_clock_ip self", fd, errno);

  /* 3. PHYS_ADDR sampling only — the hard requirement for dm oracle */
  memset(&pe, 0, sizeof(pe));
  pe.size = sizeof(pe);
  pe.type = PERF_TYPE_SOFTWARE;
  pe.config = PERF_COUNT_SW_PAGE_FAULTS;
  pe.sample_type = PERF_SAMPLE_PHYS_ADDR;
  pe.sample_period = 1;
  pe.flags = (1ULL<<5) | (1ULL<<6) | 1;
  fd = syscall(SYS_perf_event_open, &pe, 0, -1, -1, 0);
  report("sw_pagefault PHYS_ADDR self", fd, errno);
  if (fd >= 0) {
    /* mmap ring + trigger one fault and try to read phys */
    size_t pg = 4096;
    struct perf_event_mmap_page_hdr { uint64_t data_head, data_tail; } *hdr;
    void *ring = mmap(NULL, pg * 65, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (ring == MAP_FAILED) { printf("[ring] mmap fail errno=%d\n", errno); }
    else {
      ioctl(fd, PERF_EVENT_IOC_RESET, 0);
      ioctl(fd, PERF_EVENT_IOC_ENABLE, 0);
      volatile char *anon = mmap(NULL, pg, PROT_READ|PROT_WRITE, MAP_PRIVATE|MAP_ANONYMOUS, -1, 0);
      *anon = 0x42; /* page fault sampled */
      ioctl(fd, PERF_EVENT_IOC_DISABLE, 0);
      struct perf_event_mmap_page_hdr *h = ring;
      printf("[ring] head=%llu tail=%llu\n",
             (unsigned long long)h->data_head, (unsigned long long)h->data_tail);
      /* parse sample records for phys addr */
      char *base = (char*)ring + pg;
      uint64_t head = h->data_head, tail = 0;
      while (tail < head) {
        uint64_t *eh = (uint64_t*)(base + (tail % (pg*64)));
        uint64_t type = eh[0] & 0xffffffff, size = eh[0] >> 32;
        /* eh: [0]=misc|type|size packed? real layout: u32 type, u16 misc, u16 size */
        uint32_t t = *(uint32_t*)eh; uint16_t sz = *(uint16_t*)((char*)eh + 6);
        if (t == PERF_RECORD_SAMPLE && sz > 16) {
          uint64_t *p = (uint64_t*)((char*)eh + 8);
          printf("[sample] phys=%016llx\n", (unsigned long long)p[0]);
        }
        tail += sz ? sz : 8;
      }
    }
  }
  return 0;
}
