#ifndef BYTER_A059_FRAMEBUFFER_H
#define BYTER_A059_FRAMEBUFFER_H

#include "byter.h"

/* Last-resort display mode known to work when ABL omits simple-framebuffer. */
int byter_a059_framebuffer_fallback(struct byter_framebuffer *framebuffer);

#endif
