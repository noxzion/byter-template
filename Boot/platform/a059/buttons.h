#ifndef BYTER_A059_BUTTONS_H
#define BYTER_A059_BUTTONS_H

#define BYTER_BUTTON_VOLUME_UP   (1U << 0)
#define BYTER_BUTTON_VOLUME_DOWN (1U << 1)
#define BYTER_BUTTON_POWER      (1U << 2)
#define BYTER_BUTTON_ESSENTIAL  (1U << 3)

/* The volume and power buttons are sampled through read-only PMIC access. */
int byter_a059_buttons_init(void);
int byter_a059_buttons_read(unsigned *pressed);

/* Stores the A059 bootloader reboot reason and requests a PSCI system reset. */
int byter_a059_reboot_fastboot(void);

#endif
