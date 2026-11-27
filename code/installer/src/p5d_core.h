/* p5d_core.h - shared core for the P5D eboot patcher/injector (PC + PSVita). */
#pragma once
#include <stdint.h>
#include <stddef.h>

#define P5D_CMP_VA           0x811821B2u
#define P5D_RELOC_INSTR_OFF  0x1820D6u
#define P5D_RELOC_OLD_TARGET 0x8AF6Cu
#define P5D_TABLE_VA         0x814D0CD0u
#define P5D_DATA_VA          0x81322000u
#define P5D_TABLE_STRIDE     0x104u
#define P5D_TABLE_SIZE       (3u * P5D_TABLE_STRIDE)

#define P5D_OK   0
#define P5D_ERR_PARAM   -1
#define P5D_ERR_FORMAT  -2
#define P5D_ERR_MEMORY  -3
#define P5D_ERR_NOTFOUND -4
#define P5D_ERR_IO      -5

typedef struct {
    uint32_t type;
    uint32_t off;
    uint32_t va;
    uint32_t filesz;
} P5dPhdr;

/* in-place patch of a FAGDec-produced decrypted ELF buffer */
int p5d_patch_elf(unsigned char *elf, size_t elflen);

/* build a patched decrypted SELF from (self, patched elf); caller frees *out */
int p5d_inject(const unsigned char *self, size_t selflen,
               const unsigned char *elf, size_t elflen,
               unsigned char **out, size_t *outlen);
