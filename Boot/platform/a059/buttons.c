#include "buttons.h"

typedef unsigned int u32;
typedef unsigned char u8;
typedef unsigned long long u64;

#define ARB_CORE       0x0c400000ULL
#define ARB_CHANNELS   0x0c500000ULL
#define ARB_OBSERVER   0x0c440000ULL
#define ARB_CONFIG     0x0c42d000ULL
#define ARB_VERSION    0x0000
#define ARB_FEATURES   0x0004
#define ARB_APID_MAP   0x2000
#define ARB_APID_OWNER 0x0000
#define ARB_V7_MIN     0x70000000U
#define ARB_V8_MIN     0x80000000U
#define ARB_MAX_APIDS  1024U
#define SPMI_DONE      (1U << 0)
#define SPMI_ERROR     (0x0eU)
#define SPMI_EXT_READL 1U
#define SPMI_EXT_WRITEL 0U
#define TLMM_GPIO_BASE 0x0f100000ULL
#define A059_ESSENTIAL_GPIO 71U
#define A059_REBOOT_REASON 0x48U
#define A059_FASTBOOT_REASON 0x04U

static u32 mmio_read(u64 address) {
    return *(volatile u32 *)address;
}

static void mmio_write(u64 address, u32 value) {
    *(volatile u32 *)address = value;
    __asm__ volatile("dsb sy" ::: "memory");
}

static u32 apid_count;
static u32 pmk8550_pon_apid = ~0U;
static u32 pmk8550_sdam_apid = ~0U;
static u32 pm7550_gpio_apid = ~0U;
static int ready;

int byter_a059_buttons_init(void) {
    u32 version = mmio_read(ARB_CORE + ARB_VERSION);
    if (version < ARB_V7_MIN || version >= ARB_V8_MIN) return 0;

    apid_count = mmio_read(ARB_CORE + ARB_FEATURES) & 0x7ffU;
    if (!apid_count || apid_count > ARB_MAX_APIDS) return 0;

    for (u32 apid = 0; apid < apid_count; ++apid) {
        u32 mapping = mmio_read(ARB_CORE + ARB_APID_MAP + (u64)apid * 4);
        if (!mapping) continue;
        u32 ppid = (mapping >> 8) & 0xfffU;
        u32 write_ee = mmio_read(ARB_CONFIG + ARB_APID_OWNER + (u64)apid * 4) & 7U;
        if (write_ee != 0) continue;
        if (ppid == ((0U << 8) | 0x13U)) pmk8550_pon_apid = apid;
        if (ppid == ((0U << 8) | 0x71U)) pmk8550_sdam_apid = apid;
        if (ppid == ((1U << 8) | 0x8dU)) pm7550_gpio_apid = apid;
    }

    ready = pmk8550_pon_apid != ~0U && pm7550_gpio_apid != ~0U;
    return ready;
}

static int spmi_read(u32 apid, u8 register_address, u8 *value) {
    u64 channel = ARB_OBSERVER + (u64)apid * 0x20;
    u32 command = (SPMI_EXT_READL << 27) |
                  ((u32)register_address << 4); /* One byte: byte-count is zero. */
    mmio_write(channel, command);

    u64 frequency;
    __asm__ volatile("mrs %0, cntfrq_el0" : "=r"(frequency));
    u64 start;
    __asm__ volatile("mrs %0, cntpct_el0" : "=r"(start));
    u64 timeout = frequency / 1000;
    if (!timeout) timeout = 100000;

    for (u32 spins = 0; spins < 1000000; ++spins) {
        u32 status = mmio_read(channel + 8);
        if (status & SPMI_DONE) {
            if (status & SPMI_ERROR) return 0;
            *value = (u8)mmio_read(channel + 0x18);
            return 1;
        }
        if (frequency) {
            u64 now;
            __asm__ volatile("mrs %0, cntpct_el0" : "=r"(now));
            if (now - start >= timeout) return 0;
        }
    }
    return 0;
}

static int spmi_write(u32 apid, u8 register_address, u8 value) {
    u64 channel = ARB_CHANNELS + (u64)apid * 0x1000;
    u32 command = (SPMI_EXT_WRITEL << 27) |
                  ((u32)register_address << 4); /* One byte: byte-count is zero. */
    mmio_write(channel + 0x10, value);
    mmio_write(channel, command);

    u64 frequency;
    __asm__ volatile("mrs %0, cntfrq_el0" : "=r"(frequency));
    u64 start;
    __asm__ volatile("mrs %0, cntpct_el0" : "=r"(start));
    u64 timeout = frequency / 1000;
    if (!timeout) timeout = 100000;

    for (u32 spins = 0; spins < 1000000; ++spins) {
        u32 status = mmio_read(channel + 8);
        if (status & SPMI_DONE) return !(status & SPMI_ERROR);
        if (frequency) {
            u64 now;
            __asm__ volatile("mrs %0, cntpct_el0" : "=r"(now));
            if (now - start >= timeout) return 0;
        }
    }
    return 0;
}

int byter_a059_buttons_read(unsigned *pressed) {
    if (!ready || !pressed) return 0;
    u8 pon, volume_up;
    /* Only SPMI read transactions are issued; never write PMIC peripheral regs. */
    if (!spmi_read(pmk8550_pon_apid, 0x10, &pon) ||
        !spmi_read(pm7550_gpio_apid, 0x10, &volume_up)) return 0;

    unsigned keys = 0;
    if (!(volume_up & 1U)) keys |= BYTER_BUTTON_VOLUME_UP; /* Active-low GPIO. */
    if (pon & (1U << 6)) keys |= BYTER_BUTTON_VOLUME_DOWN; /* PON resin status. */
    if (pon & (1U << 7)) keys |= BYTER_BUTTON_POWER;        /* PON key status. */
    /* TLMM GPIO input is bit 0 at GPIO_CONFIG + 4; the key is active-low. */
    u64 gpio = TLMM_GPIO_BASE + (u64)A059_ESSENTIAL_GPIO * 0x1000;
    if (!(mmio_read(gpio + 4) & 1U)) keys |= BYTER_BUTTON_ESSENTIAL;
    *pressed = keys;
    return 1;
}

int byter_a059_reboot_fastboot(void) {
    if (!ready || pmk8550_sdam_apid == ~0U) return 0;

    u8 current;
    if (!spmi_read(pmk8550_sdam_apid, A059_REBOOT_REASON, &current)) return 0;
    /* DTS reboot-mode uses value 2 in bits 1..7, i.e. raw SDAM bits = 0x04. */
    u8 reason = (u8)((current & 1U) | A059_FASTBOOT_REASON);
    if (!spmi_write(pmk8550_sdam_apid, A059_REBOOT_REASON, reason)) {
        spmi_write(pmk8550_sdam_apid, A059_REBOOT_REASON, current);
        return 0;
    }

    u8 verify;
    if (!spmi_read(pmk8550_sdam_apid, A059_REBOOT_REASON, &verify) ||
        verify != reason) {
        spmi_write(pmk8550_sdam_apid, A059_REBOOT_REASON, current);
        return 0;
    }

    register u64 x0 __asm__("x0") = 0x84000009ULL; /* PSCI_SYSTEM_RESET */
    __asm__ volatile("smc #0" : "+r"(x0) : : "x1", "x2", "x3", "memory");
    spmi_write(pmk8550_sdam_apid, A059_REBOOT_REASON, current);
    return 0;
}
