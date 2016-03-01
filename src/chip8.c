/*
 * chip8 is a CHIP-8 emulator done in C
 * Copyright (C) 2015 Dani Rodríguez <danirod@outlook.com>
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include "cpu.h"
#include "sdl.h"
#include "../config.h"

#include <getopt.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <SDL2/SDL.h>

/* Flag set by '--hex' */
static int use_hexloader;

/* getopt parameter structure. */
static struct option long_options[] = {
    { "help", no_argument, 0, 'h' },
    { "version", no_argument, 0, 'v' },
    { "hex", no_argument, &use_hexloader, 1 },
    { 0, 0, 0, 0 }
};

/**
 * Print usage. In case you use bad arguments, this will be printed.
 * @param name how is the program named, usually argv[0].
 */
static void
usage(const char* name)
{
    printf("Usage: %s [-h | --help] [-v | --version] [--hex] <file>\n", name);
}

char
hex_to_bin(char hex)
{
    if (hex >= '0' && hex <= '9')
        return hex - '0';
    hex &= 0xDF;
    if (hex >= 'A' && hex <= 'F')
        return 10 + (hex - 'A');
    return -1;
}

/**
 * Load a hex file.
 *
 * @param file file path.
 * @param machine data structure to load the HEX into.
 */
static int
load_hex(const char* file, struct machine_t* machine)
{
    FILE* fp = fopen(file, "rb");
    if (fp == NULL) {
        fprintf(stderr, "Cannot open ROM file.\n");
        return 1;
    }

    // Use the fseek/ftell/fseek trick to retrieve file size.
    fseek(fp, 0, SEEK_END);
    int length = ftell(fp);
    fseek(fp, 0, SEEK_SET);

    // Create a temporal buffer where to store the data.
    char* hexfile = malloc(length);
    if (hexfile == NULL) {
        return 1;
    }

    fread(hexfile, length, 1, fp);
    fclose(fp);

    int mempos = 0x200;

    if (length & 0x01) length--;
    for (int i = 0; i < length; i += 2)
    {
        char hi = hexfile[i];
        char lo = hexfile[i + 1];

        char hi_b = hex_to_bin(hi);
        char lo_b = hex_to_bin(lo);
        if (hi_b == -1 || lo_b == -1) {
            free(hexfile);
            return 1;
        }

        machine->mem[mempos++] = hi_b << 4 | lo_b;
        if (mempos > 0xFFF)
            break;
    }

    free(hexfile);
    return 0;
}

/**
 * Load a ROM into a machine. This function will open a file and load its
 * contents into the memory from the provided machine data structure.
 * In compliance with the specification, ROM data will start at 0x200.
 *
 * @param file file path.
 * @param machine machine data structure to load the ROM into.
 */
static int
load_rom(const char* file, struct machine_t* machine)
{
    FILE* fp = fopen(file, "rb");
    if (fp == NULL) {
        fprintf(stderr, "Cannot open ROM file.\n");
        return 1;
    }

    // Use the fseek/ftell/fseek trick to retrieve file size.
    fseek(fp, 0, SEEK_END);
    int length = ftell(fp);
    fseek(fp, 0, SEEK_SET);

    // Check the length of the rom. Must be as much 3584 bytes long, which
    // is 4096 - 512. Since first 512 bytes of memory are reserved, program
    // code can only allocate up to 3584 bytes. Must check for bounds in
    // order to avoid buffer overflows.
    if (length > 3584) {
        fprintf(stderr, "ROM too large.\n");
        return 1;
    }

    // Everything is OK, read the ROM.
    fread(machine->mem + 0x200, length, 1, fp);
    fclose(fp);
    return 0;
}

int
main(int argc, char** argv)
{
    struct machine_t mac;

    /* Parse parameters */
    int indexptr, c;
    while ((c = getopt_long(argc, argv, "hv", long_options, &indexptr)) != -1) {
        switch (c) {
            case 'h':
                /* Print help message. */
                usage(argv[0]);
                exit(0);
            case 'v':
                /* Print version information. */
                printf("%s\n", PACKAGE_STRING);
                exit(0);
                break;
            case 0:
                /* A long option is being processed, probably --hex. */
                break;
            default:
                /* Wrong argument. */
                exit(1);
        }
    }

    /*
     * optind should have the index of next parameter in argv. It should be
     * the name of the file to read. Therefore, should complain if file is not
     * given.
     */
    if (optind >= argc) {
        fprintf(stderr, "%1$s: no file given. '%1$s -h' for help.\n", argv[0]);
        exit(1);
    }

    char* file = argv[optind];

    /* Init emulator. */
    srand(time(NULL));
    init_machine(&mac);
    mac.keydown = &is_key_down;
    mac.speaker = &update_speaker;
    
    if (use_hexloader == 0) {
        if (load_rom(file, &mac)) {
            return 1;
        }
    } else {
        if (load_hex(file, &mac)) {
            return 1;
        }
    }

    /* Initialize SDL Context. */
    if (init_context()) {
        fprintf(stderr, "Error initializing SDL graphical context:\n");
        fprintf(stderr, "%s\n", SDL_GetError());
        return 1;
    }
    
    int last_ticks = SDL_GetTicks();
    int last_delta = 0, step_delta = 0, render_delta = 0;
    while (!is_close_requested()) {
        /* Update timers. */
        last_delta = SDL_GetTicks() - last_ticks;
        last_ticks = SDL_GetTicks();
        step_delta += last_delta;
        render_delta += last_delta;
        
        /* Opcode execution: estimated 1000 opcodes/second. */
        while (step_delta >= 1) {
            step_machine(&mac);
            step_delta--;
        }
        
        /* Update timed subsystems. */
        update_time(&mac, last_delta);
        
        /* Render frame every 1/60th of second. */
        while (render_delta >= (1000 / 60)) {
            render_display(&mac);
            render_delta -= (1000 / 60);
        }
    }

    /* Dispose SDL context. */
    destroy_context();

    return 0;
}
