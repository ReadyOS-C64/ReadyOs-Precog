#include "readyshell_reu_host.h"

#include <string.h>

#define READYSHELL_REU_HOST_SIZE 0x1000000ul

static unsigned char g_readyshell_reu_host[READYSHELL_REU_HOST_SIZE];

void readyshell_reu_host_reset(void) {
  memset(g_readyshell_reu_host, 0, sizeof(g_readyshell_reu_host));
}

int rs_reu_available(void) {
  return 1;
}

int rs_reu_read(unsigned long reu_off, void* ram_dst, unsigned short len) {
  if (!ram_dst || len == 0u ||
      reu_off + (unsigned long)len > (unsigned long)sizeof(g_readyshell_reu_host)) {
    return -1;
  }
  memcpy(ram_dst, g_readyshell_reu_host + reu_off, len);
  return 0;
}

int rs_reu_write(unsigned long reu_off, const void* ram_src, unsigned short len) {
  if (!ram_src || len == 0u ||
      reu_off + (unsigned long)len > (unsigned long)sizeof(g_readyshell_reu_host)) {
    return -1;
  }
  memcpy(g_readyshell_reu_host + reu_off, ram_src, len);
  return 0;
}
