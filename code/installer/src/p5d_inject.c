/* p5d_inject.c - build a patched decrypted SELF (zlib segments + digest update). */
#include <string.h>
#include <stdint.h>
#include <stdlib.h>
#include "p5d_core.h"
#include "zlib/zlib.h"
#include "sha256.h"

#pragma pack(push, 1)
typedef struct {
    uint32_t magic, version;
    uint16_t sdk_type, header_type;
    uint32_t metadata_offset;
    uint64_t header_len, elf_filesize, self_filesize, unknown;
    uint64_t self_offset, appinfo_offset, elf_offset, phdr_offset, shdr_offset;
    uint64_t section_info_offset, sceversion_offset;
    uint64_t controlinfo_offset, controlinfo_size, padding;
} SceHeader;

typedef struct {
    uint64_t offset, length, compression, encryption;
} SegInfo;
#pragma pack(pop)

typedef struct {
    uint32_t type, off, va, filesz;
} ElfPh;

#define MAX_PH 32

static int elf_phdrs(const unsigned char *elf, size_t len, ElfPh *out, int max)
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

int p5d_inject(const unsigned char *self, size_t selflen,
               const unsigned char *elf, size_t elflen,
               unsigned char **outp, size_t *outlenp)
{
    SceHeader sh;
    ElfPh ph[MAX_PH];
    SegInfo old_si[MAX_PH];
    size_t clen[MAX_PH];
    size_t new_off[MAX_PH];
    int order[MAX_PH];
    int n, rc = P5D_ERR_FORMAT;
    unsigned char digest[32];
    uint32_t e_phoff, e_phnum, e_phentsize;

    if (selflen < sizeof(SceHeader) || elflen < 52) return P5D_ERR_PARAM;
    if (outp) *outp = NULL;
    if (outlenp) *outlenp = 0;

    memcpy(&sh, self, sizeof(sh));
    if (sh.magic != 0x454353u) return P5D_ERR_FORMAT;
    if (sh.header_len < 0x80 || sh.header_len > selflen) return P5D_ERR_FORMAT;
    if (sh.section_info_offset + sizeof(SegInfo) * MAX_PH > selflen) return P5D_ERR_FORMAT;
    if (sh.controlinfo_offset + sh.controlinfo_size > selflen) return P5D_ERR_FORMAT;
    if (sh.phdr_offset > selflen) return P5D_ERR_FORMAT;

    n = elf_phdrs(elf, elflen, ph, MAX_PH);
    if (n < 0) return n;

    memcpy(&e_phoff, elf + 28, 4);
    memcpy(&e_phentsize, elf + 42, 2);
    memcpy(&e_phnum, elf + 44, 2);
    if ((int)e_phnum != n) return P5D_ERR_FORMAT;

    /* read old segment infos */
    for (int i = 0; i < n; i++)
        memcpy(&old_si[i], self + sh.section_info_offset + (size_t)i * sizeof(SegInfo), sizeof(SegInfo));

    /* pass 1: compress sizes only (free each temp buffer immediately) */
    for (int i = 0; i < n; i++) {
        size_t raw = ph[i].filesz;
        uLongf dstlen;
        unsigned char *tmp;
        if ((size_t)ph[i].off + raw > elflen) { rc = P5D_ERR_FORMAT; goto fail; }
        if (raw == 0) { clen[i] = 0; continue; }
        dstlen = compressBound((uLong)raw);
        tmp = (unsigned char *)malloc(dstlen ? dstlen : 1);
        if (!tmp) { rc = P5D_ERR_MEMORY; goto fail; }
        if (compress2(tmp, &dstlen, elf + ph[i].off, (uLong)raw, 9) != Z_OK) {
            free(tmp); rc = P5D_ERR_FORMAT; goto fail;
        }
        free(tmp);
        clen[i] = (size_t)dstlen;
    }

    /* layout: keep original order by old offset, shift when needed, 0x10 align */
    for (int i = 0; i < n; i++) order[i] = i;
    for (int a = 0; a < n; a++)
        for (int b = a + 1; b < n; b++)
            if (old_si[order[b]].offset < old_si[order[a]].offset) {
                int t = order[a]; order[a] = order[b]; order[b] = t;
            }
    {
        size_t cur = sh.header_len;
        for (int k = 0; k < n; k++) {
            int i = order[k];
            size_t off = (size_t)old_si[i].offset;
            if (off < cur) off = cur;
            off = (off + 0xFu) & ~(size_t)0xFu;
            new_off[i] = off;
            cur = off + clen[i];
        }
    }

    /* total size */
    {
        size_t total = sh.header_len + (size_t)e_phoff + (size_t)e_phnum * e_phentsize;
        for (int i = 0; i < n; i++) {
            size_t end = new_off[i] + clen[i];
            if (end > total) total = end;
        }
        if (total < sh.header_len) { rc = P5D_ERR_FORMAT; goto fail; }

        unsigned char *out = (unsigned char *)calloc(1, total);
        if (!out) { rc = P5D_ERR_MEMORY; goto fail; }

        /* header + updated segment table + sizes */
        memcpy(out, self, (size_t)sh.header_len);
        {
            uint64_t total64 = (uint64_t)total;
            memcpy(out + 32, &total64, 8);   /* self_filesize */
            memcpy(out + 24, &total64, 8);   /* elf_filesize (informational) */
        }
        for (int i = 0; i < n; i++) {
            SegInfo si;
            si.offset = new_off[i];
            si.length = clen[i];
            si.compression = old_si[i].compression;
            si.encryption = old_si[i].encryption;
            memcpy(out + sh.section_info_offset + (size_t)i * sizeof(SegInfo), &si, sizeof(si));
        }
        if ((size_t)sh.header_len + 52 > total) { free(out); rc = P5D_ERR_FORMAT; goto fail; }
        memcpy(out + sh.header_len, elf, 52);                       /* ELF header */
        {
            size_t blob = (size_t)e_phnum * e_phentsize;
            size_t dst = sh.header_len + e_phoff;
            if (dst + blob > total) { free(out); rc = P5D_ERR_FORMAT; goto fail; }
            if (sh.phdr_offset + blob > selflen) { free(out); rc = P5D_ERR_FORMAT; goto fail; }
            memcpy(out + dst, self + sh.phdr_offset, blob);        /* SELF program headers */
        }
        /* pass 2: compress directly into the output buffer */
        for (int i = 0; i < n; i++) {
            if (!clen[i]) continue;
            {
                uLongf dstlen = (uLongf)clen[i];
                if (compress2(out + new_off[i], &dstlen, elf + ph[i].off,
                              (uLong)ph[i].filesz, 9) != Z_OK || (size_t)dstlen != clen[i]) {
                    free(out); rc = P5D_ERR_FORMAT; goto fail;
                }
            }
        }

        /* ELF digest (type 4) + zero NPDRM (type 5) in control info */
        {
            SHA256_CTX ctx;
            sha256_init(&ctx);
            sha256_update(&ctx, (uint8_t *)elf, (uint32_t)elflen);
            sha256_final(&ctx, digest);
        }
        {
            size_t co = (size_t)sh.controlinfo_offset;
            size_t end = co + (size_t)sh.controlinfo_size;
            while (co + 16 <= end) {
                uint32_t ctype, csize;
                uint64_t next;
                memcpy(&ctype, out + co, 4);
                memcpy(&csize, out + co + 4, 4);
                memcpy(&next, out + co + 8, 8);
                if (ctype == 4 && co + 16 + 0x14 + 0x20 <= total)
                    memcpy(out + co + 16 + 0x14, digest, 32);
                else if (ctype == 5 && co + 16 + 0x80 <= total)
                    memset(out + co + 16, 0, 0x80);
                if (!next || csize == 0) break;
                co += csize;
            }
        }

        *outp = out;
        *outlenp = total;
        rc = P5D_OK;
    }

fail:
    return rc;
}
