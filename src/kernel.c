#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include <limine.h>

#include "charmap.h"

// Set the base revision to 3, this is recommended as this is the latest
// base revision described by the Limine boot protocol specification.
// See specification for further info.

__attribute__((used, section(".limine_requests")))
static volatile LIMINE_BASE_REVISION(3);

// The Limine requests can be placed anywhere, but it is important that
// the compiler does not optimise them away, so, usually, they should
// be made volatile or equivalent, _and_ they should be accessed at least
// once or marked as used with the "used" attribute as done here.

__attribute__((used, section(".limine_requests")))
static volatile struct limine_framebuffer_request framebuffer_request = {
    .id = LIMINE_FRAMEBUFFER_REQUEST,
    .revision = 0
};

// Finally, define the start and end markers for the Limine requests.
// These can also be moved anywhere, to any .c file, as seen fit.

__attribute__((used, section(".limine_requests_start")))
static volatile LIMINE_REQUESTS_START_MARKER;

__attribute__((used, section(".limine_requests_end")))
static volatile LIMINE_REQUESTS_END_MARKER;

// GCC and Clang reserve the right to generate calls to the following
// 4 functions even if they are not directly called.
// Implement them as the C specification mandates.
// DO NOT remove or rename these functions, or stuff will eventually break!
// They CAN be moved to a different .c file.

void *memcpy(void *dest, const void *src, size_t n) {
    uint8_t *pdest = (uint8_t *)dest;
    const uint8_t *psrc = (const uint8_t *)src;

    for (size_t i = 0; i < n; i++) {
        pdest[i] = psrc[i];
    }

    return dest;
}

void *memset(void *s, int c, size_t n) {
    uint8_t *p = (uint8_t *)s;

    for (size_t i = 0; i < n; i++) {
        p[i] = (uint8_t)c;
    }

    return s;
}

void *memmove(void *dest, const void *src, size_t n) {
    uint8_t *pdest = (uint8_t *)dest;
    const uint8_t *psrc = (const uint8_t *)src;

    if (src > dest) {
        for (size_t i = 0; i < n; i++) {
            pdest[i] = psrc[i];
        }
    } else if (src < dest) {
        for (size_t i = n; i > 0; i--) {
            pdest[i-1] = psrc[i-1];
        }
    }

    return dest;
}

int memcmp(const void *s1, const void *s2, size_t n) {
    const uint8_t *p1 = (const uint8_t *)s1;
    const uint8_t *p2 = (const uint8_t *)s2;

    for (size_t i = 0; i < n; i++) {
        if (p1[i] != p2[i]) {
            return p1[i] < p2[i] ? -1 : 1;
        }
    }

    return 0;
}

// Halt and catch fire function.
static void hcf(void) {
    for (;;) {
        asm ("hlt");
    }
}

typedef uint32_t Color;
const Color WHITE = 0xFFFFFF;

/// @brief Calculates the address of the pixel, assuming a framebuffer with 32-bit RGB pixels.
/// @param framebuffer the framebuffer to find the pixel address for
/// @param x the x position of the pixel (left = 0)
/// @param y the y position of the pixel (top = 0)
/// @return the address of the pixel
volatile uint32_t* pixel_address(struct limine_framebuffer *framebuffer, const size_t x, const size_t y){
    return &((volatile uint32_t*)framebuffer->address)[y * (framebuffer->pitch / 4) + x];
}

/// @brief Writes a value to a pixel in the framebuffer
/// @param framebuffer the framebuffer to write to
/// @param x the x position of the pixel (left = 0)
/// @param y the y position of the pixel (top = 0)
/// @param color The color to set the pixel to
void draw_pixel(struct limine_framebuffer *framebuffer, const size_t x, const size_t y, const Color color){
    *pixel_address(framebuffer, x, y) = color;
}

volatile size_t x = 1, y = 1;

/// @brief Writes a character to the framebuffer
/// @param framebuffer The framebuffer to write the character to
/// @param c The character to write
void putchar(struct limine_framebuffer *framebuffer, const char c){
    const size_t character_height = 20;
    const size_t character_width = character_height * CHARACTER_WIDTH / CHARACTER_HEIGHT;
    const size_t character_margin = 4;
    if(c >= 32 && c <= 42){
        for(size_t i = 0;i < character_height;i++){
            volatile uint32_t *row = pixel_address(framebuffer, 0, y + i);
            for(size_t j = 0;j < character_width;j++){
                row[x + j] = WHITE * charmap[c - 32][(i * CHARACTER_HEIGHT / character_height * CHARACTER_WIDTH) + j * CHARACTER_WIDTH / character_width];
            }
        }
        x += character_width + character_margin;
        if(x + character_width > framebuffer->pitch / 4){
            putchar(framebuffer, '\n');
        }
    }else if(c == '\r'){
        x = 1;
    }else if(c == '\n'){
        if(y + character_height * 2 + character_margin < framebuffer->height){
            y += character_height + character_margin;
        }else{
            const size_t line_width = framebuffer->pitch / 4;
            const size_t line_height = character_height + character_margin;
            const size_t line_size = line_width * line_height;
            volatile uint32_t *framedata = framebuffer->address;
            for(size_t i = 0;i < (y - 1) * line_width;i++){
                framedata[i] = framedata[i + line_size];
            }
        }
        x = 1;
    }else if(c == '\b' && x > character_width + character_margin){
        x -= character_width + character_margin;
    }
}

/// @brief Writes a string to a framebuffer
/// @param framebuffer the framebuffer to write to
/// @param string the string to write to the framebuffer
/// @param length the length of the string, function will stop at this index or '\0'
void puts(struct limine_framebuffer *framebuffer, const char *string, const size_t length){
    for(size_t i = 0;i < length && string[i] != '\0';i++){
        putchar(framebuffer, string[i]);
    }
}

// The following will be our kernel's entry point.
// If renaming kmain() to something else, make sure to change the
// linker script accordingly.
void kmain(void) {
    // Ensure the bootloader actually understands our base revision (see spec).
    if (LIMINE_BASE_REVISION_SUPPORTED == false) {
        hcf();
    }

    // Ensure we got a framebuffer.
    if (framebuffer_request.response == NULL
     || framebuffer_request.response->framebuffer_count < 1) {
        hcf();
    }

    // Fetch the first framebuffer.
    struct limine_framebuffer *framebuffer = framebuffer_request.response->framebuffers[0];

    const char message[] = "!\r! \"#$%&'()*\b*+!";
    puts(framebuffer, message, sizeof(message) - 1);

    // We're done, just hang...
    hcf();
}