/*
 * reu.h - REU DMA Transfer Functions for Ready OS
 * For Commodore 64 with 16MB REU
 */

#ifndef REU_H
#define REU_H

/* REU Commands */
#define REU_CMD_STASH  0x90  /* C64 -> REU (save) */
#define REU_CMD_FETCH  0x91  /* REU -> C64 (restore) */
#define REU_CMD_SWAP   0x92  /* Exchange both directions */
#define REU_CMD_VERIFY 0x93  /* Compare C64 <-> REU */

/* REU Bank Allocation for Ready OS */
#define REU_BANK_SYSTEM    0   /* System state, bank bitmap */
#define REU_BANK_CLIPBOARD 1   /* Shared clipboard (up to 60KB) */
#define REU_BANK_APP_BASE  2   /* App slots start here */
#define REU_BANK_APP_COUNT 24  /* 24 app slots (banks 2-25) */
#define REU_BANK_FREE_BASE 26  /* Free pool starts here */

/* Detect if REU is present */
unsigned char reu_detect(void);

/* Stash data from C64 to REU */
void reu_stash(unsigned int c64_addr, unsigned char bank,
               unsigned int reu_addr, unsigned int length);

/* Fetch data from REU to C64 */
void reu_fetch(unsigned int c64_addr, unsigned char bank,
               unsigned int reu_addr, unsigned int length);

/* General transfer with command parameter */
void reu_transfer(unsigned int c64_addr, unsigned char bank,
                  unsigned int reu_addr, unsigned int length,
                  unsigned char command);

/* Single byte operations */
void reu_stash_byte(unsigned char value, unsigned char bank, unsigned int addr);
unsigned char reu_fetch_byte(unsigned char bank, unsigned int addr);

#endif /* REU_H */
