/**
 * archive_extract.c — streaming .tar.bz2 extractor. See dai_archive.h.
 */
#include "dai_archive.h"

#include <bzlib.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#ifdef ANDROID
#include <android/log.h>
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, "DaiArchive", __VA_ARGS__)
#else
#define LOGE(...) fprintf(stderr, __VA_ARGS__)
#endif

#define BLOCK 512
#define CHUNK (64 * 1024)
#define PATH_MAX_LEN 2048

typedef struct {
    char name[100];
    char mode[8];
    char uid[8];
    char gid[8];
    char size[12];
    char mtime[12];
    char checksum[8];
    char typeflag;
    char linkname[100];
    char magic[6];
    char version[2];
    char uname[32];
    char gname[32];
    char devmajor[8];
    char devminor[8];
    char prefix[155];
    char padding[12];
} tar_header_t;

static long octal_to_long(const char *s, int len) {
    long v = 0;
    for (int i = 0; i < len && s[i] >= '0' && s[i] <= '7'; i++) v = v * 8 + (s[i] - '0');
    return v;
}

static int mkdirs(const char *path) {
    char tmp[PATH_MAX_LEN];
    snprintf(tmp, sizeof(tmp), "%s", path);
    for (char *p = tmp + 1; *p; p++) {
        if (*p == '/') { *p = '\0'; mkdir(tmp, 0755); *p = '/'; }
    }
    if (mkdir(tmp, 0755) != 0 && errno != EEXIST) return -1;
    return 0;
}

/* Reject anything that could escape dest_dir. */
static int path_is_safe(const char *name) {
    if (name[0] == '/' || name[0] == '\\') return 0;
    const char *p = name;
    while (*p) {
        if (p[0] == '.' && p[1] == '.' && (p[2] == '/' || p[2] == '\0')) return 0;
        const char *slash = strchr(p, '/');
        if (!slash) break;
        p = slash + 1;
    }
    return 1;
}

/* ── Stage 1: bz2 → temporary tar on disk ─────────────────────────────── */

static int decompress_to_file(const char *bz2_path, const char *tar_path) {
    FILE *in = fopen(bz2_path, "rb");
    if (!in) { LOGE("open %s: %s", bz2_path, strerror(errno)); return -1; }
    FILE *out = fopen(tar_path, "wb");
    if (!out) { LOGE("create %s: %s", tar_path, strerror(errno)); fclose(in); return -1; }

    int bzerr = BZ_OK;
    BZFILE *bz = BZ2_bzReadOpen(&bzerr, in, 0, 0, NULL, 0);
    if (bzerr != BZ_OK) { LOGE("bzReadOpen failed: %d", bzerr); fclose(in); fclose(out); return -1; }

    unsigned char buf[CHUNK];
    int ok = 1;
    for (;;) {
        int n = BZ2_bzRead(&bzerr, bz, buf, CHUNK);
        if (bzerr != BZ_OK && bzerr != BZ_STREAM_END) { LOGE("bzRead failed: %d", bzerr); ok = 0; break; }
        if (n > 0 && fwrite(buf, 1, (size_t) n, out) != (size_t) n) { LOGE("write %s: %s", tar_path, strerror(errno)); ok = 0; break; }
        if (bzerr == BZ_STREAM_END) break;
    }
    BZ2_bzReadClose(&bzerr, bz);
    fclose(in);
    if (fclose(out) != 0) ok = 0;
    return ok ? 0 : -1;
}

/* ── Stage 2: walk the tar with fread ─────────────────────────────────── */

static int skip_padded(FILE *f, long size) {
    long padded = ((size + BLOCK - 1) / BLOCK) * BLOCK;
    return fseek(f, padded, SEEK_CUR);
}

static int copy_entry(FILE *f, long size, const char *fullpath) {
    FILE *out = fopen(fullpath, "wb");
    if (!out) { LOGE("create %s: %s", fullpath, strerror(errno)); return -1; }
    unsigned char buf[CHUNK];
    long left = size;
    while (left > 0) {
        size_t want = left > CHUNK ? CHUNK : (size_t) left;
        size_t got = fread(buf, 1, want, f);
        if (got == 0) { LOGE("truncated tar at %s", fullpath); fclose(out); return -1; }
        if (fwrite(buf, 1, got, out) != got) { LOGE("write %s: %s", fullpath, strerror(errno)); fclose(out); return -1; }
        left -= (long) got;
    }
    if (fclose(out) != 0) return -1;
    long pad = ((size + BLOCK - 1) / BLOCK) * BLOCK - size;
    if (pad > 0 && fseek(f, pad, SEEK_CUR) != 0) return -1;
    return 0;
}

static int extract_tar_file(const char *tar_path, const char *dest_dir) {
    FILE *f = fopen(tar_path, "rb");
    if (!f) { LOGE("open %s: %s", tar_path, strerror(errno)); return -1; }

    int files = 0;
    char longname[PATH_MAX_LEN];
    int have_longname = 0;
    unsigned char block[BLOCK];

    for (;;) {
        if (fread(block, 1, BLOCK, f) != BLOCK) break;

        int all_zero = 1;
        for (int i = 0; i < BLOCK; i++) if (block[i]) { all_zero = 0; break; }
        if (all_zero) break;

        const tar_header_t *h = (const tar_header_t *) block;
        long size = octal_to_long(h->size, 12);

        /* GNU long name: the next entry's name is the payload of this one. */
        if (h->typeflag == 'L') {
            if (size <= 0 || size >= (long) sizeof(longname)) { fclose(f); LOGE("long name too large"); return -1; }
            if (fread(longname, 1, (size_t) size, f) != (size_t) size) { fclose(f); return -1; }
            longname[size] = '\0';
            long pad = ((size + BLOCK - 1) / BLOCK) * BLOCK - size;
            if (pad > 0) fseek(f, pad, SEEK_CUR);
            have_longname = 1;
            continue;
        }

        char name[PATH_MAX_LEN];
        if (have_longname) {
            snprintf(name, sizeof(name), "%s", longname);
            have_longname = 0;
        } else if (h->prefix[0]) {
            snprintf(name, sizeof(name), "%.155s/%.100s", h->prefix, h->name);
        } else {
            snprintf(name, sizeof(name), "%.100s", h->name);
        }

        if (!path_is_safe(name)) {
            LOGE("skipping unsafe path in archive: %s", name);
            if (skip_padded(f, size) != 0) break;
            continue;
        }

        char fullpath[PATH_MAX_LEN];
        snprintf(fullpath, sizeof(fullpath), "%s/%s", dest_dir, name);

        switch (h->typeflag) {
            case '5':
                if (mkdirs(fullpath) != 0) { LOGE("mkdir %s: %s", fullpath, strerror(errno)); fclose(f); return -1; }
                /* POSIX allows a non-zero size on a directory entry (old and
                 * GNU-incremental tars write one); skip it or every following
                 * header is misread. */
                if (size > 0 && skip_padded(f, size) != 0) { fclose(f); return -1; }
                break;
            case '0':
            case '\0': {
                char parent[PATH_MAX_LEN];
                snprintf(parent, sizeof(parent), "%s", fullpath);
                char *slash = strrchr(parent, '/');
                if (slash) { *slash = '\0'; mkdirs(parent); }
                if (copy_entry(f, size, fullpath) != 0) { fclose(f); return -1; }
                files++;
                break;
            }
            default:
                /* symlink, hardlink, device, pax header: skip payload */
                if (skip_padded(f, size) != 0) { fclose(f); return -1; }
                break;
        }
    }

    fclose(f);
    return files;
}

/* ── Public API ───────────────────────────────────────────────────────── */

int dai_extract_tar_bz2(const char *archive_path, const char *dest_dir) {
    if (!archive_path || !dest_dir) return -1;
    if (mkdirs(dest_dir) != 0) { LOGE("mkdir %s: %s", dest_dir, strerror(errno)); return -1; }

    char tar_path[PATH_MAX_LEN];
    snprintf(tar_path, sizeof(tar_path), "%s.tar.tmp", archive_path);

    if (decompress_to_file(archive_path, tar_path) != 0) { unlink(tar_path); return -1; }
    int files = extract_tar_file(tar_path, dest_dir);
    unlink(tar_path);
    return files;
}
