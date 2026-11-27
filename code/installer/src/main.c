/*
 * P5D Mod Installer (PSVita) - single-VPK version.
 *
 * Everything on console:
 *   1) copies bundled assets (kentente.skprx / userAllied.suprx / mod.cpk)
 *   2) loads FAGDec's decryption modules (GPL, from PSVita-RE-tools)
 *   3) mounts the game, finds the license, decrypts app0:/eboot.bin into
 *      ux0:data/P5DMod/raw (segment files)
 *   4) rebuilds a decrypted ELF, patches it (3-CPK mount patch)
 *   5) injects the patched ELF into a synthetic decrypted SELF header and
 *      writes ux0:rePatch/PCSH10109/eboot.bin
 *   6) copies mod.cpk into ux0:rePatch/PCSH10109/data/
 *
 * Log: ux0:data/P5DMod/p5d_installer_log.txt
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdarg.h>

#include <psp2/io/fcntl.h>
#include <psp2/io/stat.h>
#include <psp2/io/dirent.h>
#include <psp2/ctrl.h>
#include <psp2/kernel/threadmgr.h>
#include <psp2/kernel/modulemgr.h>
#include <psp2/appmgr.h>
#include <psp2/npdrm.h>

#include <taihen.h>

#include "debugScreen.h"
#include "zh_font.h"
#include "p5d_core.h"
#include "kEntente.h"
#include "userAllied.h"
#include "zlib/zlib.h"

#define printf(...) ui_printf(__VA_ARGS__)

unsigned int sceLibcHeapSize = 32 * 1024 * 1024;

#define BASE          "ux0:data/P5DMod"
#define LOG_PATH      BASE "/p5d_installer_log.txt"
#define KP_PATH       BASE "/kentente.skprx"
#define UA_PATH       BASE "/userAllied.suprx"
#define RAW_DIR       BASE "/raw"
#define RAW_SELF      BASE "/raw/eboot.bin"
#define ASSET_KP      "app0:/assets/kentente.skprx"
#define ASSET_UA      "app0:/assets/userAllied.suprx"
#define ASSET_CPK     "app0:/assets/mod.cpk"
#define OUT_DIR       "ux0:rePatch/PCSH10109"
#define OUT_SELF      "ux0:rePatch/PCSH10109/eboot.bin"
#define OUT_CPK       "ux0:rePatch/PCSH10109/data/mod.cpk"

#define GAME_TITLE    "PCSH10109"
#define GAME_SELF     "ux0:/app/" GAME_TITLE "/eboot.bin"

#define HEADER_LEN    0x1000u

#pragma pack(push, 1)
typedef struct {
    uint32_t magic, version;
    uint16_t sdk_type, header_type;
    uint32_t metadata_offset;
    uint64_t header_len, elf_filesize, self_filesize, unknown;
    uint64_t self_offset, appinfo_offset, elf_offset, phdr_offset, shdr_offset;
    uint64_t section_info_offset, sceversion_offset;
    uint64_t controlinfo_offset, controlinfo_size, padding;
} SceHdr;

typedef struct {
    uint64_t offset, length, compression, encryption;
} SegInfo;

typedef struct {
    uint32_t p_type, p_offset, p_vaddr, p_paddr, p_filesz, p_memsz, p_flags, p_align;
} Phdr32;

typedef struct {
    unsigned char e_ident[16];
    uint16_t e_type, e_machine;
    uint32_t e_version, e_entry, e_phoff, e_shoff, e_flags;
    uint16_t e_ehsize, e_phentsize, e_phnum, e_shentsize, e_shnum, e_shstrndx;
} Ehdr32;

typedef struct {
    uint32_t type, size;
    uint64_t next;
} CtrlInfo;
#pragma pack(pop)

static SceUID g_log = -1;

static void log_open(void)
{
    g_log = sceIoOpen(LOG_PATH, SCE_O_WRONLY | SCE_O_CREAT | SCE_O_TRUNC, 0666);
}
static void log_msg(const char *fmt, ...)
{
    char buf[512];
    int n;
    va_list ap;
    va_start(ap, fmt);
    n = vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    if (n < 0) return;
    if (n > (int)sizeof(buf) - 2) n = (int)sizeof(buf) - 2;
    buf[n++] = '\n';
    if (g_log >= 0) sceIoWrite(g_log, buf, (unsigned int)n);
}


/* ---------- simple UI: Chinese bitmap lines + ASCII text ---------- */
#define UI_X        48
#define UI_TOP      40
static int g_ui_y;

static void ui_clear(void)
{
    psvDebugScreenClear(COLOR_BLACK);
    g_ui_y = UI_TOP;
}

static void ui_zh(int idx)
{
    const ZhLine *l = zh_lines[idx];
    int stride = (l->w + 31) / 32;
    uint32_t *vram = (uint32_t *)psvDebugScreenFrameBuf.base;
    for (int y = 0; y < l->h; y++) {
        int py = g_ui_y + y;
        if (py < 0 || py >= SCREEN_HEIGHT) continue;
        for (int x = 0; x < l->w; x++) {
            int px = UI_X + x;
            if (px < 0 || px >= SCREEN_WIDTH) continue;
            if (l->bits[y * stride + x / 32] & (1u << (31 - (x % 32))))
                vram[px + py * SCREEN_FB_WIDTH] = COLOR_WHITE;
        }
    }
    g_ui_y += l->h + 14;
}

static void ui_ascii(const char *s, int x, int y, uint32_t color)
{
    uint32_t *vram = (uint32_t *)psvDebugScreenFrameBuf.base;
    for (int c = 0; s[c]; c++) {
        const unsigned char *font = &psvDebugScreenFont[(unsigned char)s[c] * 8];
        for (int row = 0; row < 8; row++, font++) {
            for (int col = 0; col < 8; col++) {
                if (*font & (128 >> col)) {
                    int px = x + c * 16 + col * 2;
                    int py = y + row * 2;
                    for (int dy = 0; dy < 2; dy++)
                        for (int dx = 0; dx < 2; dx++) {
                            int qx = px + dx, qy = py + dy;
                            if (qx >= 0 && qx < SCREEN_WIDTH && qy >= 0 && qy < SCREEN_HEIGHT)
                                vram[qx + qy * SCREEN_FB_WIDTH] = color;
                        }
                }
            }
        }
    }
}

static void ui_printf(const char *fmt, ...)
{
    char buf[256];
    va_list ap;
    int n;
    va_start(ap, fmt);
    n = vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    if (n <= 0) return;
    if (g_ui_y + 18 > SCREEN_HEIGHT) return;
    ui_ascii(buf, UI_X, g_ui_y, COLOR_GREY);
    g_ui_y += 20;
}

static int file_exists(const char *path)
{
    SceIoStat st;
    return sceIoGetstat(path, &st) >= 0;
}

static unsigned char *read_whole_file(const char *path, size_t *out_len)
{
    SceUID fd = sceIoOpen(path, SCE_O_RDONLY, 0);
    unsigned char *buf;
    SceIoStat st;
    size_t size, done = 0;
    if (fd < 0) return NULL;
    if (sceIoGetstat(path, &st) < 0) { sceIoClose(fd); return NULL; }
    size = (size_t)st.st_size;
    buf = (unsigned char *)malloc(size ? size : 1);
    if (!buf) { sceIoClose(fd); return NULL; }
    while (done < size) {
        int r = sceIoRead(fd, buf + done, (unsigned int)(size - done));
        if (r <= 0) break;
        done += (size_t)r;
    }
    sceIoClose(fd);
    if (done != size) { free(buf); return NULL; }
    *out_len = size;
    return buf;
}

static int read_file_seek(const char *path, unsigned int off, void *buf, unsigned int len)
{
    SceUID fd = sceIoOpen(path, SCE_O_RDONLY, 0);
    int r;
    if (fd < 0) return -1;
    if (off && sceIoLseek(fd, off, SCE_SEEK_SET) < 0) { sceIoClose(fd); return -1; }
    r = sceIoRead(fd, buf, len);
    sceIoClose(fd);
    return r == (int)len ? 0 : -1;
}

static int write_whole_file(const char *path, const unsigned char *data, size_t len)
{
    SceUID fd = sceIoOpen(path, SCE_O_WRONLY | SCE_O_CREAT | SCE_O_TRUNC, 0666);
    size_t done = 0;
    if (fd < 0) return -1;
    while (done < len) {
        int r = sceIoWrite(fd, data + done, (unsigned int)(len - done));
        if (r <= 0) { sceIoClose(fd); return -1; }
        done += (size_t)r;
    }
    sceIoClose(fd);
    return 0;
}

static int copy_file(const char *src, const char *dst)
{
    unsigned char *buf = (unsigned char *)malloc(0x10000);
    SceUID in, out;
    int ret = 0;
    if (!buf) return -1;
    in = sceIoOpen(src, SCE_O_RDONLY, 0);
    if (in < 0) { free(buf); return -1; }
    out = sceIoOpen(dst, SCE_O_WRONLY | SCE_O_CREAT | SCE_O_TRUNC, 0666);
    if (out < 0) { sceIoClose(in); free(buf); return -1; }
    for (;;) {
        int r = sceIoRead(in, buf, 0x10000);
        if (r < 0) { ret = -1; break; }
        if (r == 0) break;
        if (sceIoWrite(out, buf, (unsigned int)r) != r) { ret = -1; break; }
    }
    sceIoClose(out);
    sceIoClose(in);
    free(buf);
    return ret;
}

static void wait_key_press(void)
{
    SceCtrlData pad;
    memset(&pad, 0, sizeof(pad));
    ui_zh(10);
    for (;;) {
        sceCtrlPeekBufferPositive(0, &pad, 1);
        if (pad.buttons & SCE_CTRL_CROSS) break;
        sceKernelDelayThread(100 * 1000);
    }
}

/* ---------- rif discovery (ported from FAGDec app) ---------- */
static int find_rif(char *out_path, size_t out_sz)
{
    static const char *devices[3] = { "ux0:", "gro0:", "grw0:" };
    char rif_name[0x30];
    char path[512];
    int res;
    memset(rif_name, 0, sizeof(rif_name));
    res = _sceNpDrmGetFixedRifName(rif_name, 0);
    log_msg("fixed rif name res=0x%08X name='%s'", res, (res >= 0) ? rif_name : "");
    if (res >= 0 && rif_name[0]) {
        for (int i = 0; i < 3; i++) {
            snprintf(path, sizeof(path), "%slicense/app/%s/%s", devices[i], GAME_TITLE, rif_name);
            if (file_exists(path)) {
                snprintf(out_path, out_sz, "%s", path);
                log_msg("rif found: %s", out_path);
                return 0;
            }
        }
    }
    /* fallback: first file in the license directory */
    for (int i = 0; i < 3; i++) {
        SceUID dfd;
        SceIoDirent dir;
        snprintf(path, sizeof(path), "%slicense/app/%s", devices[i], GAME_TITLE);
        dfd = sceIoDopen(path);
        if (dfd < 0) continue;
        memset(&dir, 0, sizeof(dir));
        if (sceIoDread(dfd, &dir) > 0 && dir.d_name[0] && dir.d_name[0] != '.') {
            snprintf(out_path, out_sz, "%s/%s", path, dir.d_name);
            sceIoDclose(dfd);
            log_msg("rif fallback: %s", out_path);
            return 0;
        }
        sceIoDclose(dfd);
    }
    log_msg("no rif found");
    return -1;
}


/* ---------- direct decrypted view of the game's eboot (appmgr mount) ---------- */
static unsigned char *g_full_self = NULL;
static size_t g_full_self_len = 0;
static unsigned char *g_self_hdr;
static unsigned char *g_elf;
static size_t g_elf_len;

static void apply_fagdec_header_edits(unsigned char *hdr)
{
    SceHdr *sh = (SceHdr *)hdr;
    CtrlInfo *ci;
    sh->sdk_type = 0xc0;
    /* appinfo.version = 0 (FAGDec does this for the fake SELF) */
    if (sh->appinfo_offset + 0x20 <= HEADER_LEN) {
        uint64_t *ver = (uint64_t *)(hdr + sh->appinfo_offset + 16);
        *ver = 0;
    }
    /* control info: strip NPDrm + min_required_fw = 0 */
    if (sh->controlinfo_offset + 0x20 <= HEADER_LEN) {
        size_t co = sh->controlinfo_offset;
        size_t end = co + (size_t)sh->controlinfo_size;
        while (co + 16 <= end && co + 16 <= HEADER_LEN) {
            uint32_t ctype, csize;
            uint64_t next;
            memcpy(&ctype, hdr + co, 4);
            memcpy(&csize, hdr + co + 4, 4);
            memcpy(&next, hdr + co + 8, 8);
            if (ctype == 4 && co + 16 + 0x40 <= HEADER_LEN) {
                uint32_t zero = 0;
                memcpy(hdr + co + 16 + 0x3C, &zero, 4); /* min_required_fw */
            } else if (ctype == 5 && co + 16 + 0x80 <= HEADER_LEN) {
                memset(hdr + co + 16, 0, 0x80);
            }
            if (!next || csize == 0) break;
            co += csize;
        }
    }
}

/* Try to build the patched-ready ELF directly from an already-decrypted
 * (appmgr gp: mounted) SELF. Returns 0 on success. */
static int use_mounted_self(const char *path)
{
    unsigned char *buf;
    size_t len = 0, elf_len = 0;
    SceHdr *sh;
    Ehdr32 *eh;
    Phdr32 *ph;
    SegInfo *si;
    int ok = 1;

    buf = read_whole_file(path, &len);
    if (!buf) { log_msg("mounted self: read failed"); return -1; }
    if (len < HEADER_LEN || memcmp(buf, "SCE\x00", 4) != 0) {
        log_msg("mounted self: bad magic/len (%u)", (unsigned)len);
        free(buf);
        return -1;
    }
    sh = (SceHdr *)buf;
    log_msg("mounted self len=%u hl=0x%x elf_off=0x%x phdr=0x%x secinfo=0x%x",
            (unsigned)len, (unsigned)sh->header_len, (unsigned)sh->elf_offset,
            (unsigned)sh->phdr_offset, (unsigned)sh->section_info_offset);
    if (sh->header_len != HEADER_LEN) { free(buf); return -1; }
    eh = (Ehdr32 *)(buf + sh->elf_offset);
    ph = (Phdr32 *)(buf + sh->phdr_offset);
    si = (SegInfo *)(buf + sh->section_info_offset);
    if (eh->e_phnum == 0 || eh->e_phnum > 16) { free(buf); return -1; }
    for (int i = 0; i < eh->e_phnum; i++) {
        size_t end = (size_t)ph[i].p_offset + ph[i].p_filesz;
        if (end > elf_len) elf_len = end;
        log_msg("seg%d: off=0x%x len=0x%x filesz=0x%x enc=%u comp=%u",
                i, (unsigned)si[i].offset, (unsigned)si[i].length,
                (unsigned)ph[i].p_filesz, (unsigned)si[i].encryption,
                (unsigned)si[i].compression);
    }
    if (elf_len < 0x1000) elf_len = 0x1000;
    g_elf = (unsigned char *)calloc(1, elf_len);
    if (!g_elf) { free(buf); return -1; }
    g_elf_len = elf_len;
    memcpy(g_elf, eh, sizeof(Ehdr32));
    ((Ehdr32 *)g_elf)->e_shoff = 0;
    ((Ehdr32 *)g_elf)->e_shnum = 0;
    ((Ehdr32 *)g_elf)->e_shentsize = 0;
    ((Ehdr32 *)g_elf)->e_shstrndx = 0;
    memcpy(g_elf + eh->e_phoff, ph, (size_t)eh->e_phnum * sizeof(Phdr32));
    for (int i = 0; i < eh->e_phnum && ok; i++) {
        size_t src_len = (size_t)si[i].length;
        unsigned long dlen = ph[i].p_filesz;
        int zres;
        if (si[i].offset + src_len > len) { ok = 0; break; }
        zres = uncompress(g_elf + ph[i].p_offset, &dlen, buf + si[i].offset, (unsigned long)src_len);
        if (zres != Z_OK || dlen != ph[i].p_filesz) {
            log_msg("seg%d decompress failed z=%d dlen=%u", i, zres, (unsigned)dlen);
            ok = 0;
        }
    }
    if (!ok) { free(g_elf); g_elf = NULL; free(buf); return -1; }
    apply_fagdec_header_edits(buf);
    g_full_self = buf;
    g_full_self_len = len;
    log_msg("mounted self: ELF built (%u bytes), no decryption needed", (unsigned)g_elf_len);
    return 0;
}

/* ---------- decrypt + rebuild ELF ---------- */
static int load_modules(void)
{
    SceUID kid, uaid;
    kid = taiLoadStartKernelModule(KP_PATH, 0, NULL, 0);
    log_msg("kentente load=0x%08X (negative=already loaded or denied)", (unsigned int)kid);
    /* module may already be resident from a previous run in this boot */
    uaid = sceKernelLoadStartModule(UA_PATH, 0, NULL, 0, NULL, NULL);
    log_msg("userAllied load=0x%08X", (unsigned int)uaid);
    if (uaid < 0) {
        log_msg("ERROR: userAllied load failed");
        return -1;
    }
    sceKernelDelayThread(200 * 1000);
    return 0;
}


static int entente_wait(char *final_msg, size_t msg_sz)
{
    char cur[160], last[160];
    int waited = 0;
    memset(cur, 0, sizeof(cur));
    memset(last, 0, sizeof(last));
    final_msg[0] = 0;
    while (waited < 120000) {
        int st = userAlliedStatus();
        userAlliedGetLogs(cur);
        if (strcmp(cur, last) != 0) {
            log_msg("entente: %s", cur);
            snprintf(last, sizeof(last), "%s", cur);
        }
        if (st == ENTENTE_DONE) {
            snprintf(final_msg, msg_sz, "%s", cur);
            return 0;
        }
        sceKernelDelayThread(30 * 1000);
        waited += 30;
    }
    snprintf(final_msg, msg_sz, "%s", cur);
    return -1;
}

static int seg_files_exist(void)
{
    char p[160];
    snprintf(p, sizeof(p), RAW_SELF ".seg0");
    return file_exists(p);
}

static int decrypt_try(const char *path, int self_type, int path_id, const char *rif)
{
    ententeParams param;
    char final_msg[160];
    char segp[160];
    int res;
    /* clean previous attempts */
    for (int i = 0; i < 16; i++) {
        snprintf(segp, sizeof(segp), RAW_SELF ".seg%d", i);
        sceIoRemove(segp);
    }
    memset(&param, 0, sizeof(param));
    param.path = (char *)path;
    param.outpath = (char *)RAW_SELF;
    param.rifpath = (char *)(rif ? rif : NULL);
    param.path_id = path_id;
    param.usecdram = 0;
    param.self_auth = 1;
    param.self_type = self_type;
    log_msg("try: path=%s self_type=%d path_id=%d rif=%s", path, self_type, path_id,
            rif ? rif : "(null)");
    res = userAlliedDecryptSelf(&param);
    log_msg("DecryptSelf res=0x%08X", res);
    if (res < 0) return -1;
    if (entente_wait(final_msg, sizeof(final_msg)) < 0) {
        log_msg("decrypt timeout");
        return -1;
    }
    log_msg("decrypt finish: %s", final_msg);
    if (seg_files_exist()) {
        log_msg("segment files present -> decryption OK");
        return 0;
    }
    log_msg("no segment files -> decryption failed");
    return -1;
}

static int decrypt_eboot(const char *mount_point)
{
    char rif_path[512];
    char alt_self[160];
    int have_rif;

    sceIoMkdir(BASE, 0777);
    sceIoMkdir(RAW_DIR, 0777);

    snprintf(alt_self, sizeof(alt_self), "%seboot.bin", mount_point);
    log_msg("game self: %s exists=%d | alt: %s exists=%d",
            GAME_SELF, file_exists(GAME_SELF), alt_self, file_exists(alt_self));
    if (!file_exists(GAME_SELF) && !file_exists(alt_self)) {
        log_msg("ERROR: game self not found");
        return -1;
    }

    have_rif = (find_rif(rif_path, sizeof(rif_path)) == 0);

    /* attempt main game path first */
    if (file_exists(GAME_SELF)) {
        if (decrypt_try(GAME_SELF, 1, -1, have_rif ? rif_path : NULL) == 0) return 0;
        if (decrypt_try(GAME_SELF, 2, -1, have_rif ? rif_path : NULL) == 0) return 0;
    }
    /* alt: mount point path */
    if (file_exists(alt_self)) {
        if (decrypt_try(alt_self, 1, -1, have_rif ? rif_path : NULL) == 0) return 0;
    }
    log_msg("ERROR: all decrypt attempts failed");
    return -1;
}


/* Build the decrypted ELF + synthetic SELF header from the decrypted
 * segment files produced by kentente (RAW_SELF.segN). */
static int build_from_segments(const char *self_path)
{
    SceHdr *sh;
    Ehdr32 *eh;
    Phdr32 *ph;
    SegInfo *si;
    size_t elf_len = 0;
    unsigned char *hdr = NULL;

    for (int i = 0; i < 16; i++) {
        char segp[160];
        snprintf(segp, sizeof(segp), RAW_SELF ".seg%d", i);
        if (i == 0) {
            if (!file_exists(segp)) {
                log_msg("build: no segment files");
                return -1;
            }
            hdr = (unsigned char *)malloc(HEADER_LEN);
            if (!hdr) return -1;
            if (read_file_seek(self_path, 0, hdr, HEADER_LEN) < 0) {
                log_msg("build: cannot read self header from %s", self_path);
                free(hdr);
                return -1;
            }
        }
        if (!file_exists(segp)) break;
    }
    if (!hdr) return -1;
    sh = (SceHdr *)hdr;
    if (sh->header_len != HEADER_LEN) { free(hdr); return -1; }
    eh = (Ehdr32 *)(hdr + sh->elf_offset);
    ph = (Phdr32 *)(hdr + sh->phdr_offset);
    si = (SegInfo *)(hdr + sh->section_info_offset);
    if (eh->e_phnum == 0 || eh->e_phnum > 16) { free(hdr); return -1; }
    for (int i = 0; i < eh->e_phnum; i++) {
        size_t end = (size_t)ph[i].p_offset + ph[i].p_filesz;
        if (end > elf_len) elf_len = end;
    }
    if (elf_len < 0x1000) elf_len = 0x1000;
    g_elf = (unsigned char *)calloc(1, elf_len);
    if (!g_elf) { free(hdr); return -1; }
    g_elf_len = elf_len;
    memcpy(g_elf, eh, sizeof(Ehdr32));
    ((Ehdr32 *)g_elf)->e_shoff = 0;
    ((Ehdr32 *)g_elf)->e_shnum = 0;
    ((Ehdr32 *)g_elf)->e_shentsize = 0;
    ((Ehdr32 *)g_elf)->e_shstrndx = 0;
    memcpy(g_elf + eh->e_phoff, ph, (size_t)eh->e_phnum * sizeof(Phdr32));

    for (int i = 0; i < eh->e_phnum; i++) {
        char segp[160];
        unsigned char *src;
        size_t src_len = 0;
        unsigned long dlen = ph[i].p_filesz;
        int zres;
        snprintf(segp, sizeof(segp), RAW_SELF ".seg%d", i);
        if (!file_exists(segp)) {
            log_msg("build: missing %s", segp);
            free(hdr);
            return -1;
        }
        src = read_whole_file(segp, &src_len);
        if (!src) { free(hdr); return -1; }
        zres = uncompress(g_elf + ph[i].p_offset, &dlen, src, (unsigned long)src_len);
        if (zres != Z_OK || dlen != ph[i].p_filesz) {
            log_msg("build: seg%d inflate failed z=%d dlen=%u (src %u)",
                    i, zres, (unsigned)dlen, (unsigned)src_len);
            free(src);
            free(hdr);
            return -1;
        }
        free(src);
        sceIoRemove(segp);
        log_msg("build: seg%d ok", i);
    }
    apply_fagdec_header_edits(hdr);
    g_self_hdr = hdr;
    log_msg("build: ELF ready (%u bytes)", (unsigned)g_elf_len);
    return 0;
}

int main(int argc, char *argv[])
{
    unsigned char *self_buf = NULL, *elf_buf = NULL, *out_buf = NULL;
    size_t self_len = 0, elf_len = 0, out_len = 0;
    int r;

    (void)argc; (void)argv;
    psvDebugScreenInit();
    ui_clear();
    ui_zh(0);
    ui_zh(1);
    sceIoMkdir(BASE, 0777);
    log_open();
    log_msg("P5D single-VPK installer start");


    /* assets -> ux0:data */
    if (!file_exists(KP_PATH)) {
        if (copy_file(ASSET_KP, KP_PATH) < 0) log_msg("ERROR copying kentente");
    }
    if (!file_exists(UA_PATH)) {
        if (copy_file(ASSET_UA, UA_PATH) < 0) log_msg("ERROR copying userAllied");
    }
    if (!file_exists(BASE "/mod.cpk")) {
        copy_file(ASSET_CPK, BASE "/mod.cpk");
    }
    log_msg("assets: kp=%d ua=%d cpk=%d", file_exists(KP_PATH), file_exists(UA_PATH),
            file_exists(BASE "/mod.cpk"));
    ui_zh(2);

    /* fast path: already-decrypted files present */
    if (file_exists(BASE "/eboot.bin") && file_exists(BASE "/eboot.bin.elf")) {
        log_msg("using pre-decrypted files from %s", BASE);
        self_buf = read_whole_file(BASE "/eboot.bin", &self_len);
        elf_buf = read_whole_file(BASE "/eboot.bin.elf", &elf_len);
        if (self_len > HEADER_LEN) self_len = HEADER_LEN;
    } else {
        char mount_point[32];
        char alt_self[160];
        int mres;
        memset(mount_point, 0, sizeof(mount_point));
        mres = sceAppMgrGameDataMount("ux0:/app/" GAME_TITLE, NULL, NULL, mount_point);
        log_msg("GameDataMount res=0x%08X mount='%s'", mres, mount_point);
        snprintf(alt_self, sizeof(alt_self), "%seboot.bin", mount_point);

        ui_zh(4);
        if (mres >= 0 && file_exists(alt_self) && use_mounted_self(alt_self) == 0) {
            printf("game eboot ready\n");
            self_buf = g_full_self;
            self_len = g_full_self_len;
            elf_buf = g_elf;
            elf_len = g_elf_len;
        } else {
            ui_zh(3);
            log_msg("mounted self path failed, fallback to kentente");
            if (load_modules() < 0) {
                log_msg("ERROR: module load failed");
                printf("ERROR: cannot load decryption modules\n");
                goto fail;
            }
            if (decrypt_eboot(mount_point) < 0) {
                printf("ERROR: decryption failed\n");
                goto fail;
            }
            if (build_from_segments(GAME_SELF) < 0 &&
                build_from_segments(alt_self) < 0) {
                printf("ERROR: rebuild ELF failed\n");
                goto fail;
            }
            self_buf = g_self_hdr;
            self_len = HEADER_LEN;
            elf_buf = g_elf;
            elf_len = g_elf_len;
        }
    }
    if (!self_buf || !elf_buf) {
        log_msg("ERROR: missing input data");
        goto fail;
    }
    log_msg("inputs: self=%u elf=%u", (unsigned)self_len, (unsigned)elf_len);

    r = p5d_patch_elf(elf_buf, elf_len);
    log_msg("patch result=%d", r);
    if (r != P5D_OK) {
        printf("ERROR: patch ELF failed (%d)\n", r);
        goto fail;
    }
    ui_zh(5);

    r = p5d_inject(self_buf, self_len, elf_buf, elf_len, &out_buf, &out_len);
    log_msg("inject result=%d out_len=%u", r, (unsigned)out_len);
    if (r != P5D_OK) {
        printf("ERROR: inject failed (%d)\n", r);
        goto fail;
    }
    ui_ascii("SELF rebuilt OK", 48, g_ui_y, COLOR_GREY); g_ui_y += 20;

    sceIoMkdir("ux0:rePatch", 0777);
    sceIoMkdir(OUT_DIR, 0777);
    sceIoMkdir("ux0:rePatch/PCSH10109/data", 0777);
    if (write_whole_file(OUT_SELF, out_buf, out_len) < 0) {
        log_msg("ERROR: cannot write %s", OUT_SELF);
        printf("ERROR: cannot write rePatch eboot\n");
        goto fail;
    }
    log_msg("wrote %s (%u)", OUT_SELF, (unsigned)out_len);


    if (file_exists(OUT_CPK)) {
        log_msg("mod.cpk already in rePatch");
    } else if (file_exists(BASE "/mod.cpk")) {
        r = copy_file(BASE "/mod.cpk", OUT_CPK);
        log_msg("copy mod.cpk -> %d", r);
        if (r < 0) printf("WARNING: copy mod.cpk failed\n");
    } else {
        log_msg("WARNING: no mod.cpk");
        printf("WARNING: mod.cpk missing\n");
    }

    printf("\nDONE! Launch the game.\n");
    log_msg("DONE");
    wait_key_press();
    if (g_log >= 0) sceIoClose(g_log);
    return 0;

fail:
    ui_zh(7);
    ui_zh(8);
    wait_key_press();
    if (g_log >= 0) sceIoClose(g_log);
    return 1;
}
