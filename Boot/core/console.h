#ifndef BYTER_CONSOLE_H
#define BYTER_CONSOLE_H

#include "../include/byter.h"

void byter_console_set_framebuffer(const struct byter_framebuffer *framebuffer);
void byter_console_write(const char *text);
int byter_console_available(void);
void byter_console_get_cursor(unsigned int *x, unsigned int *y);
int byter_console_set_cursor(unsigned int x, unsigned int y);
void byter_console_get_size(unsigned int *width, unsigned int *height);

#endif
