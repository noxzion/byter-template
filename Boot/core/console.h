#ifndef BYTER_CONSOLE_H
#define BYTER_CONSOLE_H

#include "../include/byter.h"

void byter_console_set_framebuffer(const struct byter_framebuffer *framebuffer);
void byter_console_write(const char *text);

#endif
