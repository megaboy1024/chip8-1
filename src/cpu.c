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
#include <stdlib.h>
#include <string.h>

#define OPCODE_NNN(opcode) (opcode & 0xFFF)
#define OPCODE_KK(opcode) (opcode & 0xFF)
#define OPCODE_N(opcode) (opcode & 0xF)
#define OPCODE_X(opcode) ((opcode >> 8) & 0xF)
#define OPCODE_Y(opcode) ((opcode >> 4) & 0xF)
#define OPCODE_P(opcode) (opcode >> 12)

/**
 * These are the bitmaps for the sprites that represent numbers.
 * This array should be memcopied to memory address 0x050. LD F, Vx
 * instruction sets I register to the memory address of a provided
 * number.
 */
static char hexcodes[] = {
    0xF0, 0x90, 0x90, 0x90, 0xF0, // 0
    0x20, 0x60, 0x20, 0x20, 0x70, // 1
    0xF0, 0x10, 0xF0, 0x80, 0xF0, // 2
    0xF0, 0x10, 0xF0, 0x10, 0xF0, // 3
    0x90, 0x90, 0xF0, 0x10, 0x10, // 4
    0xF0, 0x80, 0xF0, 0x10, 0xF0, // 5
    0xF0, 0x80, 0xF0, 0x90, 0xF0, // 6
    0xF0, 0x10, 0x20, 0x40, 0x40, // 7
    0xF0, 0x90, 0xF0, 0x90, 0xF0, // 8
    0xF0, 0x90, 0xF0, 0x10, 0xF0, // 9
    0xF0, 0x90, 0xF0, 0x90, 0x90, // A
    0xE0, 0x90, 0xE0, 0x90, 0xE0, // B
    0xF0, 0x80, 0x80, 0x80, 0xF0, // C
    0xE0, 0x90, 0x90, 0x90, 0xE0, // D
    0xF0, 0x80, 0xF0, 0x80, 0xF0, // E
    0xF0, 0x80, 0xF0, 0x80, 0x80  // F
};

static int global_delta;

typedef void (*opcode_table_t) (struct machine_t* cpu, word opcode);

static void
nibble_0(struct machine_t* cpu, word opcode)
{
    if (opcode == 0x00e0) {
        /* 00E0: CLS - Clear the screen. */
        memset(cpu->screen, 0, 2048);
    } else if (opcode == 0x00ee) {
        /* 00EE: RET - Return from subroutine. */
        if (cpu->sp > 0)
        cpu->pc = cpu->stack[(int) --cpu->sp];
        /* TODO: Should throw an error on stack underflow. */
    }
}

static void
nibble_1(struct machine_t* cpu, word opcode)
{
    /* 1NNN: JMP - Jump to address location NNN. */
    cpu->pc = OPCODE_NNN(opcode);
}

static void
nibble_2(struct machine_t* cpu, word opcode)
{
    /* 2NNN: CALL - Call subroutine starting at address NNN. */
    if (cpu->sp < 16) {
        cpu->stack[(int) cpu->sp++] = cpu->pc;
        cpu->pc = OPCODE_NNN(opcode);
    }
    /* TODO: Should throw an error on stack overflow. */
}

static void
nibble_3(struct machine_t* cpu, word opcode)
{
    /* 3XKK: SE: Skip next instruction if V[X] = KK. */
    if (cpu->v[OPCODE_X(opcode)] == OPCODE_KK(opcode))
        cpu->pc = (cpu->pc + 2) & 0xfff;
}

static void
nibble_4(struct machine_t* cpu, word opcode)
{
    /* 4XKK: SNE - Skip next instruction if V[X] != KK. */
    if (cpu->v[OPCODE_X(opcode)] != OPCODE_KK(opcode))
        cpu->pc = (cpu->pc + 2) & 0xfff;
}

static void
nibble_5(struct machine_t* cpu, word opcode)
{
    /* 5XY0: SE - Skip next instruction if V[X] == V[Y]. */
    if (cpu->v[OPCODE_X(opcode)] == cpu->v[OPCODE_Y(opcode)])
        cpu->pc = (cpu->pc + 2) & 0xfff;
}

static void
nibble_6(struct machine_t* cpu, word opcode)
{
    /* 6XKK: LD - Set V[X] = KK. */
    cpu->v[OPCODE_X(opcode)] = OPCODE_KK(opcode);
}

static void
nibble_7(struct machine_t* cpu, word opcode)
{
    /* 7XKK: ADD - Add KK to V[X]. */
    cpu->v[OPCODE_X(opcode)] += OPCODE_KK(opcode);
}

static void
nibble_8(struct machine_t* cpu, word opcode)
{
    /* All these opcodes work with X and most of them with Y, worth it. */
    byte x = OPCODE_X(opcode), y = OPCODE_Y(opcode);
    switch (OPCODE_N(opcode))
    {
    case 0:
        /* 8XY0: LD - Set V[X] = V[Y]. */
        cpu->v[x] = cpu->v[y];
        break;
    case 1:
        /* 8XY1: OR - Set V[X] |= V[Y]. */
        cpu->v[x] |= cpu->v[y];
        break;
    case 2:
        /* 8XY2: AND - Set V[X] &= V[Y]. */
        cpu->v[x] &= cpu->v[y];
        break;
    case 3:
        /* 8XY3: XOR - Set V[X] ^= V[Y]. */
        cpu->v[x] ^= cpu->v[y];
        break;
    case 4:
        /* 8XY4: ADD - Set V[X] += V[Y], V[15] is carry flag. */
        cpu->v[0xf] = cpu->v[x] > ((cpu->v[x] + cpu->v[y]) & 0xFF);
        cpu->v[x] += cpu->v[y];
        break;
    case 5:
        /* 8XY5: SUB - Set V[X] -= V[Y], V[15] is borrow flag. */
        cpu->v[0xf] = (cpu->v[x] > cpu->v[y]);
        cpu->v[x] -= cpu->v[y];
        break;
    case 6:
        /* 8X06: SHR - Shifts right V[X], LSB bit goes to V[15]. */
        cpu->v[0xf] = (cpu->v[x] & 1);
        cpu->v[x] >>= 1;
        break;
    case 7:
        /* 8XY7: SUBN X, Y - Set V[X] = V[Y] - V[X], V[16] is borrow. */
        cpu->v[0xF] = (cpu->v[y] > cpu->v[x]);
        cpu->v[x] = cpu->v[y] - cpu->v[x];
        break;
    case 0xE:
        /* 8X0E: SHL - Shifts left V[X], MSB bit goes to V[15]. */
        cpu->v[0xF] = ((cpu->v[x] & 0x80) != 0);
        cpu->v[x] <<= 1;
        break;
    }
}

static void
nibble_9(struct machine_t* cpu, word opcode)
{
    /* 9XY0: SNE - Skip next instruction if V[X] != V[Y]. */
    if (cpu->v[OPCODE_X(opcode)] != cpu->v[OPCODE_Y(opcode)])
        cpu->pc = (cpu->pc + 2) & 0xFFF;
}

static void
nibble_A(struct machine_t* cpu, word opcode)
{
    /* ANNN: LD - Set I to NNN. */
    cpu->i = OPCODE_NNN(opcode);
}

static void
nibble_B(struct machine_t* cpu, word opcode)
{
    /* BNNN: JP - Jump to memory address (V[0] + NNN). */
    cpu->pc = (cpu->v[0] + OPCODE_NNN(opcode)) & 0xFFF;
}

static void
nibble_C(struct machine_t* cpu, word opcode)
{
    /* CXKK: RND - Put a random value, bitmasked against KK in V[X]. */
    cpu->v[OPCODE_X(opcode)] = rand() & OPCODE_KK(opcode);
}

static void
nibble_D(struct machine_t* cpu, word opcode)
{
    /* DXYN: DRW - Draw a sprite on the screen at location V[X], V[Y]. */
    byte x = OPCODE_X(opcode), y = OPCODE_Y(opcode);
    cpu->v[15] = 0;
    for (int j = 0; j < OPCODE_N(opcode); j++) {
        byte sprite = cpu->mem[cpu->i + j];
        for (int i = 0; i < 8; i++) {
            int px = (cpu->v[x] + i) & 63;
            int py = (cpu->v[y] + j) & 31;
            int pos = 64 * py + px;
            int pixel = (sprite & (1 << (7-i))) != 0;
            cpu->v[15] |= (cpu->screen[pos] & pixel);
            cpu->screen[pos] ^= pixel;
        }
    }
}

static void
nibble_E(struct machine_t* cpu, word opcode)
{
    char key = cpu->v[OPCODE_X(opcode)];
    if (OPCODE_KK(opcode) == 0x9E) {
        /* EX9E: SKP - Skip next instruction if key V[X] is down. */
        if (cpu->keydown && cpu->keydown(key & 0xF))
            cpu->pc = (cpu->pc + 2) & 0xFFF;
    } else if (OPCODE_KK(opcode) == 0xA1) {
        /* EXA1: SKNP - Skip next instruction if key V[X] is not down. */
        if (cpu->keydown && !cpu->keydown(key & 0xF))
            cpu->pc = (cpu->pc + 2) & 0xFFF;
    }
}

static void
nibble_F(struct machine_t* cpu, word opcode)
{
    switch (OPCODE_KK(opcode)) {
    case 0x07:
        /* FX07: LD - Set V[X] to DT. */
        cpu->v[OPCODE_X(opcode)] = cpu->dt;
        break;
    case 0x0A:
        /* FX0A: LD - Wait for a keypress, then store the key in V[X]. */
        cpu->wait_key = OPCODE_X(opcode);
        break;
    case 0x15:
        /* FX15: LD - Set DT to V[X]. */
        cpu->dt = cpu->v[OPCODE_X(opcode)];
        break;
    case 0x18:
        /* FX18: LD - Set ST to V[X]. */
        cpu->st = cpu->v[OPCODE_X(opcode)];
        break;
    case 0x1E:
        /* FX1E: ADD - Add V[X] to I. */
        cpu->i += cpu->v[OPCODE_X(opcode)];
        break;
    case 0x29:
        /* FX29: LD - Set I to the address location for the sprite. */
        cpu->i = 0x50 + (cpu->v[OPCODE_X(opcode)] & 0xF) * 5;
        break;
    case 0x33:
        /* FX33: Represent V[X] as BCD in I, I+1, I+2. */
        cpu->mem[cpu->i + 2] = cpu->v[OPCODE_X(opcode)] % 10;
        cpu->mem[cpu->i + 1] = (cpu->v[OPCODE_X(opcode)] / 10) % 10;
        cpu->mem[cpu->i] = cpu->v[OPCODE_X(opcode)] / 100;
        break;
    case 0x55:
        /* FX55: LD - Save registers V[0] to V[x] starting at I. */
        for (int reg = 0; reg <= OPCODE_X(opcode); reg++) {
            cpu->mem[cpu->i + reg] = cpu->v[reg];
        }
        break;
    case 0x65:
        /* FX65: LD - Load registers V[0] to V[x] from I. */
        for (int reg = 0; reg <= OPCODE_X(opcode); reg++) {
            cpu->v[reg] = cpu->mem[cpu->i + reg];
        }
        break;
    }
}

/**
 * This is the handler table. There are 16 handlers in the following array,
 * each one covering a subset of the opcodes for the CHIP-8. During opcode
 * fetching, the most significant nibble (most significant hex char) is taken
 * out as a value in range [0, 15]. The handler from this array whose index
 * matches the value of that nibble is executed. The handler should execute
 * opcodes starting by that value.
 */
static opcode_table_t nibbles[16] = {
    &nibble_0, &nibble_1, &nibble_2, &nibble_3,
    &nibble_4, &nibble_5, &nibble_6, &nibble_7,
    &nibble_8, &nibble_9, &nibble_A, &nibble_B,
    &nibble_C, &nibble_D, &nibble_E, &nibble_F
};

void
init_machine(struct machine_t* machine)
{
    memset(machine, 0x00, sizeof(struct machine_t));
    memcpy(machine->mem + 0x50, hexcodes, 80);
    machine->pc = 0x200;
    machine->wait_key = -1;
    global_delta = 0;
}

#include <stdio.h>

void
step_machine(struct machine_t* cpu)
{
    /* Are we waiting for a key press? */
    if (cpu->wait_key != -1 && cpu->keydown) {
        for (int i = 0; i < 16; i++) {
            int status = cpu->keydown(i);
            if (status) {
                /* Key was down. Restore system. */
                cpu->v[(int) cpu->wait_key] = i;
                cpu->wait_key = -1;
                break;
            }
        }
        /* Test again. If we are still waiting for a key, don't fetch. */
        if (cpu->wait_key != -1) {
            return;
        }
    }
    
    /* Fetch next opcode. */
    word opcode = (cpu->mem[cpu->pc] << 8) | cpu->mem[cpu->pc + 1];
    cpu->pc = (cpu->pc + 2) & 0xFFF;

    /* Execute the corresponding handler from the nibble table. */
    nibbles[OPCODE_P(opcode)](cpu, opcode);
}

void
update_time(struct machine_t* cpu, int delta)
{
    global_delta += delta;
    while (global_delta > (1000 / 60)) {
        global_delta -= (1000 / 60);
        if (cpu->dt > 0) {
            cpu->dt--;
        }
        if (cpu->st > 0) {
            if (--cpu->st == 0 && cpu->speaker) {
                /* Disable speaker buzz. */
                cpu->speaker(0);
            } else if (cpu->speaker) {
                /* Enable speaker buzz. */
                cpu->speaker(1);
            }
        }
    }
}