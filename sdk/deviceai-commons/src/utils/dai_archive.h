#ifndef DAI_ARCHIVE_H
#define DAI_ARCHIVE_H

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Extract a .tar.bz2 archive into dest_dir, streaming.
 *
 * Decompresses to a temporary .tar next to the archive in 64 KB chunks, then
 * walks that tar with fread, so peak memory stays flat regardless of archive
 * size. (The earlier iOS-only version inflated the whole tar into RAM — 70 MB+
 * for a Piper voice — which is the wrong shape for a 3-4 GB phone.)
 *
 * Handles ustar name+prefix and GNU 'L' long names, which sherpa-onnx's
 * model tarballs use. Rejects absolute paths and ".." components. Skips
 * symlinks and hardlinks.
 *
 * @param archive_path  Path to the .tar.bz2 file.
 * @param dest_dir      Directory to extract into (created if needed).
 * @return Number of regular files written, or -1 on error (with the reason
 *         logged). A partially written tree is left in place on error; the
 *         caller should delete dest_dir and retry.
 */
int dai_extract_tar_bz2(const char *archive_path, const char *dest_dir);

#ifdef __cplusplus
}
#endif

#endif /* DAI_ARCHIVE_H */
