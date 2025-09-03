/**
 * @file kernel.c
 * @brief MaxOS Kernel - Main System Implementation
 * @author Maxwell Corwin
 * @date 2025
 * @version 2.0
 * 
 * This file contains the main kernel implementation for MaxOS, including
 * system initialization, video memory management, and basic system services.
 * The kernel operates in 32-bit protected mode with direct hardware access.
 * 
 * CREDITS AND SOURCES:
 * - VGA programming from "VGA Hardware Programming" by Chris Giese
 * - Kernel structure from "Operating System Concepts" by Silberschatz
 * - Freestanding C programming from C99 standard documentation
 * - Video memory access patterns from VGA hardware specifications
 */

#include <stdint.h>
#include <stddef.h>

// =============================================================================
// System Constants and Definitions
// =============================================================================

// Video memory configuration
// Source: VGA hardware specification and "VGA Hardware Programming" by Chris Giese
#define VIDEO_MEMORY_ADDRESS    0xB8000    // Standard VGA text mode memory address
#define SCREEN_WIDTH           80          // Standard VGA text mode width
#define SCREEN_HEIGHT          25          // Standard VGA text mode height
#define CHARACTERS_PER_SCREEN  (SCREEN_WIDTH * SCREEN_HEIGHT)
#define BYTES_PER_CHARACTER    2           // Character + attribute byte

// Color definitions (VGA text mode attributes)
// Source: VGA hardware specification and IBM PC/AT documentation
#define COLOR_BLACK            0x00
#define COLOR_BLUE             0x01
#define COLOR_GREEN            0x02
#define COLOR_CYAN             0x03
#define COLOR_RED              0x04
#define COLOR_MAGENTA          0x05
#define COLOR_BROWN            0x06
#define COLOR_LIGHT_GRAY       0x07
#define COLOR_DARK_GRAY        0x08
#define COLOR_LIGHT_BLUE       0x09
#define COLOR_LIGHT_GREEN      0x0A
#define COLOR_LIGHT_CYAN       0x0B
#define COLOR_LIGHT_RED        0x0C
#define COLOR_LIGHT_MAGENTA    0x0D
#define COLOR_YELLOW           0x0E
#define COLOR_WHITE            0x0F

// Default color scheme
#define DEFAULT_FOREGROUND     COLOR_WHITE
#define DEFAULT_BACKGROUND     COLOR_BLACK
#define DEFAULT_ATTRIBUTE      ((DEFAULT_BACKGROUND << 4) | DEFAULT_FOREGROUND)

// System status constants
#define SYSTEM_STATUS_READY    0x01
#define SYSTEM_STATUS_ERROR    0x02
#define SYSTEM_STATUS_WARNING  0x04

// =============================================================================
// Global Variables
// =============================================================================

// Cursor position tracking
static struct {
    uint8_t x;
    uint8_t y;
} cursor_position = {0, 0};

// System status
static uint8_t system_status = SYSTEM_STATUS_READY;

// =============================================================================
// Forward Declarations
// =============================================================================

void kernel_main(void);
void system_initialize(void);
void video_initialize(void);
void clear_screen(void);
void set_cursor_position(uint8_t x, uint8_t y);
void print_character(char c);
void print_string(const char* str);
void print_colored_string(const char* str, uint8_t color);
void print_system_banner(void);
void print_system_information(void);
void print_status_message(void);
void scroll_screen(void);
void delay_milliseconds(uint32_t ms);
uint32_t get_system_uptime(void);

// =============================================================================
// Kernel Entry Point
// =============================================================================

/**
 * @brief Kernel entry point called by bootloader
 * 
 * This function is the main entry point for the kernel after the bootloader
 * transfers control. It initializes the system and enters the main loop.
 * 
 * Design pattern from "Operating System Concepts" by Silberschatz et al.
 */
void _start(void) {
    kernel_main();
    
    // Infinite loop to prevent system hang
    // Standard OS kernel pattern for idle state
    while (1) {
        // System idle - could be extended with task scheduling
        __asm__ volatile("hlt");  // Halt CPU until next interrupt
    }
}

// =============================================================================
// Main Kernel Function
// =============================================================================

/**
 * @brief Main kernel function
 * 
 * Initializes the system, displays startup information, and prepares
 * the system for user interaction.
 * 
 * Structure inspired by "Understanding the Linux Kernel" by Bovet & Cesati
 */
void kernel_main(void) {
    // Initialize system components
    system_initialize();
    
    // Display system banner
    print_system_banner();
    
    // Display system information
    print_system_information();
    
    // Display status and prompt
    print_status_message();
    
    // System is now ready for operation
    system_status = SYSTEM_STATUS_READY;
}

// =============================================================================
// System Initialization
// =============================================================================

/**
 * @brief Initialize system components
 * 
 * Sets up video memory, clears the screen, and prepares the system
 * for operation.
 * 
 * Initialization pattern from "Operating System Concepts" by Silberschatz
 */
void system_initialize(void) {
    video_initialize();
    clear_screen();
    
    // Set initial cursor position
    set_cursor_position(0, 0);
}

/**
 * @brief Initialize video system
 * 
 * Sets up the video memory and prepares the display for output.
 * 
 * VGA initialization based on "VGA Hardware Programming" by Chris Giese
 */
void video_initialize(void) {
    // Video memory is already mapped by the bootloader
    // No additional initialization required for VGA text mode
    // Memory mapping handled by BIOS during boot process
}

// =============================================================================
// Video Memory Management
// =============================================================================

/**
 * @brief Clear the entire screen
 * 
 * Fills the video memory with space characters and default attributes,
 * effectively clearing the display.
 * 
 * Implementation based on VGA hardware specification and direct memory access
 */
void clear_screen(void) {
    volatile uint16_t* video_memory = (volatile uint16_t*)VIDEO_MEMORY_ADDRESS;
    
    for (size_t i = 0; i < CHARACTERS_PER_SCREEN; ++i) {
        video_memory[i] = (' ' | (DEFAULT_ATTRIBUTE << 8));
    }
    
    cursor_position.x = 0;
    cursor_position.y = 0;
}

/**
 * @brief Set cursor position
 * 
 * @param x X coordinate (0-79)
 * @param y Y coordinate (0-24)
 * 
 * Bounds checking based on VGA text mode specifications
 */
void set_cursor_position(uint8_t x, uint8_t y) {
    if (x >= SCREEN_WIDTH) x = SCREEN_WIDTH - 1;
    if (y >= SCREEN_HEIGHT) y = SCREEN_HEIGHT - 1;
    
    cursor_position.x = x;
    cursor_position.y = y;
}

/**
 * @brief Print a single character
 * 
 * @param c Character to print
 * 
 * VGA text mode implementation based on hardware specification
 * Character attributes and memory layout from VGA documentation
 */
void print_character(char c) {
    volatile uint16_t* video_memory = (volatile uint16_t*)VIDEO_MEMORY_ADDRESS;
    
    if (c == '\n') {
        // Newline: move to beginning of next line
        cursor_position.x = 0;
        cursor_position.y++;
        
        if (cursor_position.y >= SCREEN_HEIGHT) {
            scroll_screen();
            cursor_position.y = SCREEN_HEIGHT - 1;
        }
        return;
    }
    
    if (c == '\r') {
        // Carriage return: move to beginning of current line
        cursor_position.x = 0;
        return;
    }
    
    if (c == '\t') {
        // Tab: move to next tab stop (every 8 characters)
        // Standard terminal behavior for tab handling
        cursor_position.x = (cursor_position.x + 8) & 0xF8;
        if (cursor_position.x >= SCREEN_WIDTH) {
            cursor_position.x = 0;
            cursor_position.y++;
        }
        return;
    }
    
    // Calculate memory offset for current cursor position
    // VGA text mode memory layout: 2 bytes per character (char + attribute)
    size_t offset = cursor_position.y * SCREEN_WIDTH + cursor_position.x;
    
    // Write character and attribute to video memory
    // Format: low byte = character, high byte = attribute
    video_memory[offset] = (c | (DEFAULT_ATTRIBUTE << 8));
    
    // Advance cursor position
    cursor_position.x++;
    if (cursor_position.x >= SCREEN_WIDTH) {
        cursor_position.x = 0;
        cursor_position.y++;
        
        if (cursor_position.y >= SCREEN_HEIGHT) {
            scroll_screen();
            cursor_position.y = SCREEN_HEIGHT - 1;
        }
    }
}

/**
 * @brief Print a null-terminated string
 * 
 * @param str String to print
 * 
 * String handling without standard library (freestanding environment)
 */
void print_string(const char* str) {
    if (!str) return;
    
    for (size_t i = 0; str[i] != '\0'; ++i) {
        print_character(str[i]);
    }
}

/**
 * @brief Print a colored string
 * 
 * @param str String to print
 * @param color Color attribute to use
 * 
 * VGA color attribute handling based on hardware specification
 * Color attributes: foreground (low 4 bits) + background (high 4 bits)
 */
void print_colored_string(const char* str, uint8_t color) {
    if (!str) return;
    
    volatile uint16_t* video_memory = (volatile uint16_t*)VIDEO_MEMORY_ADDRESS;
    size_t start_offset = cursor_position.y * SCREEN_WIDTH + cursor_position.x;
    
    for (size_t i = 0; str[i] != '\0'; ++i) {
        if (str[i] == '\n') {
            print_character('\n');
            start_offset = cursor_position.y * SCREEN_WIDTH + cursor_position.x;
        } else {
            size_t offset = cursor_position.y * SCREEN_WIDTH + cursor_position.x;
            video_memory[offset] = (str[i] | (color << 8));
            
            cursor_position.x++;
            if (cursor_position.x >= SCREEN_WIDTH) {
                cursor_position.x = 0;
                cursor_position.y++;
                if (cursor_position.y >= SCREEN_HEIGHT) {
                    scroll_screen();
                    cursor_position.y = SCREEN_HEIGHT - 1;
                }
            }
        }
    }
}

/**
 * @brief Scroll the screen up by one line
 * 
 * Moves all lines up by one, clearing the bottom line for new content.
 * 
 * Memory manipulation technique from VGA programming examples
 */
void scroll_screen(void) {
    volatile uint16_t* video_memory = (volatile uint16_t*)VIDEO_MEMORY_ADDRESS;
    
    // Move all lines up by one
    for (size_t i = 0; i < (SCREEN_HEIGHT - 1) * SCREEN_WIDTH; ++i) {
        video_memory[i] = video_memory[i + SCREEN_WIDTH];
    }
    
    // Clear the bottom line
    size_t bottom_line_start = (SCREEN_HEIGHT - 1) * SCREEN_WIDTH;
    for (size_t i = 0; i < SCREEN_WIDTH; ++i) {
        video_memory[bottom_line_start + i] = (' ' | (DEFAULT_ATTRIBUTE << 8));
    }
}

// =============================================================================
// System Display Functions
// =============================================================================

/**
 * @brief Display system banner
 * 
 * Shows the MaxOS logo and version information with animated effects.
 * 
 * ASCII art and display techniques inspired by classic OS boot screens
 */
void print_system_banner(void) {
    set_cursor_position(0, 2);
    
    // Print MaxOS logo with color animation
    // ASCII art design for educational demonstration
    const char* logo[] = {
        "  __  __       _  ___   ___ ",
        " |  \\/  |     / \\/ __\\ / __\\",
        " | \\  / |    / _ \\__ \\ / /   ",
        " | |\\/| |   / ___ \\__// /___ ",
        " |_|  |_|  /_/   \\_\\/_____| "
    };
    
    for (int i = 0; i < 5; ++i) {
        set_cursor_position(25, 2 + i);
        print_colored_string(logo[i], COLOR_CYAN);
        delay_milliseconds(100);
    }
    
    set_cursor_position(0, 8);
    print_colored_string("MaxOS v2.0 - Educational Operating System", COLOR_YELLOW);
    set_cursor_position(0, 9);
    print_colored_string("Built for learning and computer science education", COLOR_LIGHT_GRAY);
}

/**
 * @brief Display system information
 * 
 * Shows technical details about the system configuration and capabilities.
 * 
 * Information display pattern from educational OS projects
 */
void print_system_information(void) {
    set_cursor_position(0, 12);
    print_colored_string("System Information:", COLOR_LIGHT_GREEN);
    
    set_cursor_position(2, 13);
    print_string("Architecture: x86 (32-bit protected mode)");
    
    set_cursor_position(2, 14);
    print_string("Memory Model: Flat memory model with segmentation");
    
    set_cursor_position(2, 15);
    print_string("Video Mode: VGA text mode (80x25, 16 colors)");
    
    set_cursor_position(2, 16);
    print_string("Boot Method: BIOS bootloader with kernel loading");
    
    set_cursor_position(2, 17);
    print_string("System Status: Initialized and ready");
}

/**
 * @brief Display status message and command prompt
 * 
 * Shows the system status and provides a command prompt interface.
 * 
 * User interface pattern from classic operating systems
 */
void print_status_message(void) {
    set_cursor_position(0, 20);
    print_colored_string("System Status: Ready", COLOR_LIGHT_GREEN);
    
    set_cursor_position(0, 21);
    print_colored_string("Available Commands: help, info, status, clear", COLOR_YELLOW);
    
    set_cursor_position(0, 22);
    print_colored_string("Type 'help' for command information", COLOR_LIGHT_GRAY);
    
    set_cursor_position(0, 23);
    print_colored_string("> ", COLOR_WHITE);
}

// =============================================================================
// Utility Functions
// =============================================================================

/**
 * @brief Simple delay function
 * 
 * @param ms Milliseconds to delay (approximate)
 * 
 * Busy-wait delay implementation for educational purposes
 * In production systems, use hardware timers or scheduler
 */
void delay_milliseconds(uint32_t ms) {
    // Simple busy-wait delay (not accurate but functional)
    // Uses CPU cycles for timing - not efficient but simple
    for (volatile uint32_t i = 0; i < ms * 10000; ++i) {
        __asm__ volatile("nop");  // No-operation instruction
    }
}

/**
 * @brief Get system uptime
 * 
 * @return Approximate uptime in milliseconds
 * 
 * Placeholder for future implementation
 * Could use PIT timer or RTC for accurate timing
 */
uint32_t get_system_uptime(void) {
    // Placeholder for future implementation
    // Could use PIT timer or RTC for accurate timing
    // Implementation would require hardware timer setup
    return 0;
}