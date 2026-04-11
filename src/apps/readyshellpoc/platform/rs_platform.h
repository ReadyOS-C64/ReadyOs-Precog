#ifndef RS_PLATFORM_H
#define RS_PLATFORM_H

struct RSValue;

void rs_putc(char c);
void rs_puts(const char* s);
void rs_newline(void);

void rs_prompt(void);

int rs_file_read_all(const char* path, unsigned char* dst, unsigned short max,
                     unsigned short* out_len);
int rs_file_write_all(const char* path, const unsigned char* src, unsigned short len);

int rs_reu_available(void);
int rs_reu_read(unsigned long reu_off, void* ram_dst, unsigned short len);
int rs_reu_write(unsigned long reu_off, const void* ram_src, unsigned short len);

#endif
