/*
 * simtest.c - tiny test harness around the simavr library (libsimavr).
 *
 * WHY THIS EXISTS: the stock "run_avr" program just runs firmware. For tests
 * I need numbers: "at which CPU cycle did PB5 change?", "what bytes came out
 * of the UART and when?", "how many cycles did the tick interrupt take?".
 * This program runs an ELF file inside the simulated ATmega328P and prints one
 * line per event. tests/run_tests.py reads those lines and checks them.
 * (This file is test tooling for the PC. It is NOT part of the AVR firmware.)
 *
 * Output lines (all numbers are decimal unless 0x):
 *   U <cycle> <byte>               a byte left the UART transmitter
 *   P <cycle> <Pn> <0|1>           pin Pn (e.g. B5) changed level
 *   V <enter> <exit> <kb> <ka>     one run of the watched interrupt vector;
 *                                  kb/ka = 16-bit watched variable before/after
 *   S <name> <hex bytes>           memory dump of a symbol at the end
 *   M <addr> <hex bytes>           raw memory dump at the end
 *   A <name> <addr>                address of a symbol (function = flash BYTE address)
 *   E <cycle> <pc> <sp> <state>    final state
 *
 * Input: -r <ms> <byte> sends one byte to the firmware's UART receiver at that time
 * (like the dashboard sending 'T'). simavr's own echo of UART text is switched off,
 * so stdout holds only the records above.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "sim_avr.h"
#include "sim_elf.h"
#include "avr_ioport.h"
#include "avr_uart.h"

static avr_t *avr;

static void on_uart(struct avr_irq_t *irq, uint32_t value, void *param)
{
    (void)irq; (void)param;
    printf("U %llu %u\n", (unsigned long long)avr->cycle, value & 0xFF);
}

typedef struct { char port; int bit; } pin_t;

static void on_pin(struct avr_irq_t *irq, uint32_t value, void *param)
{
    (void)irq;
    pin_t *p = param;
    printf("P %llu %c%d %u\n", (unsigned long long)avr->cycle, p->port, p->bit, value & 1);
}

/* Look up a symbol (variable or function) address in the ELF symbol table. */
static int find_sym(elf_firmware_t *f, const char *name, uint32_t *addr, uint32_t *size)
{
    for (uint32_t i = 0; i < f->symbolcount; i++) {
        if (strcmp(f->symbol[i]->symbol, name) == 0) {
            *addr = f->symbol[i]->addr;
            *size = f->symbol[i]->size;
            return 1;
        }
    }
    return 0;
}

static uint16_t rd16(uint32_t a) { return avr->data[a] | (avr->data[a + 1] << 8); }

static void dump(uint32_t a, uint32_t n)
{
    for (uint32_t i = 0; i < n; i++) printf("%02x", avr->data[a + i]);
    printf("\n");
}

int main(int argc, char **argv)
{
    const char *file = NULL;
    unsigned long long max_cycles = 16000000ULL / 10;   /* default 100 ms */
    int vector = -1;                                     /* -v N : watch vector N */
    const char *watch_sym = NULL;                        /* -k sym: 16-bit var to sample */
    const char *stop_sym = NULL;                         /* -b sym: stop at this function */
    const char *dump_syms[16]; int ndump = 0;
    const char *addr_syms[16]; int naddr = 0;
    unsigned dump_raw[8][2]; int nraw = 0;
    pin_t pins[16]; int npins = 0;
    int want_uart = 0;
    unsigned long long rx_at[8]; unsigned rx_byte[8]; int nrx = 0, rx_next = 0;

    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "-c") && i + 1 < argc) max_cycles = strtoull(argv[++i], 0, 0);
        else if (!strcmp(argv[i], "-ms") && i + 1 < argc) max_cycles = strtoull(argv[++i], 0, 0) * 16000ULL;
        else if (!strcmp(argv[i], "-u")) want_uart = 1;
        else if (!strcmp(argv[i], "-p") && i + 1 < argc && npins < 16) {
            pins[npins].port = argv[i + 1][0]; pins[npins].bit = atoi(argv[i + 1] + 1); npins++; i++;
        }
        else if (!strcmp(argv[i], "-v") && i + 1 < argc) vector = atoi(argv[++i]);
        else if (!strcmp(argv[i], "-k") && i + 1 < argc) watch_sym = argv[++i];
        else if (!strcmp(argv[i], "-b") && i + 1 < argc) stop_sym = argv[++i];
        else if (!strcmp(argv[i], "-s") && i + 1 < argc && ndump < 16) dump_syms[ndump++] = argv[++i];
        else if (!strcmp(argv[i], "-a") && i + 1 < argc && naddr < 16) addr_syms[naddr++] = argv[++i];
        else if (!strcmp(argv[i], "-m") && i + 2 < argc && nraw < 8) {
            dump_raw[nraw][0] = strtoul(argv[i + 1], 0, 0); dump_raw[nraw][1] = strtoul(argv[i + 2], 0, 0);
            nraw++; i += 2;
        }
        else if (!strcmp(argv[i], "-r") && i + 2 < argc && nrx < 8) {
            rx_at[nrx] = strtoull(argv[i + 1], 0, 0) * 16000ULL; rx_byte[nrx] = strtoul(argv[i + 2], 0, 0);
            nrx++; i += 2;
        }
        else if (argv[i][0] != '-') file = argv[i];
        else { fprintf(stderr, "unknown option %s\n", argv[i]); return 2; }
    }
    if (!file) { fprintf(stderr, "usage: simtest file.elf [-ms N|-c N] [-u] [-p B5].. [-v N -k sym] [-b func] [-s sym].. [-a sym].. [-m addr len].. [-r ms byte]..\n"); return 2; }

    static elf_firmware_t f;
    memset(&f, 0, sizeof f);
    if (elf_read_firmware(file, &f) != 0) { fprintf(stderr, "cannot read %s\n", file); return 2; }
    strcpy(f.mmcu, "atmega328p");
    f.frequency = 16000000;                      /* Arduino UNO crystal */
    avr = avr_make_mcu_by_name(f.mmcu);
    if (!avr) { fprintf(stderr, "no such mcu\n"); return 2; }
    avr_init(avr);
    avr_load_firmware(avr, &f);

    uint32_t uflags = 0;
    avr_ioctl(avr, AVR_IOCTL_UART_GET_FLAGS('0'), &uflags);
    uflags &= ~AVR_UART_FLAG_STDIO;                      /* no echo of UART text on stdout */
    avr_ioctl(avr, AVR_IOCTL_UART_SET_FLAGS('0'), &uflags);
    if (want_uart)
        avr_irq_register_notify(avr_io_getirq(avr, AVR_IOCTL_UART_GETIRQ('0'), UART_IRQ_OUTPUT), on_uart, NULL);
    for (int i = 0; i < npins; i++)
        avr_irq_register_notify(avr_io_getirq(avr, AVR_IOCTL_IOPORT_GETIRQ(pins[i].port), pins[i].bit), on_pin, &pins[i]);

    uint32_t vec_pc = vector >= 0 ? (uint32_t)vector * 4 : 0xFFFFFFFF;   /* 2 words = 4 bytes per vector */
    uint32_t wa = 0, ws = 0, sa = 0, ss = 0;
    if (watch_sym && !find_sym(&f, watch_sym, &wa, &ws)) { fprintf(stderr, "no symbol %s\n", watch_sym); return 2; }
    wa &= 0xFFFF;
    if (stop_sym) {
        if (!find_sym(&f, stop_sym, &sa, &ss)) { fprintf(stderr, "no symbol %s\n", stop_sym); return 2; }
    }

    int in_isr = 0; unsigned long long enter = 0; unsigned kb = 0;
    while (avr->cycle < max_cycles) {
        if (rx_next < nrx && avr->cycle >= rx_at[rx_next])  /* a byte "typed" into the UART receiver */
            avr_raise_irq(avr_io_getirq(avr, AVR_IOCTL_UART_GETIRQ('0'), UART_IRQ_INPUT), rx_byte[rx_next++]);
        uint32_t pc = avr->pc;
        if (stop_sym && pc == sa) break;
        uint16_t op = avr->flash[pc] | (avr->flash[pc + 1] << 8);
        if (pc == vec_pc && !in_isr) {                 /* interrupt just taken */
            in_isr = 1; enter = avr->cycle; kb = watch_sym ? rd16(wa) : 0;
        }
        int is_reti = (op == 0x9518);
        int st = avr_run(avr);
        if (in_isr && is_reti) {                       /* RETI executed: ISR is over */
            printf("V %llu %llu %u %u\n", enter, (unsigned long long)avr->cycle, kb, watch_sym ? rd16(wa) : 0);
            in_isr = 0;
        }
        if (st == cpu_Done || st == cpu_Crashed) break;
    }

    for (int i = 0; i < ndump; i++) {
        uint32_t a, s;
        if (!find_sym(&f, dump_syms[i], &a, &s)) { printf("S %s MISSING\n", dump_syms[i]); continue; }
        printf("S %s ", dump_syms[i]); dump(a & 0xFFFF, s);
    }
    for (int i = 0; i < naddr; i++) {
        uint32_t a, s;
        if (find_sym(&f, addr_syms[i], &a, &s)) printf("A %s 0x%x\n", addr_syms[i], a & 0xFFFF);
        else printf("A %s MISSING\n", addr_syms[i]);
    }
    for (int i = 0; i < nraw; i++) { printf("M 0x%x ", dump_raw[i][0]); dump(dump_raw[i][0], dump_raw[i][1]); }
    printf("E %llu %u %u %d\n", (unsigned long long)avr->cycle, avr->pc,
           avr->data[0x5d] | (avr->data[0x5e] << 8), avr->state);
    return 0;
}
