/* p5d_patch.c - retarget the P5D CPK mount table (see release patcher source). */
#include <string.h>
#include <stdint.h>
#include "p5d_core.h"

static int read_phdrs(const unsigned char *elf, size_t len, P5dPhdr *out, int max)
{
    uint32_t phoff;
    uint16_t phentsize, phnum;
    if (len < 52 || memcmp(elf, "\x7f" "ELF", 4) != 0) return P5D_ERR_FORMAT;
    memcpy(&phoff, elf + 28, 4);
    memcpy(&phentsize, elf + 42, 2);
    memcpy(&phnum, elf + 44, 2);
    if (phentsize != 32 || phnum == 0 || phnum > max) return P5D_ERR_FORMAT;
    if ((size_t)phoff + (size_t)phentsize * phnum > len) return P5D_ERR_FORMAT;
    for (int i = 0; i < phnum; i++) {
        const unsigned char *p = elf + phoff + (size_t)i * phentsize;
        memcpy(&out[i].type, p + 0, 4);
        memcpy(&out[i].off, p + 4, 4);
        memcpy(&out[i].va, p + 8, 4);
        memcpy(&out[i].filesz, p + 16, 4);
    }
    return phnum;
}

static long va2off(const P5dPhdr *segs, int n, uint32_t va)
{
    for (int i = 0; i < n; i++)
        if (segs[i].type == 1 && va >= segs[i].va && va < segs[i].va + segs[i].filesz)
            return (long)segs[i].off + (long)(va - segs[i].va);
    return -1;
}

int p5d_patch_elf(unsigned char *elf, size_t elflen)
{
    P5dPhdr segs[32];
    int n = read_phdrs(elf, elflen, segs, 32);
    if (n < 0) return n;

    /* 1) mount count 2 -> 3 */
    long o = va2off(segs, n, P5D_CMP_VA);
    if (o < 0 || (size_t)o + 2 > elflen) return P5D_ERR_NOTFOUND;
    if (elf[o] != 0x02 || elf[o + 1] != 0x2c) return P5D_ERR_FORMAT;
    elf[o] = 0x03;

    /* 2) table in the zero gap of the data segment */
    long to = va2off(segs, n, P5D_TABLE_VA);
    if (to < 0 || (size_t)to + P5D_TABLE_SIZE > elflen) return P5D_ERR_NOTFOUND;
    for (size_t i = 0; i < P5D_TABLE_SIZE; i++)
        if (elf[to + i] != 0) return P5D_ERR_FORMAT;
    memset(elf + to, 0, P5D_TABLE_SIZE);
    {
        static const char *names[3] = { "mod.cpk", "vita.cpk", "data.cpk" };
        for (int i = 0; i < 3; i++) {
            size_t e = (size_t)to + (size_t)i * P5D_TABLE_STRIDE;
            int32_t minus1 = -1;
            memcpy(elf + e, &minus1, 4);
            memcpy(elf + e + 4, names[i], strlen(names[i]) + 1);
        }
    }

    /* 3) retarget the relocation entry of the MOVW/MOVT table load */
    for (int i = 0; i < n; i++) {
        if (segs[i].type != 0x60000000u) continue;
        if ((size_t)segs[i].off + segs[i].filesz > elflen) continue;
        for (size_t k = 0; k + 12 <= segs[i].filesz; k += 12) {
            size_t e = (size_t)segs[i].off + k;
            uint32_t target, instr;
            memcpy(&target, elf + e, 4);
            memcpy(&instr, elf + e + 4, 4);
            if (instr == P5D_RELOC_INSTR_OFF) {
                if (target != P5D_RELOC_OLD_TARGET) return P5D_ERR_FORMAT;
                uint32_t nt = P5D_TABLE_VA - P5D_DATA_VA;
                memcpy(elf + e, &nt, 4);
                return P5D_OK;
            }
        }
    }
    return P5D_ERR_NOTFOUND;
}
