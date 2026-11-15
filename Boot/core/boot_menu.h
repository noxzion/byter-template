#ifndef BYTER_BOOT_MENU_H
#define BYTER_BOOT_MENU_H

#include "boot_catalog.h"

/* Returns the selected option index; a timeout or unavailable input selects zero. */
unsigned int byter_boot_menu_wait(const struct byter_boot_option *options,
                                  unsigned int option_count);

#endif
