#pragma once

#include "FSCommon.h"
#include <stddef.h>
#include <stdint.h>
#include <string.h>

namespace monstermesh
{

// The existing MonsterMesh paths are held in 256-byte buffers. Reserve enough
// space for the longest accepted path, a four-character sibling suffix, and
// the terminating NUL without ever silently truncating a path.
static const size_t ATOMIC_SD_MAX_PATH_LENGTH = 255;
static const size_t ATOMIC_SD_SIBLING_PATH_CAPACITY = ATOMIC_SD_MAX_PATH_LENGTH + sizeof(".tmp");

namespace atomic_sd_detail
{

inline bool makeSiblingPath(const char *path, const char *suffix, char *out, size_t outCapacity)
{
    if (!path || !suffix || !out || outCapacity == 0 || path[0] != '/') {
        return false;
    }

    size_t pathLength = 0;
    while (pathLength <= ATOMIC_SD_MAX_PATH_LENGTH && path[pathLength] != '\0') {
        ++pathLength;
    }
    if (pathLength == 0 || pathLength > ATOMIC_SD_MAX_PATH_LENGTH) {
        return false;
    }

    const size_t suffixLength = strlen(suffix);
    if (pathLength > outCapacity - 1 || suffixLength > outCapacity - pathLength - 1) {
        return false;
    }

    memcpy(out, path, pathLength);
    memcpy(out + pathLength, suffix, suffixLength + 1);
    return true;
}

template <typename FileSystem>
bool verifyContents(FileSystem &filesystem, const char *path, const uint8_t *expected, size_t expectedSize)
{
    auto file = filesystem.open(path, FILE_O_READ);
    if (!file) {
        return false;
    }
    if (file.size() != expectedSize) {
        file.close();
        return false;
    }

    uint8_t buffer[256];
    size_t offset = 0;
    while (offset < expectedSize) {
        const size_t remaining = expectedSize - offset;
        const size_t chunk = remaining < sizeof(buffer) ? remaining : sizeof(buffer);
        const size_t read = file.read(buffer, chunk);
        if (read != chunk || memcmp(buffer, expected + offset, chunk) != 0) {
            file.close();
            return false;
        }
        offset += chunk;
    }

    file.close();
    return true;
}

template <typename FileSystem>
void removeTempBestEffort(FileSystem &filesystem, const char *tempPath)
{
    if (filesystem.exists(tempPath)) {
        (void)filesystem.remove(tempPath);
    }
}

template <typename FileSystem>
bool restoreBackup(FileSystem &filesystem, const char *path, const char *backupPath)
{
    // A failed promote is not expected to create the target, but remove an
    // unexpected/partially promoted target before putting the known-good file
    // back. If removal fails, leave the backup untouched for manual recovery.
    if (filesystem.exists(path) && !filesystem.remove(path)) {
        return false;
    }
    return filesystem.rename(backupPath, path);
}

// Recover the one interrupted state that ordinary readers cannot otherwise
// see: the old real file has already been rotated to .bak, but the verified
// .tmp has not yet been promoted.  A present real path always remains
// authoritative; .bak is only auto-restored when the real path is absent.
template <typename FileSystem>
bool recoverFile(FileSystem &filesystem, const char *path)
{
    char backupPath[ATOMIC_SD_SIBLING_PATH_CAPACITY];
    if (!makeSiblingPath(path, ".bak", backupPath, sizeof(backupPath))) {
        return false;
    }
    if (filesystem.exists(path)) {
        return true;
    }
    return filesystem.exists(backupPath) && filesystem.rename(backupPath, path);
}

// Roll back a just-promoted file when a caller-specific commit step fails.
// If the rename itself fails after the target was removed, .bak deliberately
// remains in place so recoverFile() can restore it on the next read.
template <typename FileSystem>
bool restorePreviousFile(FileSystem &filesystem, const char *path)
{
    char backupPath[ATOMIC_SD_SIBLING_PATH_CAPACITY];
    if (!makeSiblingPath(path, ".bak", backupPath, sizeof(backupPath)) ||
        !filesystem.exists(backupPath)) {
        return false;
    }
    return restoreBackup(filesystem, path, backupPath);
}

// The caller owns SD initialization and the non-recursive spiLock. Keeping the
// lock outside this helper lets all MonsterMesh call sites use one transaction
// without accidentally taking spiLock twice.
template <typename FileSystem>
bool atomicWriteFile(FileSystem &filesystem, const char *path, const uint8_t *data, size_t size)
{
    if (!path || (!data && size != 0)) {
        return false;
    }

    char tempPath[ATOMIC_SD_SIBLING_PATH_CAPACITY];
    char backupPath[ATOMIC_SD_SIBLING_PATH_CAPACITY];
    if (!makeSiblingPath(path, ".tmp", tempPath, sizeof(tempPath)) ||
        !makeSiblingPath(path, ".bak", backupPath, sizeof(backupPath))) {
        return false;
    }

    // A .tmp is never authoritative. It may be a partial write or a verified
    // image whose promotion was interrupted; in either case the real/.bak
    // pair is the recovery source.
    if (filesystem.exists(tempPath) && !filesystem.remove(tempPath)) {
        return false;
    }

    bool originalExists = filesystem.exists(path);
    if (!originalExists && filesystem.exists(backupPath)) {
        // Recover an interrupted real -> .bak rotation before attempting a new
        // transaction. A later write failure will then still leave the real
        // save at its normal path.
        if (!filesystem.rename(backupPath, path)) {
            return false;
        }
        originalExists = true;
    }

    auto temp = filesystem.open(tempPath, FILE_O_WRITE);
    if (!temp) {
        return false;
    }

    const size_t written = size == 0 ? 0 : temp.write(data, size);
    if (written != size) {
        temp.close();
        removeTempBestEffort(filesystem, tempPath);
        return false;
    }

    // Arduino-ESP32 File::flush() is void. Its VFS implementation performs
    // fflush() and fsync(), so close-and-readback below is the strongest error
    // signal exposed through the portable FS API.
    temp.flush();
    temp.close();

    if (!verifyContents(filesystem, tempPath, data, size)) {
        removeTempBestEffort(filesystem, tempPath);
        return false;
    }

    if (originalExists) {
        // FAT rename does not portably replace an existing destination. Remove
        // the older backup only after the new temp image has been verified.
        if (filesystem.exists(backupPath) && !filesystem.remove(backupPath)) {
            removeTempBestEffort(filesystem, tempPath);
            return false;
        }
        if (!filesystem.rename(path, backupPath)) {
            removeTempBestEffort(filesystem, tempPath);
            return false;
        }
    }

    if (!filesystem.rename(tempPath, path)) {
        if (originalExists) {
            (void)restoreBackup(filesystem, path, backupPath);
        }
        return false;
    }

    // Verify the promoted name as well. This catches a filesystem/backend that
    // reports a successful rename but exposes incomplete data afterward.
    if (!verifyContents(filesystem, path, data, size)) {
        if (originalExists) {
            (void)restoreBackup(filesystem, path, backupPath);
        }
        return false;
    }

    // On replacement, backupPath intentionally remains as the previous known-
    // good save. On a first-ever write there is no prior image to preserve.
    return true;
}

} // namespace atomic_sd_detail

} // namespace monstermesh
