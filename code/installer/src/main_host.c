/* host test: p5d_installer_host <fagdec_eboot.bin> <fagdec_eboot.bin.elf> <out_eboot.bin> */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "p5d_core.h"

static unsigned char *readf(const char *p, size_t *len)
{
    FILE *f = fopen(p, "rb");
    unsigned char *b;
    long n;
    if (!f) { printf("cannot open %s\n", p); return NULL; }
    fseek(f, 0, SEEK_END); n = ftell(f); fseek(f, 0, SEEK_SET);
    b = (unsigned char *)malloc(n ? (size_t)n : 1);
    if (!b || fread(b, 1, (size_t)n, f) != (size_t)n) { fclose(f); free(b); return NULL; }
    fclose(f); *len = (size_t)n; return b;
}

int main(int argc, char **argv)
{
    size_t selflen, elflen, outlen = 0;
    unsigned char *self, *elf, *out = NULL;
    int r;
    if (argc != 4) { printf("usage: %s <self> <elf> <out>\n", argv[0]); return 2; }
    self = readf(argv[1], &selflen);
    elf = readf(argv[2], &elflen);
    if (!self || !elf) return 2;
    r = p5d_patch_elf(elf, elflen);
    printf("patch: %d\n", r);
    if (r != P5D_OK) return 1;
    r = p5d_inject(self, selflen, elf, elflen, &out, &outlen);
    printf("inject: %d outlen=%zu\n", r, outlen);
    if (r != P5D_OK) return 1;
    {
        FILE *f = fopen(argv[3], "wb");
        if (!f || fwrite(out, 1, outlen, f) != outlen) { printf("write fail\n"); return 1; }
        fclose(f);
    }
    free(out); free(self); free(elf);
    printf("OK\n");
    return 0;
}
