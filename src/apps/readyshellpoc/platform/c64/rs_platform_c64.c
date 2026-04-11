#include "../rs_platform.h"
#include "reu_mgr.h"

#define RS_REU_PROBE_BANK 0x43u
#define RS_REU_PROBE_OFF  0x0000u

int rs_reu_available(void) {
  unsigned char probe;
  unsigned char check;
  probe = 0xA5u;
  check = 0u;
  reu_dma_stash((unsigned int)&probe, RS_REU_PROBE_BANK, RS_REU_PROBE_OFF, 1u);
  reu_dma_fetch((unsigned int)&check, RS_REU_PROBE_BANK, RS_REU_PROBE_OFF, 1u);
  return check == probe;
}

int rs_reu_read(unsigned long reu_off, void* ram_dst, unsigned short len) {
  unsigned char bank;
  unsigned int off;
  if (!ram_dst || len == 0u) {
    return -1;
  }
  bank = (unsigned char)((reu_off >> 16u) & 0xFFul);
  off = (unsigned int)(reu_off & 0xFFFFul);
  reu_dma_fetch((unsigned int)ram_dst, bank, off, (unsigned int)len);
  return 0;
}

int rs_reu_write(unsigned long reu_off, const void* ram_src, unsigned short len) {
  unsigned char bank;
  unsigned int off;
  if (!ram_src || len == 0u) {
    return -1;
  }
  bank = (unsigned char)((reu_off >> 16u) & 0xFFul);
  off = (unsigned int)(reu_off & 0xFFFFul);
  reu_dma_stash((unsigned int)ram_src, bank, off, (unsigned int)len);
  return 0;
}
