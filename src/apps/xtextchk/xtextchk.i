 
 
 
 
 
 
 
 
 
 
 
 
 
 
 
 
 
 
 
 
 
 
 
 
 
 
 
 
 
 
 
 
 
 
 
 
 
 
 
 
 
 
 
 
 
 
 
 
 
 
 
 
 
 
 
 
 
 
 
 
 
 
 
 
 
 
 
 
typedef int ptrdiff_t;
typedef char wchar_t;
typedef unsigned size_t;
 
 
 
 
 
 
 
 
 
 
 
 
 
 
 
 
 
 
 
 
 
 
 
 
 
 
 
 
 
 
 
 
 
 
 
 
 
 
 
 
 
 
 
 
 
 
 
 
 
 
 
 
 
 
 
 
 
 
 
 
 
 
 
 
 
 
 
 
 
 
 
 
 
 
 
 
 
 
struct __vic2 {
union {
struct {
unsigned char spr0_x; 
unsigned char spr0_y; 
unsigned char spr1_x; 
unsigned char spr1_y; 
unsigned char spr2_x; 
unsigned char spr2_y; 
unsigned char spr3_x; 
unsigned char spr3_y; 
unsigned char spr4_x; 
unsigned char spr4_y; 
unsigned char spr5_x; 
unsigned char spr5_y; 
unsigned char spr6_x; 
unsigned char spr6_y; 
unsigned char spr7_x; 
unsigned char spr7_y; 
};
struct {
unsigned char x; 
unsigned char y; 
} spr_pos[8];
};
unsigned char spr_hi_x; 
unsigned char ctrl1; 
unsigned char rasterline; 
union {
struct {
unsigned char strobe_x; 
unsigned char strobe_y; 
};
struct {
unsigned char x; 
unsigned char y; 
} strobe;
};
unsigned char spr_ena; 
unsigned char ctrl2; 
unsigned char spr_exp_y; 
unsigned char addr; 
unsigned char irr; 
unsigned char imr; 
unsigned char spr_bg_prio; 
unsigned char spr_mcolor; 
unsigned char spr_exp_x; 
unsigned char spr_coll; 
unsigned char spr_bg_coll; 
unsigned char bordercolor; 
union {
struct {
unsigned char bgcolor0; 
unsigned char bgcolor1; 
unsigned char bgcolor2; 
unsigned char bgcolor3; 
};
unsigned char bgcolor[4]; 
};
union {
struct {
unsigned char spr_mcolor0; 
unsigned char spr_mcolor1; 
};
 
unsigned char spr_mcolors[2]; 
};
union {
struct {
unsigned char spr0_color; 
unsigned char spr1_color; 
unsigned char spr2_color; 
unsigned char spr3_color; 
unsigned char spr4_color; 
unsigned char spr5_color; 
unsigned char spr6_color; 
unsigned char spr7_color; 
};
unsigned char spr_color[8]; 
};
 
unsigned char x_kbd; 
unsigned char clock; 
};
 
 
 
 
 
 
 
 
 
 
 
 
 
 
 
 
 
 
 
 
 
 
 
 
 
 
 
 
 
 
 
 
 
 
struct __sid_voice {
unsigned freq; 
unsigned pw; 
unsigned char ctrl; 
unsigned char ad; 
unsigned char sr; 
};
struct __sid {
struct __sid_voice v1; 
struct __sid_voice v2; 
struct __sid_voice v3; 
unsigned flt_freq; 
unsigned char flt_ctrl; 
unsigned char amp; 
unsigned char ad1; 
unsigned char ad2; 
unsigned char noise; 
unsigned char read3; 
};
 
 
 
 
 
 
 
 
 
 
 
 
 
 
 
 
 
 
 
 
 
 
 
 
 
 
 
 
 
 
 
 
 
 
struct __6526 {
unsigned char pra; 
unsigned char prb; 
unsigned char ddra; 
unsigned char ddrb; 
unsigned char ta_lo; 
unsigned char ta_hi; 
unsigned char tb_lo; 
unsigned char tb_hi; 
unsigned char tod_10; 
unsigned char tod_sec; 
unsigned char tod_min; 
unsigned char tod_hour; 
unsigned char sdr; 
unsigned char icr; 
unsigned char cra; 
unsigned char crb; 
};
 
 
 
 
 
 
 
extern void c64_65816_emd[];
extern void c64_c256k_emd[];
extern void c64_dqbb_emd[];
extern void c64_georam_emd[];
extern void c64_isepic_emd[];
extern void c64_ram_emd[];
extern void c64_ramcart_emd[];
extern void c64_reu_emd[];
extern void c64_vdc_emd[];
extern void dtv_himem_emd[];
extern void c64_hitjoy_joy[];
extern void c64_numpad_joy[];
extern void c64_ptvjoy_joy[];
extern void c64_stdjoy_joy[]; 
extern void c64_1351_mou[]; 
extern void c64_joy_mou[];
extern void c64_inkwell_mou[];
extern void c64_pot_mou[];
extern void c64_swlink_ser[];
extern void c64_hi_tgi[]; 
 
 
 
unsigned char get_ostype (void);
 
 
 
 
 
 
 
 
 
 
 
 
 
 
 
 
 
 
 
 
 
 
 
 
 
 
 
 
 
 
 
 
 
 
 
 
 
 
 
 
 
 
 
 
unsigned char __fastcall__ _cbm_filetype (unsigned char c);
 
 
 
 
 
 
extern char _filetype; 
 
 
 
 
 
 
 
struct cbm_dirent {
char name[17]; 
unsigned int size; 
unsigned char type;
unsigned char access;
};
 
 
 
unsigned char get_tv (void);
 
unsigned char __fastcall__ kbrepeat (unsigned char mode);
 
void waitvsync (void);
 
 
 
 
 
 
unsigned char cbm_k_acptr (void);
unsigned char cbm_k_basin (void);
void __fastcall__ cbm_k_bsout (unsigned char C);
unsigned char __fastcall__ cbm_k_chkin (unsigned char FN);
void __fastcall__ cbm_k_ciout (unsigned char C);
unsigned char __fastcall__ cbm_k_ckout (unsigned char FN);
void cbm_k_clall (void);
void __fastcall__ cbm_k_close (unsigned char FN);
void cbm_k_clrch (void);
unsigned char cbm_k_getin (void);
unsigned cbm_k_iobase (void);
void __fastcall__ cbm_k_listen (unsigned char dev);
unsigned int __fastcall__ cbm_k_load(unsigned char flag, unsigned addr);
unsigned char cbm_k_open (void);
unsigned char cbm_k_readst (void);
unsigned char __fastcall__ cbm_k_save(unsigned int start, unsigned int end);
void cbm_k_scnkey (void);
void __fastcall__ cbm_k_second (unsigned char addr);
void __fastcall__ cbm_k_setlfs (unsigned char LFN, unsigned char DEV,
unsigned char SA);
void __fastcall__ cbm_k_setnam (const char* Name);
void __fastcall__ cbm_k_settim (unsigned long timer);
void __fastcall__ cbm_k_talk (unsigned char dev);
void __fastcall__ cbm_k_tksa (unsigned char addr);
void cbm_k_udtim (void);
void cbm_k_unlsn (void);
void cbm_k_untlk (void);
 
 
 
 
unsigned int __fastcall__ cbm_load (const char* name, unsigned char device, void* data);
 
unsigned char __fastcall__ cbm_save (const char* name, unsigned char device,
const void* addr, unsigned int size);
 
unsigned char __fastcall__ cbm_open (unsigned char lfn, unsigned char device,
unsigned char sec_addr, const char* name);
 
void __fastcall__ cbm_close (unsigned char lfn);
 
int __fastcall__ cbm_read (unsigned char lfn, void* buffer, unsigned int size);
 
int __fastcall__ cbm_write (unsigned char lfn, const void* buffer,
unsigned int size);
 
unsigned char cbm_opendir (unsigned char lfn, unsigned char device, ...);
 
unsigned char __fastcall__ cbm_readdir (unsigned char lfn,
struct cbm_dirent* l_dirent);
 
void __fastcall__ cbm_closedir (unsigned char lfn);
 
 
 
 
 
 
 
 
 
 
 
 
 
 
 
 
 
 
 
 
 
 
 
 
 
 
 
 
 
 
 
 
 
 
 
 
 
 
 
 
 
 
 
 
 
 
 
 
 
 
 
 
 
 
 
 
 
 
 
 
 
 
 
 
 
 
 
typedef unsigned char* va_list;
 
 
 
 
 
 
 
 
 
 
 
 
 
 
 
 
 
 
 
 
 
 
 
 
 
 
 
 
 
 
 
 
 
 
 
 
 
 
 
 
 
 
 
 
 
 
 
 
 
 
 
 
 
 
 
 
 
 
 
 
 
 
 
 
void clrscr (void);
 
unsigned char kbhit (void);
 
void __fastcall__ gotox (unsigned char x);
 
void __fastcall__ gotoy (unsigned char y);
 
void __fastcall__ gotoxy (unsigned char x, unsigned char y);
 
unsigned char wherex (void);
 
unsigned char wherey (void);
 
void __fastcall__ cputc (char c);
 
void __fastcall__ cputcxy (unsigned char x, unsigned char y, char c);
 
void __fastcall__ cputs (const char* s);
 
void __fastcall__ cputsxy (unsigned char x, unsigned char y, const char* s);
 
int cprintf (const char* format, ...);
 
int __fastcall__ vcprintf (const char* format, va_list ap);
 
char cgetc (void);
 
int cscanf (const char* format, ...);
 
int __fastcall__ vcscanf (const char* format, va_list ap);
 
char cpeekc (void);
 
unsigned char cpeekcolor (void);
 
unsigned char cpeekrevers (void);
 
void __fastcall__ cpeeks (char* s, unsigned int length);
 
unsigned char __fastcall__ cursor (unsigned char onoff);
 
unsigned char __fastcall__ revers (unsigned char onoff);
 
unsigned char __fastcall__ textcolor (unsigned char color);
 
unsigned char __fastcall__ bgcolor (unsigned char color);
 
unsigned char __fastcall__ bordercolor (unsigned char color);
 
void __fastcall__ chline (unsigned char length);
 
void __fastcall__ chlinexy (unsigned char x, unsigned char y, unsigned char length);
 
void __fastcall__ cvline (unsigned char length);
 
void __fastcall__ cvlinexy (unsigned char x, unsigned char y, unsigned char length);
 
void __fastcall__ cclear (unsigned char length);
 
void __fastcall__ cclearxy (unsigned char x, unsigned char y, unsigned char length);
 
void __fastcall__ screensize (unsigned char* x, unsigned char* y);
 
void __fastcall__ cputhex8 (unsigned char val);
void __fastcall__ cputhex16 (unsigned val);
 
 
 
 
 
 
 
 
 
 
 
 
 
 
 
 
 
 
 
 
 
 
 
 
 
 
 
 
 
 
 
 
 
 
 
 
 
 
 
 
 
 
 
 
 
 
 
 
 
 
 
 
 
 
 
 
 
 
 
 
 
 
 
 
 
 
 
 
 
 
char* __fastcall__ strcat (char* dest, const char* src);
char* __fastcall__ strchr (const char* s, int c);
int __fastcall__ strcmp (const char* s1, const char* s2);
int __fastcall__ strcoll (const char* s1, const char* s2);
char* __fastcall__ strcpy (char* dest, const char* src);
size_t __fastcall__ strcspn (const char* s1, const char* s2);
char* __fastcall__ strerror (int errcode);
size_t __fastcall__ strlen (const char* s);
char* __fastcall__ strncat (char* s1, const char* s2, size_t count);
int __fastcall__ strncmp (const char* s1, const char* s2, size_t count);
char* __fastcall__ strncpy (char* dest, const char* src, size_t count);
char* __fastcall__ strpbrk (const char* str, const char* set);
char* __fastcall__ strrchr (const char* s, int c);
size_t __fastcall__ strspn (const char* s1, const char* s2);
char* __fastcall__ strstr (const char* str, const char* substr);
char* __fastcall__ strtok (char* s1, const char* s2);
size_t __fastcall__ strxfrm (char* s1, const char* s2, size_t count);
void* __fastcall__ memchr (const void* mem, int c, size_t count);
int __fastcall__ memcmp (const void* p1, const void* p2, size_t count);
void* __fastcall__ memcpy (void* dest, const void* src, size_t count);
void* __fastcall__ memmove (void* dest, const void* src, size_t count);
void* __fastcall__ memset (void* s, int c, size_t count);
 
void* __fastcall__ _bzero (void* ptr, size_t n);
 
void __fastcall__ bzero (void* ptr, size_t n); 
char* __fastcall__ strdup (const char* s); 
int __fastcall__ stricmp (const char* s1, const char* s2); 
int __fastcall__ strcasecmp (const char* s1, const char* s2); 
int __fastcall__ strnicmp (const char* s1, const char* s2, size_t count); 
int __fastcall__ strncasecmp (const char* s1, const char* s2, size_t count); 
char* __fastcall__ strlwr (char* s);
char* __fastcall__ strlower (char* s);
char* __fastcall__ strupr (char* s);
char* __fastcall__ strupper (char* s);
char* __fastcall__ strqtok (char* s1, const char* s2);
const char* __fastcall__ _stroserror (unsigned char errcode);
 
 
 
 
 
 
 
 
 
 
 
 
 
 
 
 
 
 
 
 
 
 
 
 
 
 
 
 
 
 
 
 
 
 
 
 
 
 
 
 
 
 
 
 
 
 
typedef struct {
unsigned char x;
unsigned char y;
unsigned char w;
unsigned char h;
} TuiRect;
 
typedef struct {
unsigned char x;
unsigned char y;
unsigned char width;
unsigned char maxlen;
unsigned char cursor;
unsigned char color;
char *buffer;
} TuiInput;
 
typedef struct {
unsigned char x;
unsigned char y;
unsigned char w;
unsigned char h;
unsigned char count;
unsigned char selected;
unsigned char scroll_offset;
unsigned char item_color;
unsigned char sel_color;
const char **items;
} TuiMenu;
 
typedef struct {
unsigned char x;
unsigned char y;
unsigned char w;
unsigned char h;
unsigned int line_count;
unsigned int scroll_pos;
unsigned char color;
const char **lines;
} TuiTextArea;
 
 
void tui_init(void);
 
void tui_clear(unsigned char bg_color);
 
void tui_gotoxy(unsigned char x, unsigned char y);
 
 
void tui_putc(unsigned char x, unsigned char y, unsigned char ch, unsigned char color);
 
void tui_puts(unsigned char x, unsigned char y, const char *str, unsigned char color);
 
void tui_puts_n(unsigned char x, unsigned char y, const char *str,
unsigned char maxlen, unsigned char color);
 
void tui_hline(unsigned char x, unsigned char y, unsigned char len, unsigned char color);
 
void tui_vline(unsigned char x, unsigned char y, unsigned char len, unsigned char color);
 
void tui_fill_rect(const TuiRect *rect, unsigned char ch, unsigned char color);
 
void tui_clear_rect(const TuiRect *rect, unsigned char color);
 
void tui_clear_line(unsigned char y, unsigned char x, unsigned char len, unsigned char color);
 
 
void tui_window(const TuiRect *rect, unsigned char border_color);
 
void tui_window_title(const TuiRect *rect, const char *title,
unsigned char border_color, unsigned char title_color);
 
void tui_panel(const TuiRect *rect, const char *title, unsigned char bg_color);
 
 
void tui_status_bar(const char *text, unsigned char color);
 
void tui_status_bar_multi(const char **items, unsigned char count, unsigned char color);
 
 
void tui_menu_init(TuiMenu *menu, unsigned char x, unsigned char y,
unsigned char w, unsigned char h,
const char **items, unsigned char count);
 
void tui_menu_draw(TuiMenu *menu);
 
unsigned char tui_menu_input(TuiMenu *menu, unsigned char key);
 
unsigned char tui_menu_selected(TuiMenu *menu);
 
 
void tui_input_init(TuiInput *input, unsigned char x, unsigned char y,
unsigned char width, unsigned char maxlen,
char *buffer, unsigned char color);
 
void tui_input_draw(TuiInput *input);
 
unsigned char tui_input_key(TuiInput *input, unsigned char key);
 
void tui_input_clear(TuiInput *input);
 
 
void tui_textarea_init(TuiTextArea *area, unsigned char x, unsigned char y,
unsigned char w, unsigned char h,
const char **lines, unsigned int line_count);
 
void tui_textarea_draw(TuiTextArea *area);
 
void tui_textarea_scroll(TuiTextArea *area, int delta);
 
 
unsigned char tui_getkey(void);
 
unsigned char tui_kbhit(void);
 
unsigned char tui_get_modifiers(void);
 
unsigned char tui_is_back_pressed(void);
 
void tui_return_to_launcher(void);
 
void tui_switch_to_app(unsigned char bank);
 
unsigned char tui_get_next_app(unsigned char current_bank);
unsigned char tui_get_prev_app(unsigned char current_bank);
 
unsigned char tui_handle_global_hotkey(unsigned char key,
unsigned char current_bank,
unsigned char allow_bind);
 
 
void tui_progress_bar(unsigned char x, unsigned char y, unsigned char width,
unsigned char filled, unsigned char total,
unsigned char fill_color, unsigned char empty_color);
 
unsigned char tui_ascii_to_screen(unsigned char ascii);
 
void tui_str_to_screen(char *str);
 
void tui_print_uint(unsigned char x, unsigned char y, unsigned int value,
unsigned char color);
 
void tui_print_hex8(unsigned char x, unsigned char y, unsigned char value,
unsigned char color);
 
void tui_print_hex16(unsigned char x, unsigned char y, unsigned int value,
unsigned char color);
static unsigned char textfile_byte_to_screen(unsigned char ch) {
if (ch == '|' || ch == 221) {
return 0x5D;
}
return tui_ascii_to_screen(ch);
}
 
typedef struct {
const char* name;
unsigned char expected_delim;
unsigned char row;
} ProbeCase;
static unsigned char g_dbg[0x100u];
static unsigned char g_line[32u];
static unsigned char g_io_buf[16u];
static const ProbeCase g_cases[3u] = {
{ "bang", 33u, 6u + 0u },
{ "apipe", 124u, 6u + 2u },
{ "vline", 221u, 6u + 4u }
};
static void cleanup_io(void) {
cbm_k_clrch();
cbm_k_clall();
}
static void dbg_clear(void) {
memset(g_dbg, 0, sizeof(g_dbg));
g_dbg[0] = 0x54u;
g_dbg[1] = 0x01u;
g_dbg[2] = 0u;
g_dbg[3] = (unsigned char)0;
g_dbg[8] = 3u;
}
static void dbg_set_fail(unsigned char step, unsigned char detail) {
if (g_dbg[4] == 0u) {
g_dbg[4] = step;
g_dbg[5] = detail;
}
g_dbg[2] = 0u;
}
static unsigned char* slot_ptr(unsigned char index) {
return g_dbg + 0x20u + ((unsigned int)index * (unsigned int)0x20u);
}
static void slot_store_name(unsigned char* slot, const char* name) {
unsigned char i;
for (i = 0u; i < 8u; ++i) {
slot[i] = (name && name[i] != '\0') ? (unsigned char)name[i] : 0u;
}
}
static void clear_screen_buffers(void) {
memset(((unsigned char*)0x0400), 32, 1000u);
memset(((unsigned char*)0xD800), 1, 1000u);
}
static unsigned char read_status_code(unsigned char* code_out) {
char line[40];
int n;
unsigned int code;
const char* p;
n = cbm_read(15, line, sizeof(line) - 1u);
if (n < 0) {
n = 0;
}
line[n] = '\0';
code = 0u;
p = line;
while (*p >= '0' && *p <= '9') {
code = (unsigned int)(code * 10u + (unsigned int)(*p - '0'));
++p;
}
if (code_out) {
*code_out = (unsigned char)((code > 255u) ? 255u : code);
}
return (unsigned char)n;
}
static unsigned char run_command(unsigned char device, const char* cmd) {
unsigned char code;
char* open_name;
if (cmd) {
open_name = (char*)cmd;
} else {
open_name = "";
}
cleanup_io();
if (cbm_open(15, device, 15, open_name) != 0) {
cleanup_io();
return 1u;
}
code = 255u;
(void)read_status_code(&code);
cbm_close(15);
cleanup_io();
if (code <= 1u || code == 62u) {
return 0u;
}
return 1u;
}
static unsigned char scratch_name(const char* name) {
char cmd[24];
strcpy(cmd, "s:");
strcat(cmd, name);
return run_command(8, cmd);
}
static unsigned char write_named_dump_file(const char* name) {
char open_name[24];
int n;
if (scratch_name(name) != 0u) {
dbg_set_fail(4u, 1u);
return 1u;
}
strcpy(open_name, name);
strcat(open_name, ",s,w");
cleanup_io();
if (cbm_open(7u, 8, 2, open_name) != 0) {
cleanup_io();
dbg_set_fail(4u, 2u);
return 1u;
}
n = cbm_write(7u, g_dbg, 0x100u);
cbm_close(7u);
cleanup_io();
if (n != 0x100u) {
dbg_set_fail(4u, 3u);
return 1u;
}
return 0u;
}
static unsigned char read_first_line(const char* name,
unsigned char* out,
unsigned char cap,
unsigned char* out_len) {
char open_name[24];
unsigned char total;
unsigned char i;
int n;
if (!name || !out || cap == 0u) {
return 1u;
}
strcpy(open_name, name);
strcat(open_name, ",s,r");
cleanup_io();
if (cbm_open(2, 8, 2, open_name) != 0) {
cleanup_io();
return 1u;
}
total = 0u;
while (1) {
n = cbm_read(2, g_io_buf, 16u);
if (n <= 0) {
break;
}
for (i = 0u; i < (unsigned char)n; ++i) {
if (g_io_buf[i] == 13u) {
cbm_close(2);
cleanup_io();
if (out_len) {
*out_len = total;
}
return 0u;
}
if (total + 1u >= cap) {
cbm_close(2);
cleanup_io();
return 1u;
}
out[total++] = g_io_buf[i];
}
}
cbm_close(2);
cleanup_io();
if (out_len) {
*out_len = total;
}
return 0u;
}
static unsigned char find_delim(const unsigned char* text,
unsigned char len,
unsigned char expected) {
unsigned char i;
for (i = 0u; i < len; ++i) {
if (text[i] == expected) {
return i;
}
}
return 0xFFu;
}
static void render_line_to_screen(const unsigned char* text,
unsigned char len,
unsigned char row) {
unsigned int offset;
unsigned char i;
offset = (unsigned int)row * 40u + 2u;
for (i = 0u; i < len; ++i) {
((unsigned char*)0x0400)[offset + i] = textfile_byte_to_screen(text[i]);
((unsigned char*)0xD800)[offset + i] = 1;
}
}
static unsigned char run_probe(unsigned char slot_index,
const ProbeCase* probe) {
unsigned char* slot;
unsigned char len;
unsigned char delim_index;
unsigned int screen_offset;
unsigned char i;
unsigned char rc;
slot = slot_ptr(slot_index);
slot_store_name(slot, probe->name);
slot[11] = probe->expected_delim;
memset(g_line, 0, sizeof(g_line));
rc = read_first_line(probe->name, g_line, sizeof(g_line), &len);
slot[8] = rc;
slot[9] = len;
if (rc != 0u) {
dbg_set_fail(2u, slot_index + 1u);
return 1u;
}
for (i = 0u; i < 8u; ++i) {
slot[18u + i] = (i < len) ? g_line[i] : 0u;
}
delim_index = find_delim(g_line, len, probe->expected_delim);
slot[10] = delim_index;
if (delim_index == 0xFFu) {
dbg_set_fail(2u, (unsigned char)(0x10u + slot_index));
return 1u;
}
render_line_to_screen(g_line, len, probe->row);
screen_offset = (unsigned int)probe->row * 40u + 2u;
slot[12] = g_line[delim_index];
slot[13] = textfile_byte_to_screen(g_line[delim_index]);
slot[14] = ((unsigned char*)0x0400)[screen_offset + delim_index];
slot[15] = (delim_index > 0u) ? ((unsigned char*)0x0400)[screen_offset + delim_index - 1u] : 0u;
slot[16] = (delim_index + 1u < len) ? ((unsigned char*)0x0400)[screen_offset + delim_index + 1u] : 0u;
if (slot[14] == 32u) {
dbg_set_fail(3u, slot_index + 1u);
}
return 0u;
}
static unsigned char should_run_case(unsigned char index) {
(void)index;
return 1u;
}
int main(void) {
unsigned char i;
clrscr();
clear_screen_buffers();
dbg_clear();
for (i = 0u; i < 3u; ++i) {
if (!should_run_case(i)) {
continue;
}
if (run_probe(i, &g_cases[i]) != 0u) {
(void)write_named_dump_file("xtextstat");
cprintf("XTEXT FAIL %u\r\n", (unsigned int)(i + 1u));
return 1;
}
}
if (g_dbg[4] == 0u) {
g_dbg[2] = 1u;
}
(void)write_named_dump_file("xtextstat");
cprintf("XTEXT OK\r\n");
return (g_dbg[2] != 0u) ? 0 : 1;
}
