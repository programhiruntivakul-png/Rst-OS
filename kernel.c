#define VGA_ADDRESS ((volatile unsigned short*)0xB8000)

// VGA Colors
#define GREEN  0x0A
#define YELLOW 0x0E

// GUI Colors
#define DESKTOP_BG 0x1F
#define BAR_BG     0x70
#define WIN_BG     0x0F
#define BTN_BG     0x2E

#defin Structure (512 Bytes Exact)
typedef struct {
    unsigned int magic;
    char user[32];
    char pass[32];
    int is_neofetch;
    int is_games;
    unsigned char reserved[436];
} ConfigSave;

// Global States
int in_gui_mode = 0;
int active_window = 0; // 0 = None, 1 = Sys Info, 2 = Neofetch

unsigned short log_buffer[LOG_MAX_LINES][COLS];
int total_lines = 1;
int current_col = 0;
int view_offset = 0;

char command_buffer[128];
int buffer_idx = 0;

char current_user[32] = "rstos";
char current_pass[32] = "rstos";
int lang_mode = 0;

int is_neofetch_installed = 0;
int is_games_installed = 0;

static unsigned long rand_seed = 123456789;
static unsigned short sector_buf[256];

// Mouse States
int mouse_x = 40;
int mouse_y = 12;
int mouse_left_click = 0;
int mouse_right_click = 0;
unsigned char mouse_cycle = 0;
char mouse_byte[3];

unsigned char scan_code_table_en[128] = {
    0, 0, '1', '2', '3', '4', '5', '6', '7', '8', '9', '0', '-', '=', '\b',
    0, 'q', 'w', 'e', 'r', 't', 'y', 'u', 'i', 'o', 'p', '[', ']', '\n',
    0, 'a', 's', 'd', 'f', 'g', 'h', 'j', 'k', 'l', ';', '\'', '`',
    0, '\\', 'z', 'x', 'c', 'v', 'b', 'n', 'm', ',', '.', '/', 0, '*', 0, ' '
};

unsigned char scan_code_table_th[128] = {
    0, 0, 0xE5, '/', '-', 0xC0, 0xB6, 0xD8, 0xD6, 0xA4, 0xB5, 0xA8, 0xA2, 0xAA, '\b',
    0, 0xE5, 0xD3, 0xBE, 0xD0, 0xD1, 0xD5, 0xC3, 0xB9, 0xC2, 0xBA, 0xC5, 0xA3, '\n',
    0, 0xBF, 0xCB, 0xA1, 0xB4, 0xE0, 0xE9, 0xE8, 0xD2, 0xCA, 0xC7, 0xA7, '`',
    0, '\\', 0xBC, 0xBB, 0xE1, 0xCD, 0xD4, 0xD7, 0xB7, 0xC1, 0xE3, 0xBD, 0, '*', 0, ' '
};

// Low-level Port I/O
static inline unsigned char inb(unsigned short port) {
    unsigned char result;
    __asm__ __volatile__("inb %1, %0" : "=a"(result) : "Nd"(port));
    return result;
}

static inline void outb(unsigned short port, unsigned char val) {
    __asm__ __volatile__("outb %0, %1" : : "a"(val), "Nd"(port));
}

static inline unsigned short inw(unsigned short port) {
    unsigned short result;
    __asm__ __volatile__("inw %1, %0" : "=a"(result) : "Nd"(port));
    return result;
}

static inline void outw(unsigned short port, unsigned short val) {
    __asm__ __volatile__("outw %0, %1" : : "a"(val), "Nd"(port));
}

// PS/2 Mouse Driver Functions
void mouse_wait(unsigned char type) {
    unsigned int timeout = 100000;
    if (type == 0) {
        while ((inb(0x64) & 1) && --timeout);
    } else {
        while ((inb(0x64) & 2) && --timeout);
    }
}

void mouse_write(unsigned char write) {
    mouse_wait(1);
    outb(0x64, 0xD4);
    mouse_wait(1);
    outb(0x60, write);
}

unsigned char mouse_read(void) {
    mouse_wait(0);
    return inb(0x60);
}

void mouse_init(void) {
    unsigned char status;

    mouse_wait(1);
    outb(0x64, 0xA8);

    mouse_wait(1);
    outb(0x64, 0x20);
    mouse_wait(0);
    status = inb(0x60) | 2;
    mouse_wait(1);
    outb(0x64, 0x60);
    mouse_wait(1);
    outb(0x60, status);

    mouse_write(0xF6);
    mouse_read();

    mouse_write(0xF4);
    mouse_read();
}

void draw_mouse_cursor(void) {
    if (mouse_x >= 0 && mouse_x < COLS && mouse_y >= 0 && mouse_y < ROWS) {
        volatile unsigned short* vga = VGA_ADDRESS;
        vga[mouse_y * COLS + mouse_x] = (0xEC << 8) | '>';
    }
}

// ATA Delay & Disk Operations
static inline void ata_400ns_delay(void) {
    inb(0x1F7); inb(0x1F7); inb(0x1F7); inb(0x1F7);
}

void ata_write_sector(unsigned int lba, unsigned short* buffer) {
    if (inb(0x1F7) == 0xFF) return;

    outb(0x1F6, (0xE0 | ((lba >> 24) & 0x0F)));
    ata_400ns_delay();

    int timeout = 100000;
    while ((inb(0x1F7) & 0x80) && --timeout);
    if (timeout <= 0) return;

    outb(0x1F2, 1);
    outb(0x1F3, (unsigned char)(lba & 0xFF));
    outb(0x1F4, (unsigned char)((lba >> 8) & 0xFF));
    outb(0x1F5, (unsigned char)((lba >> 16) & 0xFF));

    outb(0x1F7, 0x30);
    ata_400ns_delay();

    timeout = 100000;
    while ((inb(0x1F7) & 0x80) && --timeout);
    while (!(inb(0x1F7) & 0x08) && --timeout);
    if (timeout <= 0) return;

    for (int i = 0; i < 256; i++) {
        outw(0x1F0, buffer[i]);
    }

    outb(0x1F7, 0xE7);
    ata_400ns_delay();
    timeout = 100000;
    while ((inb(0x1F7) & 0x80) && --timeout);
}

void ata_read_sector(unsigned int lba, unsigned short* buffer) {
    if (inb(0x1F7) == 0xFF) return;

    outb(0x1F6, (0xE0 | ((lba >> 24) & 0x0F)));
    ata_400ns_delay();

    int timeout = 100000;
    while ((inb(0x1F7) & 0x80) && --timeout);
    if (timeout <= 0) return;

    outb(0x1F2, 1);
    outb(0x1F3, (unsigned char)(lba & 0xFF));
    outb(0x1F4, (unsigned char)((lba >> 8) & 0xFF));
    outb(0x1F5, (unsigned char)((lba >> 16) & 0xFF));

    outb(0x1F7, 0x20);
    ata_400ns_delay();

    timeout = 100000;
    while ((inb(0x1F7) & 0x80) && --timeout);
    while (!(inb(0x1F7) & 0x08) && --timeout);
    if (timeout <= 0) return;

    for (int i = 0; i < 256; i++) {
        buffer[i] = inw(0x1F0);
    }
}

void save_system_config(void) {
    for (int i = 0; i < 256; i++) sector_buf[i] = 0;

    ConfigSave* cfg = (ConfigSave*)sector_buf;
    cfg->magic = SAVE_MAGIC;
    for (int i = 0; i < 32; i++) {
        cfg->user[i] = current_user[i];
        cfg->pass[i] = current_pass[i];
    }
    cfg->is_neofetch = is_neofetch_installed;
    cfg->is_games = is_games_installed;
    
    ata_write_sector(SAVE_SECTOR, sector_buf);
}

void load_system_config(void) {
    ata_read_sector(SAVE_SECTOR, sector_buf);
    ConfigSave* cfg = (ConfigSave*)sector_buf;

    if (cfg->magic == SAVE_MAGIC) {
        for (int i = 0; i < 32; i++) {
            current_user[i] = cfg->user[i];
            current_pass[i] = cfg->pass[i];
        }
        is_neofetch_installed = cfg->is_neofetch;
        is_games_installed = cfg->is_games;
    } else {
        save_system_config();
    }
}

// Function Declarations
void delay(int count);
void render_screen(void);
void scroll_up(int lines);
void scroll_down(int lines);
void clear_screen(void);
void print_char(unsigned char c, char color);
void handle_backspace(void);
void print_string(const char* str, char color);
void print_prompt(void);
void read_line(char* buffer, int max_len, int is_password);
void login_screen(void);
int verify_ashp_password(void);
int strcmp(const char* s1, const char* s2);
int strncmp(const char* s1, const char* s2, int n);
int strlen(const char* str);
void print_neofetch(void);
void execute_command(void);
void draw_box(int x, int y, int width, int height, char color, char fill_char);
void print_at(int x, int y, const char* str, char color);
void render_gui_desktop(void);
void draw_window(const char* title, const char* msg1, const char* msg2);
void redraw_gui(void);
void welcome_screen(void);
void shutdown_system(void);

void show_games_menu(void);
void play_rst_clicker(void);
void play_math_master(void);
void play_lucky_slots(void);
int rand(void);

// KERNEL MAIN
void kernel_main(void) {
    __asm__ __volatile__("cli");

    while (inb(0x64) & 0x01) {
        inb(0x60);
    }

    load_system_config();
    mouse_init();

    clear_screen();
    
    print_string("==========================================\n", GREEN);
    print_string("   RAPID SECURE THAILAND OPERATION SYSTEM  \n", YELLOW);
    print_string("                 RST OS 2                 \n", GREEN);
    print_string("==========================================\n\n", GREEN);

    login_screen();

    in_gui_mode = 1;
    welcome_screen();
    redraw_gui();

    int prev_left_click = 0;

    while (1) {
        if (inb(0x64) & 0x01) {
            unsigned char status = inb(0x64);
            unsigned char data = inb(0x60);

            if (status & 0x20) {
                if (mouse_cycle == 0) {
                    if (data & 0x08) {
                        mouse_byte[0] = data;
                        mouse_cycle++;
                    }
                } else if (mouse_cycle == 1) {
                    mouse_byte[1] = data;
                    mouse_cycle++;
                } else if (mouse_cycle == 2) {
                    mouse_byte[2] = data;
                    mouse_cycle = 0;

                    char rel_x = (char)mouse_byte[1];
                    char rel_y = (char)mouse_byte[2];

                    int dx = rel_x / 2;
                    int dy = rel_y / 2;
                    if (rel_x != 0 && dx == 0) dx = (rel_x > 0) ? 1 : -1;
                    if (rel_y != 0 && dy == 0) dy = (rel_y > 0) ? 1 : -1;

                    mouse_x += dx;
                    mouse_y -= dy;

                    if (mouse_x < 0) { mouse_x = 0; }
                    if (mouse_x >= COLS) { mouse_x = COLS - 1; }
                    if (mouse_y < 0) { mouse_y = 0; }
                    if (mouse_y >= ROWS) { mouse_y = ROWS - 1; }

                    mouse_left_click = (mouse_byte[0] & 0x01) ? 1 : 0;
                    mouse_right_click = (mouse_byte[0] & 0x02) ? 1 : 0;

                    if (in_gui_mode) {
                        redraw_gui();
                    }
                }
            } else {
                unsigned char scancode = data;
                if (!(scancode & 0x80) && scancode < 128) {
                    rand_seed += scancode;

                    if (in_gui_mode) {
                        if (scancode == 0x01) {
                            if (active_window != 0) {
                                active_window = 0;
                                redraw_gui();
                            } else {
                                in_gui_mode = 0;
                                clear_screen();
                                print_string("[Terminal Environment - Type 'exit' to return to Desktop]\n", YELLOW);
                                print_prompt();
                                buffer_idx = 0;
                            }
                        } else if (scancode == 0x02) {
                            in_gui_mode = 0;
                            clear_screen();
                            print_string("[Terminal Environment - Type 'exit' to return to Desktop]\n", YELLOW);
                            print_prompt();
                            buffer_idx = 0;
                        } else if (scancode == 0x03) {
                            active_window = 1;
                            redraw_gui();
                        } else if (scancode == 0x04) {
                            active_window = 2;
                            redraw_gui();
                        } else if (scancode == 0x05) {
                            shutdown_system();
                        } else if (scancode == 0x06) {
                            show_games_menu();
                        } else if (scancode == 0x1C) {
                            active_window = 0;
                            redraw_gui();
                        }
                    } else {
                        if (scancode == 0x29) {
                            lang_mode = !lang_mode;
                            print_char('\n', YELLOW);
                            print_string(lang_mode ? "[Language Mode: THAI]\n" : "[Language Mode: ENGLISH]\n", YELLOW);
                            print_prompt();
                            buffer_idx = 0;
                        } else if (scancode == 0x48) { scroll_up(1); }
                        else if (scancode == 0x50) { scroll_down(1); }
                        else {
                            unsigned char c = (lang_mode == 0) ? scan_code_table_en[scancode] : scan_code_table_th[scancode];
                            if (c == '\n') {
                                execute_command();
                            } else if (c == '\b') {
                                if (buffer_idx > 0) {
                                    buffer_idx--;
                                    handle_backspace();
                                }
                            } else if (c != 0) {
                                if (buffer_idx < 127) {
                                    command_buffer[buffer_idx++] = c;
                                    print_char(c, YELLOW);
                                }
                            }
                        }
                    }
                }
            }
        }

        int clicked = (mouse_left_click && !prev_left_click);
        prev_left_click = mouse_left_click;

        if (clicked && in_gui_mode) {
            if (active_window != 0) {
                if ((mouse_y == 6 && mouse_x >= 60 && mouse_x <= 64) ||
                    (mouse_y < 6 || mouse_y > 17 || mouse_x < 15 || mouse_x > 65)) {
                    active_window = 0;
                    redraw_gui();
                }
            } else if (mouse_y >= 4 && mouse_y <= 7) {
                if (mouse_x >= 2 && mouse_x <= 14) {
                    in_gui_mode = 0;
                    clear_screen();
                    print_string("[Terminal Environment - Type 'exit' to return to Desktop]\n", YELLOW);
                    print_prompt();
                    buffer_idx = 0;
                } else if (mouse_x >= 17 && mouse_x <= 29) {
                    active_window = 1;
                    redraw_gui();
                } else if (mouse_x >= 32 && mouse_x <= 44) {
                    active_window = 2;
                    redraw_gui();
                } else if (mouse_x >= 47 && mouse_x <= 59) {
                    shutdown_system();
                } else if (mouse_x >= 62 && mouse_x <= 74) {
                    show_games_menu();
                }
            }
        }
    }
}

int rand(void) {
    rand_seed = rand_seed * 1103515245 + 12345;
    return (unsigned int)(rand_seed / 65536) % 32768;
}

void shutdown_system(void) {
    clear_screen();
    print_string("\n\n==========================================\n", YELLOW);
    print_string("       Shutting down RST OS 2...          \n", GREEN);
    print_string("==========================================\n", YELLOW);
    delay(1);
    
    outw(0x604, 0x2000);
    outw(0xB004, 0x2000);
    outw(0x4004, 0x3400);

    clear_screen();
    print_string("\n\nIt is now safe to turn off your computer.", GREEN);
    while (1) {
        __asm__ __volatile__("cli; hlt");
    }
}

void welcome_screen(void) {
    draw_box(0, 0, 80, 25, DESKTOP_BG, ' ');
    draw_box(10, 6, 60, 12, WIN_BG, ' ');
    draw_box(10, 6, 60, 1, BAR_BG, ' ');
    
    print_at(12, 6, "[ RST OS System Initialized ]", BAR_BG);
    print_at(12, 9, "Welcome to Rapid Secure Thailand", GREEN);
    print_at(20, 11, "Operation System 2", GREEN);
    print_at(16, 15, "Press [ENTER] or Click to Start...", YELLOW);
    draw_mouse_cursor();

    int prev_click = 0;
    while (1) {
        if (inb(0x64) & 0x01) {
            unsigned char status = inb(0x64);
            unsigned char data = inb(0x60);
            if (status & 0x20) {
                if (mouse_cycle == 0) {
                    if (data & 0x08) { mouse_byte[0] = data; mouse_cycle++; }
                } else if (mouse_cycle == 1) {
                    mouse_byte[1] = data; mouse_cycle++;
                } else if (mouse_cycle == 2) {
                    mouse_byte[2] = data; mouse_cycle = 0;
                    char rel_x = (char)mouse_byte[1];
                    char rel_y = (char)mouse_byte[2];
                    mouse_x += rel_x / 2;
                    mouse_y -= rel_y / 2;

                    if (mouse_x < 0) { mouse_x = 0; }
                    if (mouse_x >= COLS) { mouse_x = COLS - 1; }
                    if (mouse_y < 0) { mouse_y = 0; }
                    if (mouse_y >= ROWS) { mouse_y = ROWS - 1; }

                    mouse_left_click = (mouse_byte[0] & 0x01) ? 1 : 0;
                    
                    draw_box(0, 0, 80, 25, DESKTOP_BG, ' ');
                    draw_box(10, 6, 60, 12, WIN_BG, ' ');
                    draw_box(10, 6, 60, 1, BAR_BG, ' ');
                    print_at(12, 6, "[ RST OS System Initialized ]", BAR_BG);
                    print_at(12, 9, "Welcome to Rapid Secure Thailand", GREEN);
                    print_at(20, 11, "Operation System 2", GREEN);
                    print_at(16, 15, "Press [ENTER] or Click to Start...", YELLOW);
                    draw_mouse_cursor();
                }
            } else {
                if (!(data & 0x80) && data == 0x1C) break;
            }
        }
        if (mouse_left_click && !prev_click) break;
        prev_click = mouse_left_click;
    }
}

void delay(int count) {
    for (volatile int i = 0; i < count * 5000000; i++) {
        __asm__ __volatile__("nop");
    }
}

void render_screen(void) {
    int start_line = 0;
    if (total_lines > ROWS) {
        int max_offset = total_lines - ROWS;
        if (view_offset > max_offset) view_offset = max_offset;
        if (view_offset < 0) view_offset = 0;
        start_line = max_offset - view_offset;
    }

    volatile unsigned short* vga = VGA_ADDRESS;
    for (int r = 0; r < ROWS; r++) {
        int log_r = start_line + r;
        for (int c = 0; c < COLS; c++) {
            if (log_r < total_lines) {
                vga[r * COLS + c] = log_buffer[log_r][c];
            } else {
                vga[r * COLS + c] = ((unsigned short)GREEN << 8) | ' ';
            }
        }
    }
}

void scroll_up(int lines) {
    if (total_lines > ROWS) {
        int max_offset = total_lines - ROWS;
        view_offset += lines;
        if (view_offset > max_offset) view_offset = max_offset;
        render_screen();
    }
}

void scroll_down(int lines) {
    if (total_lines > ROWS) {
        view_offset -= lines;
        if (view_offset < 0) view_offset = 0;
        render_screen();
    }
}

void clear_screen(void) {
    for (int r = 0; r < LOG_MAX_LINES; r++) {
        for (int c = 0; c < COLS; c++) {
            log_buffer[r][c] = ((unsigned short)GREEN << 8) | ' ';
        }
    }
    total_lines = 1;
    current_col = 0;
    view_offset = 0;
    render_screen();
}

void print_char(unsigned char c, char color) {
    view_offset = 0;
    if (c == '\n') {
        current_col = 0;
        total_lines++;
        if (total_lines > LOG_MAX_LINES) {
            for (int i = 0; i < LOG_MAX_LINES - 1; i++) {
                for (int j = 0; j < COLS; j++) {
                    log_buffer[i][j] = log_buffer[i + 1][j];
                }
            }
            for (int j = 0; j < COLS; j++) {
                log_buffer[LOG_MAX_LINES - 1][j] = ((unsigned short)GREEN << 8) | ' ';
            }
            total_lines = LOG_MAX_LINES;
        }
        render_screen();
    } else {
        log_buffer[total_lines - 1][current_col] = ((unsigned short)color << 8) | c;
        int vga_row = (total_lines > ROWS) ? (ROWS - 1) : (total_lines - 1);
        VGA_ADDRESS[vga_row * COLS + current_col] = ((unsigned short)color << 8) | c;

        current_col++;
        if (current_col >= COLS) {
            current_col = 0;
            total_lines++;
            if (total_lines > LOG_MAX_LINES) {
                for (int i = 0; i < LOG_MAX_LINES - 1; i++) {
                    for (int j = 0; j < COLS; j++) {
                        log_buffer[i][j] = log_buffer[i + 1][j];
                    }
                }
                for (int j = 0; j < COLS; j++) {
                    log_buffer[LOG_MAX_LINES - 1][j] = ((unsigned short)GREEN << 8) | ' ';
                }
                total_lines = LOG_MAX_LINES;
            }
            render_screen();
        }
    }
}

void handle_backspace(void) {
    view_offset = 0;
    if (current_col > 0) {
        current_col--;
        log_buffer[total_lines - 1][current_col] = ((unsigned short)GREEN << 8) | ' ';
        int vga_row = (total_lines > ROWS) ? (ROWS - 1) : (total_lines - 1);
        VGA_ADDRESS[vga_row * COLS + current_col] = ((unsigned short)GREEN << 8) | ' ';
    }
}

void print_string(const char* str, char color) {
    for (int i = 0; str[i] != '\0'; i++) {
        print_char((unsigned char)str[i], color);
    }
}

void print_prompt(void) {
    print_string(current_user, GREEN);
    print_string(lang_mode == 1 ? "/RST [TH] > " : "/RST [EN] > ", GREEN);
}

void read_line(char* buffer, int max_len, int is_password) {
    int idx = 0;
    while (1) {
        if (inb(0x64) & 0x01) {
            unsigned char scancode = inb(0x60);
            if (scancode & 0x80) continue;
            if (scancode >= 128) continue;

            if (scancode == 0x29) {
                lang_mode = !lang_mode;
                print_char('\n', YELLOW);
                print_string(lang_mode ? "[Language Mode: THAI]\n" : "[Language Mode: ENGLISH]\n", YELLOW);
                continue;
            }

            unsigned char c = (lang_mode == 0) ? scan_code_table_en[scancode] : scan_code_table_th[scancode];
            if (c == '\n') {
                print_char('\n', YELLOW);
                buffer[idx] = '\0';
                return;
            } else if (c == '\b') {
                if (idx > 0) {
                    idx--;
                    handle_backspace();
                }
            } else if (c != 0) {
                if (idx < max_len - 1) {
                    buffer[idx++] = c;
                    print_char(is_password ? '*' : c, YELLOW);
                }
            }
        }
    }
}

void login_screen(void) {
    char user_in[32];
    char pass_in[32];
    while (1) {
        print_string("RST OS Login System\n", YELLOW);
        print_string("Username: ", GREEN);
        read_line(user_in, 32, 0);
        
        print_string("Password: ", GREEN);
        read_line(pass_in, 32, 1);
        
        if (strcmp(user_in, current_user) == 0 && strcmp(pass_in, current_pass) == 0) {
            print_string("\nAccess Granted! Launching GUI Environment...\n\n", GREEN);
            delay(1);
            break;
        } else {
            print_string("\n[ERROR] Incorrect Username or Password!\n\n", YELLOW);
        }
    }
}

int verify_ashp_password(void) {
    char pass_check[32];
    print_string("[ashp] Enter Current Password: ", YELLOW);
    read_line(pass_check, 32, 1);
    if (strcmp(pass_check, current_pass) == 0) return 1;
    print_string("[ashp] Access Denied: Incorrect Password.\n", YELLOW);
    return 0;
}

int strcmp(const char* s1, const char* s2) {
    while (*s1 && (*s1 == *s2)) { s1++; s2++; }
    return *(unsigned char*)s1 - *(unsigned char*)s2;
}

int strncmp(const char* s1, const char* s2, int n) {
    for (int i = 0; i < n; i++) {
        if (s1[i] != s2[i] || s1[i] == '\0' || s2[i] == '\0') {
            return (unsigned char)s1[i] - (unsigned char)s2[i];
        }
    }
    return 0;
}

int strlen(const char* str) {
    int len = 0;
    while (str[len] != '\0') len++;
    return len;
}

void print_neofetch(void) {
    print_string("  .--------------.    ", GREEN); print_string("OS: ", YELLOW); print_string("Rapid Secure Thailand OS v2.0.0\n", GREEN);
    print_string(" /   _   _   _    \\   ", GREEN); print_string("Kernel: ", YELLOW); print_string("RST-x86 Native Core v2.0\n", GREEN);
    print_string("|   |_) (_  ~|~    |  ", GREEN); print_string("User: ", YELLOW); print_string(current_user, GREEN); print_string("\n", GREEN);
    print_string("|   | \\ __)  |     |  ", GREEN); print_string("Permission: ", YELLOW); print_string("ashp (Root/Highest)\n", GREEN);
    print_string("|                  |  ", GREEN); print_string("Shell: ", YELLOW); print_string("ashp terminal v2.0.0\n", GREEN);
    print_string(" \\   [ SECURE ]   /   ", GREEN); print_string("Packages: ", YELLOW); 
    
    int pkg_count = (is_neofetch_installed ? 1 : 0) + (is_games_installed ? 1 : 0);
    if (pkg_count == 2) print_string("2 (neofetch, rstos-sg)\n", GREEN);
    else if (pkg_count == 1) print_string("1 Active Package\n", GREEN);
    else print_string("0 Packages Installed\n", GREEN);

    print_string("  \\              /    ", GREEN); print_string("Theme: ", YELLOW); print_string("Cyberpunk Green & Yellow\n", GREEN);
    print_string("   \\            /     \n", GREEN);
    print_string("    \\          /      \n", GREEN);
    print_string("     \\        /       \n", GREEN);
    print_string("      \\      /        \n", GREEN);
    print_string("       \\    /         \n", GREEN);
    print_string("        \\  /          \n", GREEN);
    print_string("         \\/           \n\n", GREEN);
}

void draw_box(int x, int y, int width, int height, char color, char fill_char) {
    volatile unsigned short* vga = VGA_ADDRESS;
    for (int r = y; r < y + height; r++) {
        for (int c = x; c < x + width; c++) {
            if (r >= 0 && r < ROWS && c >= 0 && c < COLS) {
                vga[r * COLS + c] = ((unsigned short)color << 8) | fill_char;
            }
        }
    }
}

void print_at(int x, int y, const char* str, char color) {
    volatile unsigned short* vga = VGA_ADDRESS;
    for (int i = 0; str[i] != '\0'; i++) {
        if (x + i < COLS && y < ROWS) {
            vga[y * COLS + (x + i)] = ((unsigned short)color << 8) | str[i];
        }
    }
}

void render_gui_desktop(void) {
    draw_box(0, 0, 80, 25, DESKTOP_BG, ' ');
    draw_box(0, 0, 80, 1, BAR_BG, ' ');
    print_at(2, 0, "[RST OS v2.0.0 Desktop]", BAR_BG);
    print_at(55, 0, "User: ", BAR_BG);
    print_at(61, 0, current_user, BAR_BG);

    draw_box(0, 24, 80, 1, BAR_BG, ' ');
    print_at(1, 24, "[1] Terminal | [2] Sys Info | [3] Neofetch | [4] Shutdown | [5] Games", BAR_BG);

    draw_box(2, 4, 13, 4, BTN_BG, ' ');
    print_at(3, 5, ">_ Terminal", BTN_BG);
    print_at(3, 6, " [Click / 1]", BTN_BG);

    draw_box(17, 4, 13, 4, BTN_BG, ' ');
    print_at(18, 5, "i Sys Info", BTN_BG);
    print_at(18, 6, " [Click / 2]", BTN_BG);

    draw_box(32, 4, 13, 4, BTN_BG, ' ');
    print_at(33, 5, "# Neofetch", BTN_BG);
    print_at(33, 6, " [Click / 3]", BTN_BG);

    draw_box(47, 4, 13, 4, BTN_BG, ' ');
    print_at(48, 5, "X Shutdown", BTN_BG);
    print_at(48, 6, " [Click / 4]", BTN_BG);

    draw_box(62, 4, 13, 4, BTN_BG, ' ');
    print_at(63, 5, "+ RST Games", BTN_BG);
    print_at(63, 6, " [Click / 5]", BTN_BG);
}

void draw_window(const char* title, const char* msg1, const char* msg2) {
    draw_box(15, 6, 50, 12, WIN_BG, ' ');
    draw_box(15, 6, 50, 1, BAR_BG, ' ');
    print_at(17, 6, title, BAR_BG);
    print_at(62, 6, "[X]", BAR_BG);
    print_at(18, 9, msg1, GREEN);
    print_at(18, 11, msg2, YELLOW);
    print_at(18, 15, "Click [X] or Press [ESC] to Return Desktop", 0x0C);
}

void redraw_gui(void) {
    render_gui_desktop();

    if (active_window == 1) {
        draw_window("System Info", "RST OS v2.0.0 - x86 Native Core", "Persistent Storage & Mouse: ENABLED");
    } else if (active_window == 2) {
        if (is_neofetch_installed) {
            draw_window("Neofetch App", "Package: Installed", "Status: Cyberpunk Theme Active");
        } else {
            draw_window("Neofetch App", "Package NOT installed!", "Run 'ashp fp download neofetch' in CLI");
        }
    }

    draw_mouse_cursor();
}

void play_rst_clicker(void) {
    int clicks = 0;
    int prev_click = 0;

    while (1) {
        draw_box(0, 0, 80, 25, DESKTOP_BG, ' ');
        draw_box(15, 3, 50, 18, WIN_BG, ' ');
        draw_box(15, 3, 50, 1, BAR_BG, ' ');
        print_at(17, 3, "[ Game 1: RST Clicker ]", BAR_BG);
        print_at(58, 3, "[ESC: Exit]", BAR_BG);

        print_at(33, 5, "  /--------\\  ", GREEN);
        print_at(33, 6, " /   RST    \\ ", GREEN);
        print_at(33, 7, "|  SHIELD   |", GREEN);
        print_at(33, 8, " \\          / ", GREEN);
        print_at(33, 9, "  \\________/  ", GREEN);

        print_at(22, 12, "Total Clicks: ", YELLOW);
        char num_buf[16];
        int temp = clicks, idx = 0;
        if (temp == 0) { num_buf[idx++] = '0'; }
        else {
            char rev[16]; int r_idx = 0;
            while(temp > 0) { rev[r_idx++] = '0' + (temp % 10); temp /= 10; }
            while(r_idx > 0) { num_buf[idx++] = rev[--r_idx]; }
        }
        num_buf[idx] = '\0';
        print_at(36, 12, num_buf, YELLOW);

        const char* rank = "Novice";
        if (clicks >= 100) rank = "Cyber God";
        else if (clicks >= 50) rank = "RST Master";
        else if (clicks >= 20) rank = "Hacker";
        print_at(22, 14, "Current Rank: ", GREEN);
        print_at(36, 14, rank, GREEN);

        print_at(20, 17, "CLICK THE SHIELD or Press [SPACE]!", BAR_BG);
        draw_mouse_cursor();

        while (1) {
            if (inb(0x64) & 0x01) {
                unsigned char status = inb(0x64);
                unsigned char data = inb(0x60);
                if (status & 0x20) {
                    if (mouse_cycle == 0) {
                        if (data & 0x08) { mouse_byte[0] = data; mouse_cycle++; }
                    } else if (mouse_cycle == 1) {
                        mouse_byte[1] = data; mouse_cycle++;
                    } else if (mouse_cycle == 2) {
                        mouse_byte[2] = data; mouse_cycle = 0;
                        char rel_x = (char)mouse_byte[1];
                        char rel_y = (char)mouse_byte[2];
                        mouse_x += rel_x / 2;
                        mouse_y -= rel_y / 2;

                        if (mouse_x < 0) { mouse_x = 0; }
                        if (mouse_x >= COLS) { mouse_x = COLS - 1; }
                        if (mouse_y < 0) { mouse_y = 0; }
                        if (mouse_y >= ROWS) { mouse_y = ROWS - 1; }

                        mouse_left_click = (mouse_byte[0] & 0x01) ? 1 : 0;
                        break;
                    }
                } else {
                    if (!(data & 0x80)) {
                        if (data == 0x01) return;
                        if (data == 0x39 || data == 0x1C) { clicks++; break; }
                    }
                }
            }
            if (mouse_left_click && !prev_click) {
                if (mouse_x >= 58 && mouse_y == 3) return;
                clicks++;
                break;
            }
            prev_click = mouse_left_click;
        }
    }
}

void play_math_master(void) {
    int score = 0;
    while (1) {
        draw_box(0, 0, 80, 25, DESKTOP_BG, ' ');
        draw_box(15, 4, 50, 16, WIN_BG, ' ');
        draw_box(15, 4, 50, 1, BAR_BG, ' ');
        print_at(17, 4, "[ Game 2: RST Math Master ]", BAR_BG);
        print_at(58, 4, "[ESC: Exit]", BAR_BG);

        int a = (rand() % 20) + 1;
        int b = (rand() % 20) + 1;
        int op = rand() % 3;
        int expected = 0;
        char op_char = '+';

        if (op == 0) { expected = a + b; op_char = '+'; }
        else if (op == 1) { 
            if (a < b) { int t = a; a = b; b = t; }
            expected = a - b; op_char = '-'; 
        }
        else { a = (rand() % 10) + 1; b = (rand() % 10) + 1; expected = a * b; op_char = '*'; }

        char buf_a[8], buf_b[8];
        int ia = 0, ib = 0, ta = a, tb = b;
        if (ta==0) buf_a[ia++]='0'; else { char r[8]; int ri=0; while(ta>0){r[ri++]='0'+(ta%10); ta/=10;} while(ri>0) buf_a[ia++]=r[--ri]; } buf_a[ia]='\0';
        if (tb==0) buf_b[ib++]='0'; else { char r[8]; int ri=0; while(tb>0){r[ri++]='0'+(tb%10); tb/=10;} while(ri>0) buf_b[ib++]=r[--ri]; } buf_b[ib]='\0';

        print_at(22, 7, "Score: ", YELLOW);
        char score_buf[16];
        int ts = score, si = 0;
        if(ts==0) score_buf[si++]='0'; else { char r[16]; int ri=0; while(ts>0){r[ri++]='0'+(ts%10); ts/=10;} while(ri>0) score_buf[si++]=r[--ri]; } score_buf[si]='\0';
        print_at(29, 7, score_buf, YELLOW);

        print_at(22, 10, "Solve: ", GREEN);
        print_at(29, 10, buf_a, GREEN);
        char op_s[2] = {op_char, '\0'};
        print_at(29 + ia + 1, 10, op_s, GREEN);
        print_at(29 + ia + 3, 10, buf_b, GREEN);
        print_at(29 + ia + 3 + ib + 1, 10, "= ?", GREEN);

        print_at(22, 13, "Your Answer: ", YELLOW);

        char ans_buf[16];
        int ans_idx = 0;
        int exit_game = 0;
        while (1) {
            if (inb(0x64) & 0x01) {
                unsigned char status = inb(0x64);
                unsigned char data = inb(0x60);
                if (status & 0x20) {
                    if (mouse_cycle == 0) {
                        if (data & 0x08) { mouse_byte[0] = data; mouse_cycle++; }
                    } else if (mouse_cycle == 1) {
                        mouse_byte[1] = data; mouse_cycle++;
                    } else if (mouse_cycle == 2) {
                        mouse_byte[2] = data; mouse_cycle = 0;
                        char rel_x = (char)mouse_byte[1];
                        char rel_y = (char)mouse_byte[2];
                        mouse_x += rel_x / 2;
                        mouse_y -= rel_y / 2;

                        if (mouse_x < 0) { mouse_x = 0; }
                        if (mouse_x >= COLS) { mouse_x = COLS - 1; }
                        if (mouse_y < 0) { mouse_y = 0; }
                        if (mouse_y >= ROWS) { mouse_y = ROWS - 1; }

                        mouse_left_click = (mouse_byte[0] & 0x01) ? 1 : 0;
                        if (mouse_left_click && mouse_x >= 58 && mouse_y == 4) { exit_game = 1; break; }
                    }
                } else {
                    unsigned char scancode = data;
                    if (!(scancode & 0x80)) {
                        if (scancode == 0x01) { exit_game = 1; break; }
                        if (scancode == 0x1C) { ans_buf[ans_idx] = '\0'; break; }
                        if (scancode == 0x0E && ans_idx > 0) {
                            ans_idx--;
                            print_at(35 + ans_idx, 13, " ", YELLOW);
                        }
                        unsigned char c = scan_code_table_en[scancode];
                        if (c >= '0' && c <= '9' && ans_idx < 10) {
                            ans_buf[ans_idx++] = c;
                            char cs[2] = {c, '\0'};
                            print_at(35 + ans_idx - 1, 13, cs, YELLOW);
                        }
                    }
                }
            }
        }

        if (exit_game) return;

        int user_ans = 0;
        for (int i = 0; ans_buf[i] != '\0'; i++) {
            user_ans = user_ans * 10 + (ans_buf[i] - '0');
        }

        if (user_ans == expected) {
            score += 10;
            print_at(22, 16, "CORRECT! +10 Points", GREEN);
        } else {
            print_at(22, 16, "WRONG! Score Reset.", 0x0C);
            score = 0;
        }
        delay(1);
    }
}

void play_lucky_slots(void) {
    int coins = 100;
    const char* symbols[6] = {"[ 7 ]", "[$ $]", "[RST]", "[***]", "[VAL]", "[# #]"};
    int prev_click = 0;

    while (1) {
        draw_box(0, 0, 80, 25, DESKTOP_BG, ' ');
        draw_box(15, 4, 50, 16, WIN_BG, ' ');
        draw_box(15, 4, 50, 1, BAR_BG, ' ');
        print_at(17, 4, "[ Game 3: RST Lucky Slots ]", BAR_BG);
        print_at(58, 4, "[ESC: Exit]", BAR_BG);

        print_at(22, 7, "Coins Balance: ", YELLOW);
        char coin_buf[16];
        int tc = coins, ci = 0;
        if (tc <= 0) { coin_buf[ci++] = '0'; } else {
            char r[16]; int ri = 0;
            while(tc > 0) { r[ri++] = '0' + (tc % 10); tc /= 10; }
            while(ri > 0) coin_buf[ci++] = r[--ri];
        }
        coin_buf[ci] = '\0';
        print_at(37, 7, coin_buf, YELLOW);

        draw_box(22, 10, 36, 3, BAR_BG, ' ');
        print_at(25, 11, "[ ? ]    [ ? ]    [ ? ]", BAR_BG);

        print_at(20, 15, "Click Screen or Press [SPACE] (10 Coins)", GREEN);
        draw_mouse_cursor();

        int spin = 0;
        while (1) {
            if (inb(0x64) & 0x01) {
                unsigned char status = inb(0x64);
                unsigned char data = inb(0x60);
                if (status & 0x20) {
                    if (mouse_cycle == 0) {
                        if (data & 0x08) { mouse_byte[0] = data; mouse_cycle++; }
                    } else if (mouse_cycle == 1) {
                        mouse_byte[1] = data; mouse_cycle++;
                    } else if (mouse_cycle == 2) {
                        mouse_byte[2] = data; mouse_cycle = 0;
                        char rel_x = (char)mouse_byte[1];
                        char rel_y = (char)mouse_byte[2];
                        mouse_x += rel_x / 2;
                        mouse_y -= rel_y / 2;

                        if (mouse_x < 0) { mouse_x = 0; }
                        if (mouse_x >= COLS) { mouse_x = COLS - 1; }
                        if (mouse_y < 0) { mouse_y = 0; }
                        if (mouse_y >= ROWS) { mouse_y = ROWS - 1; }

                        mouse_left_click = (mouse_byte[0] & 0x01) ? 1 : 0;
                        break;
                    }
                } else {
                    if (!(data & 0x80)) {
                        if (data == 0x01) return;
                        if (data == 0x39 && coins >= 10) { spin = 1; coins -= 10; break; }
                    }
                }
            }
            if (mouse_left_click && !prev_click) {
                if (mouse_x >= 58 && mouse_y == 4) return;
                if (coins >= 10) { spin = 1; coins -= 10; break; }
            }
            prev_click = mouse_left_click;
        }

        if (spin) {
            int s1 = 0, s2 = 0, s3 = 0;
            for (int spin_i = 0; spin_i < 12; spin_i++) {
                s1 = rand() % 6;
                s2 = rand() % 6;
                s3 = rand() % 6;
                print_at(25, 11, symbols[s1], BAR_BG);
                print_at(34, 11, symbols[s2], BAR_BG);
                print_at(43, 11, symbols[s3], BAR_BG);
                delay(1);
            }

            if (s1 == 2 && s2 == 2 && s3 == 2) {
                coins += 500;
                print_at(22, 17, "!!! RST JACKPOT! +500 Coins !!!", GREEN);
            } else if (s1 == s2 && s2 == s3) {
                coins += 150;
                print_at(22, 17, "TRIPLE WIN! +150 Coins!", GREEN);
            } else if (s1 == s2 || s2 == s3 || s1 == s3) {
                coins += 30;
                print_at(22, 17, "DOUBLE MATCH! +30 Coins!", YELLOW);
            } else {
                print_at(22, 17, "No match! Try again...", 0x0C);
            }
            delay(2);
        }
    }
}

void show_games_menu(void) {
    if (!is_games_installed) {
        draw_window("RST Game Center", "Package 'rstos-sg' NOT installed!", "Run 'ashp fp download rstos-sg' in CLI");
        draw_mouse_cursor();
        while (1) {
            if (inb(0x64) & 0x01) {
                unsigned char scancode = inb(0x60);
                if (scancode & 0x80) continue;
                if (scancode == 0x01 || scancode == 0x1C) {
                    redraw_gui();
                    return;
                }
            }
            if (mouse_left_click) {
                redraw_gui();
                return;
            }
        }
    }

    int prev_click = 0;
    while (1) {
        draw_box(15, 4, 50, 16, WIN_BG, ' ');
        draw_box(15, 4, 50, 1, BAR_BG, ' ');
        print_at(17, 4, "[ RST Game Center (rstos-sg) ]", BAR_BG);
        print_at(58, 4, "[ESC: Exit]", BAR_BG);

        print_at(20, 7, "1. RST Clicker", GREEN);
        print_at(20, 9, "2. RST Math Master", GREEN);
        print_at(20, 11, "3. RST Lucky Slots", GREEN);
        print_at(20, 15, "Click Options or Press [1],[2],[3]", YELLOW);
        draw_mouse_cursor();

        while (1) {
            if (inb(0x64) & 0x01) {
                unsigned char status = inb(0x64);
                unsigned char data = inb(0x60);
                if (status & 0x20) {
                    if (mouse_cycle == 0) {
                        if (data & 0x08) { mouse_byte[0] = data; mouse_cycle++; }
                    } else if (mouse_cycle == 1) {
                        mouse_byte[1] = data; mouse_cycle++;
                    } else if (mouse_cycle == 2) {
                        mouse_byte[2] = data; mouse_cycle = 0;
                        char rel_x = (char)mouse_byte[1];
                        char rel_y = (char)mouse_byte[2];
                        mouse_x += rel_x / 2;
                        mouse_y -= rel_y / 2;

                        if (mouse_x < 0) { mouse_x = 0; }
                        if (mouse_x >= COLS) { mouse_x = COLS - 1; }
                        if (mouse_y < 0) { mouse_y = 0; }
                        if (mouse_y >= ROWS) { mouse_y = ROWS - 1; }

                        mouse_left_click = (mouse_byte[0] & 0x01) ? 1 : 0;
                        break;
                    }
                } else {
                    unsigned char scancode = data;
                    if (!(scancode & 0x80)) {
                        if (scancode == 0x01) { redraw_gui(); return; }
                        if (scancode == 0x02) { play_rst_clicker(); break; }
                        if (scancode == 0x03) { play_math_master(); break; }
                        if (scancode == 0x04) { play_lucky_slots(); break; }
                    }
                }
            }
            if (mouse_left_click && !prev_click) {
                if (mouse_y == 7 && mouse_x >= 20 && mouse_x <= 40) { play_rst_clicker(); break; }
                if (mouse_y == 9 && mouse_x >= 20 && mouse_x <= 40) { play_math_master(); break; }
                if (mouse_y == 11 && mouse_x >= 20 && mouse_x <= 40) { play_lucky_slots(); break; }
                if (mouse_x >= 58 && mouse_y == 4) { redraw_gui(); return; }
            }
            prev_click = mouse_left_click;
        }
    }
}

// Commands Execution
void execute_command(void) {
    print_char('\n', YELLOW);
    command_buffer[buffer_idx] = '\0';
    char* cmd = command_buffer;
    while (*cmd == ' ') cmd++;

    if (*cmd == '\0') {
        buffer_idx = 0;
        print_prompt();
        return;
    }

    if (strcmp(cmd, "help") == 0) {
        print_string("Commands: help, clear, sys, info, echo [text], ashp, neofetch, rst-games, exit, shutdown\n", GREEN);
    } else if (strcmp(cmd, "sys") == 0 || strcmp(cmd, "info") == 0) {
        print_string("OS Name: Rapid Secure Thailand OS (RST OS)\nVersion: 2.0.0\nMouse Driver: Enabled\n", GREEN);
    } else if (strcmp(cmd, "clear") == 0) {
        clear_screen();
    } else if (strncmp(cmd, "echo ", 5) == 0) {
        print_string(cmd + 5, GREEN);
        print_char('\n', GREEN);
    } else if (strcmp(cmd, "rst-games") == 0 || strcmp(cmd, "games") == 0) {
        if (!is_games_installed) {
            print_string("Package 'rstos-sg' is not installed!\nType 'ashp fp download rstos-sg' to install.\n", YELLOW);
        } else {
            show_games_menu();
            clear_screen();
        }
        buffer_idx = 0;
        print_prompt();
        return;
    } else if (strncmp(cmd, "ashp", 4) == 0) {
        if (strcmp(cmd, "ashp") == 0 || strcmp(cmd, "ashp help") == 0) {
            print_string("ashp System & Package Manager v2.0.0\n", YELLOW);
            print_string("Usage:\n", GREEN);
            print_string("  ashp fp list               - List available packages\n", GREEN);
            print_string("  ashp fp download <pkg>     - Install a package (neofetch / rstos-sg)\n", GREEN);
            print_string("  ashp fp remove <pkg>       - Uninstall a package\n", GREEN);
            print_string("  ashp define ur as <user>   - Set new Username\n", GREEN);
            print_string("  ashp define pw as <pass>   - Set new Password\n", GREEN);
        } else if (strcmp(cmd, "ashp fp list") == 0) {
            print_string("[ashp Repository]\n", YELLOW);
            if (is_neofetch_installed) print_string(" - neofetch [Status: Installed]\n", GREEN);
            else print_string(" - neofetch [Status: Available]\n", GREEN);
            if (is_games_installed) print_string(" - rstos-sg  [Status: Installed]\n", GREEN);
            else print_string(" - rstos-sg  [Status: Available]\n", GREEN);
        } else if (strcmp(cmd, "ashp fp download neofetch") == 0) {
            if (is_neofetch_installed) {
                print_string("[ashp] neofetch is already installed!\n", YELLOW);
            } else if (verify_ashp_password()) {
                is_neofetch_installed = 1;
                save_system_config();
                print_string("[ashp] Downloaded and installed neofetch successfully!\n", GREEN);
            }
        } else if (strcmp(cmd, "ashp fp remove neofetch") == 0) {
            if (!is_neofetch_installed) {
                print_string("[ashp] neofetch is not installed.\n", YELLOW);
            } else if (verify_ashp_password()) {
                is_neofetch_installed = 0;
                save_system_config();
                print_string("[ashp] Removed neofetch successfully!\n", GREEN);
            }
        } else if (strcmp(cmd, "ashp fp download rstos-sg") == 0) {
            if (is_games_installed) {
                print_string("[ashp] rstos-sg is already installed!\n", YELLOW);
            } else if (verify_ashp_password()) {
                is_games_installed = 1;
                save_system_config();
                print_string("[ashp] Downloaded and installed rstos-sg successfully!\n", GREEN);
            }
        } else if (strcmp(cmd, "ashp fp remove rstos-sg") == 0) {
            if (!is_games_installed) {
                print_string("[ashp] rstos-sg is not installed.\n", YELLOW);
            } else if (verify_ashp_password()) {
                is_games_installed = 0;
                save_system_config();
                print_string("[ashp] Removed rstos-sg successfully!\n", GREEN);
            }
        } else if (strncmp(cmd, "ashp define ur as ", 18) == 0) {
            if (verify_ashp_password()) {
                char* new_ur = cmd + 18;
                int i = 0;
                while (new_ur[i] != '\0' && i < 31) {
                    current_user[i] = new_ur[i];
                    i++;
                }
                current_user[i] = '\0';
                save_system_config();
                print_string("[ashp] Username changed to: ", GREEN);
                print_string(current_user, YELLOW);
                print_string("\n", GREEN);
            }
        } else if (strncmp(cmd, "ashp define pw as ", 18) == 0) {
            if (verify_ashp_password()) {
                char* new_pw = cmd + 18;
                int i = 0;
                while (new_pw[i] != '\0' && i < 31) {
                    current_pass[i] = new_pw[i];
                    i++;
                }
                current_pass[i] = '\0';
                save_system_config();
                print_string("[ashp] Password updated successfully!\n", GREEN);
            }
        } else {
            print_string("[ashp] Invalid subcommand. Type 'ashp help'\n", YELLOW);
        }
    } else if (strcmp(cmd, "exit") == 0 || strcmp(cmd, "gui") == 0) {
        in_gui_mode = 1;
        active_window = 0;
        redraw_gui();
        buffer_idx = 0;
        return;
    } else if (strcmp(cmd, "shutdown") == 0) {
        shutdown_system();
    } else if (strcmp(cmd, "neofetch") == 0) {
        if (is_neofetch_installed) print_neofetch();
        else print_string("neofetch: command not found.\n", YELLOW);
    } else {
        print_string("Unknown command: ", GREEN);
        print_string(command_buffer, YELLOW);
        print_char('\n', YELLOW);
    }

    buffer_idx = 0;
    print_prompt();
}