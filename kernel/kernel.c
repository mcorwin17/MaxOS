// --------------------------------------------------
// MaxOS Kernel - Cool Version
// Description: Enhanced kernel with cool effects and animations
// --------------------------------------------------

// Forward declarations
void main();
void clear_screen();
void print_string(const char* str);
void print_char(char c);
void set_cursor_position(int x, int y);
void print_welcome_message();
void print_system_info();
void animate_logo();
void draw_border();
void print_colored_text(const char* str, int color);
void delay(int ms);

// Video memory constants
#define VIDEO_MEMORY 0xB8000
#define SCREEN_WIDTH 80
#define SCREEN_HEIGHT 25
#define WHITE_ON_BLACK 0x07
#define GREEN_ON_BLACK 0x0A
#define RED_ON_BLACK 0x04
#define BLUE_ON_BLACK 0x01
#define CYAN_ON_BLACK 0x0B
#define YELLOW_ON_BLACK 0x0E
#define MAGENTA_ON_BLACK 0x0D
#define BRIGHT_WHITE_ON_BLACK 0x0F

// Global variables
static int cursor_x = 0;
static int cursor_y = 0;

// Entry point that the bootloader calls
void _start() {
    main();
    // Infinite loop to prevent the system from hanging
    while(1) {
        // Just loop forever
    }
}

void main() {
    // Clear the screen and set up
    clear_screen();
    
    // Draw a cool border
    draw_border();
    
    // Animate the logo
    animate_logo();
    
    // Print welcome message
    print_welcome_message();
    
    // Print system information
    print_system_info();
    
    // Print a cool command prompt
    set_cursor_position(0, 20);
    print_colored_text("MaxOS v2.0 - Ready for action!", CYAN_ON_BLACK);
    set_cursor_position(0, 21);
    print_colored_text("Type 'help' for commands", YELLOW_ON_BLACK);
    set_cursor_position(0, 22);
    print_colored_text("> ", BRIGHT_WHITE_ON_BLACK);
}

void clear_screen() {
    char* video_memory = (char*) VIDEO_MEMORY;
    for(int i = 0; i < SCREEN_WIDTH * SCREEN_HEIGHT * 2; i += 2) {
        video_memory[i] = ' ';      // Space character
        video_memory[i + 1] = WHITE_ON_BLACK;  // White on black
    }
    cursor_x = 0;
    cursor_y = 0;
}

void print_char(char c) {
    if(c == '\n') {
        cursor_x = 0;
        cursor_y++;
        if(cursor_y >= SCREEN_HEIGHT) {
            cursor_y = SCREEN_HEIGHT - 1;
            // Scroll screen up (simple implementation)
            char* video_memory = (char*) VIDEO_MEMORY;
            for(int i = 0; i < (SCREEN_HEIGHT - 1) * SCREEN_WIDTH * 2; i++) {
                video_memory[i] = video_memory[i + SCREEN_WIDTH * 2];
            }
        }
        return;
    }
    
    if(c == '\r') {
        cursor_x = 0;
        return;
    }
    
    char* video_memory = (char*) VIDEO_MEMORY;
    int offset = (cursor_y * SCREEN_WIDTH + cursor_x) * 2;
    video_memory[offset] = c;
    video_memory[offset + 1] = WHITE_ON_BLACK;
    
    cursor_x++;
    if(cursor_x >= SCREEN_WIDTH) {
        cursor_x = 0;
        cursor_y++;
        if(cursor_y >= SCREEN_HEIGHT) {
            cursor_y = SCREEN_HEIGHT - 1;
        }
    }
}

void print_string(const char* str) {
    while(*str) {
        print_char(*str);
        str++;
    }
}

void print_colored_text(const char* str, int color) {
    char* video_memory = (char*) VIDEO_MEMORY;
    int start_offset = (cursor_y * SCREEN_WIDTH + cursor_x) * 2;
    int offset = start_offset;
    
    while(*str) {
        if(*str == '\n') {
            cursor_x = 0;
            cursor_y++;
            offset = (cursor_y * SCREEN_WIDTH + cursor_x) * 2;
        } else {
            video_memory[offset] = *str;
            video_memory[offset + 1] = color;
            offset += 2;
            cursor_x++;
        }
        str++;
        
        if(cursor_x >= SCREEN_WIDTH) {
            cursor_x = 0;
            cursor_y++;
            offset = (cursor_y * SCREEN_WIDTH + cursor_x) * 2;
        }
    }
}

void set_cursor_position(int x, int y) {
    cursor_x = x;
    cursor_y = y;
    if(cursor_x >= SCREEN_WIDTH) cursor_x = SCREEN_WIDTH - 1;
    if(cursor_y >= SCREEN_HEIGHT) cursor_y = SCREEN_HEIGHT - 1;
}

void delay(int ms) {
    // Simple delay loop
    for(int i = 0; i < ms * 1000; i++) {
        __asm__ volatile("nop");
    }
}

void draw_border() {
    char* video_memory = (char*) VIDEO_MEMORY;
    
    // Draw top border
    for(int i = 0; i < SCREEN_WIDTH; i++) {
        video_memory[i * 2] = '=';
        video_memory[i * 2 + 1] = CYAN_ON_BLACK;
    }
    
    // Draw bottom border
    int bottom_offset = (SCREEN_HEIGHT - 1) * SCREEN_WIDTH * 2;
    for(int i = 0; i < SCREEN_WIDTH; i++) {
        video_memory[bottom_offset + i * 2] = '=';
        video_memory[bottom_offset + i * 2 + 1] = CYAN_ON_BLACK;
    }
    
    // Draw left and right borders
    for(int y = 1; y < SCREEN_HEIGHT - 1; y++) {
        video_memory[y * SCREEN_WIDTH * 2] = '|';
        video_memory[y * SCREEN_WIDTH * 2 + 1] = CYAN_ON_BLACK;
        video_memory[y * SCREEN_WIDTH * 2 + (SCREEN_WIDTH - 1) * 2] = '|';
        video_memory[y * SCREEN_WIDTH * 2 + (SCREEN_WIDTH - 1) * 2 + 1] = CYAN_ON_BLACK;
    }
}

void animate_logo() {
    set_cursor_position(30, 2);
    print_colored_text("M", RED_ON_BLACK);
    delay(100);
    print_colored_text("A", GREEN_ON_BLACK);
    delay(100);
    print_colored_text("X", BLUE_ON_BLACK);
    delay(100);
    print_colored_text("O", YELLOW_ON_BLACK);
    delay(100);
    print_colored_text("S", MAGENTA_ON_BLACK);
    delay(100);
    
    set_cursor_position(28, 3);
    print_colored_text("OPERATING SYSTEM", CYAN_ON_BLACK);
    delay(200);
    
    set_cursor_position(25, 4);
    print_colored_text("v2.0 - Enhanced Edition", BRIGHT_WHITE_ON_BLACK);
}

void print_welcome_message() {
    set_cursor_position(2, 6);
    print_colored_text("Welcome to the future of computing!", GREEN_ON_BLACK);
    set_cursor_position(2, 7);
    print_colored_text("MaxOS is now running in protected mode", YELLOW_ON_BLACK);
}

void print_system_info() {
    set_cursor_position(2, 9);
    print_colored_text("System Information:", BRIGHT_WHITE_ON_BLACK);
    set_cursor_position(2, 10);
    print_colored_text("- OS: MaxOS v2.0 Enhanced", WHITE_ON_BLACK);
    set_cursor_position(2, 11);
    print_colored_text("- Architecture: x86 (32-bit Protected Mode)", WHITE_ON_BLACK);
    set_cursor_position(2, 12);
    print_colored_text("- Memory: 16MB available", WHITE_ON_BLACK);
    set_cursor_position(2, 13);
    print_colored_text("- Status: All systems operational", GREEN_ON_BLACK);
    set_cursor_position(2, 14);
    print_colored_text("- Features: Enhanced graphics, animations", CYAN_ON_BLACK);
}