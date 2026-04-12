/*
 * reu_mgr.h - REU Memory Manager for Ready OS
 * Manages 256 REU banks (16MB) with allocation table at $C600
 */

#ifndef REU_MGR_H
#define REU_MGR_H

/* Bank type constants */
#define REU_FREE       0
#define REU_APP_STATE  1
#define REU_CLIPBOARD  2
#define REU_APP_ALLOC  3
#define REU_RESERVED   4
#define REU_RS_CACHE   5
#define REU_RS_DEBUG   8
#define REU_RS_SCRATCH 13

/* Memory-mapped system area ($C600-$C7FF, persists across app switches) */
#define REU_ALLOC_TABLE  ((unsigned char*)0xC600)  /* 256 bytes, 1 per bank */
#define REU_SYS_MAGIC    ((unsigned char*)0xC700)
#define REU_MAGIC_VALUE  0xA5
#define REU_TOTAL_BANKS  256

/* First bank available for dynamic allocation (0-23 reserved for app slots) */
#define REU_FIRST_DYNAMIC 24

/* ReadyShell fixed REU ownership (absolute offsets in 0x40xxxx-0x43xxxx range) */
#define REU_BANK_RS_CACHE 0x40
#define REU_BANK_RS_DEBUG 0x43
#define REU_BANK_RS_SCRATCH 0x48

/* Shim bitmap at $C836-$C838 (tracks which app banks are loaded) */
#define SHIM_REU_BITMAP_LO  ((unsigned char*)0xC836)
#define SHIM_REU_BITMAP_HI  ((unsigned char*)0xC837)
#define SHIM_REU_BITMAP_XHI ((unsigned char*)0xC838)

/* Initialize REU manager (safe to call multiple times) */
void reu_mgr_init(void);

/* Allocate a free bank, mark with given type. Returns bank or 0xFF if full */
unsigned char reu_alloc_bank(unsigned char type);

/* Free a bank (marks as FREE) */
void reu_free_bank(unsigned char bank);

/* Get the type of a bank */
unsigned char reu_bank_type(unsigned char bank);

/* Count free banks */
unsigned char reu_count_free(void);

/* Count banks of a given type */
unsigned char reu_count_type(unsigned char type);

/* DMA helpers - transfer between C64 memory and REU */
void reu_dma_stash(unsigned int c64_addr, unsigned char bank,
                   unsigned int reu_offset, unsigned int length);
void reu_dma_fetch(unsigned int c64_addr, unsigned char bank,
                   unsigned int reu_offset, unsigned int length);

#endif /* REU_MGR_H */
