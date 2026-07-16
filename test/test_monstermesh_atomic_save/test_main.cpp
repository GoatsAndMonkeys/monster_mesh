#include "modules/monstermesh/AtomicSdFile.h"

#include <Arduino.h>
#include <unity.h>

#include <algorithm>
#include <map>
#include <string>
#include <vector>

namespace
{

class MockFileSystem;

class MockFile
{
  public:
    MockFile() = default;
    MockFile(MockFileSystem *filesystem, const std::string &path, bool writable)
        : filesystem_(filesystem), path_(path), writable_(writable), open_(true)
    {
    }

    explicit operator bool() const { return open_; }
    size_t size() const;
    size_t write(const uint8_t *data, size_t size);
    size_t read(uint8_t *data, size_t size);
    void flush() {}
    void close() { open_ = false; }

  private:
    MockFileSystem *filesystem_ = nullptr;
    std::string path_;
    size_t position_ = 0;
    bool writable_ = false;
    bool open_ = false;
};

class MockFileSystem
{
  public:
    MockFile open(const char *path, const char *mode)
    {
        const bool writable = mode && mode[0] == 'w';
        if (writable) {
            if (failNextWriteOpen) {
                failNextWriteOpen = false;
                return MockFile();
            }
            files[path].clear(); // FILE_WRITE is truncating "w" mode.
            return MockFile(this, path, true);
        }
        if (!exists(path)) {
            return MockFile();
        }
        return MockFile(this, path, false);
    }

    bool exists(const char *path) const { return files.find(path) != files.end(); }

    bool remove(const char *path) { return files.erase(path) != 0; }

    bool rename(const char *from, const char *to)
    {
        if (failPromoteOnce && std::string(from).find(".tmp") != std::string::npos &&
            std::string(to).find(".tmp") == std::string::npos) {
            failPromoteOnce = false;
            return false;
        }
        const auto source = files.find(from);
        if (source == files.end() || exists(to)) {
            return false; // Match FatFS's no-replace behavior conservatively.
        }
        files[to] = source->second;
        files.erase(source);
        return true;
    }

    std::map<std::string, std::vector<uint8_t> > files;
    bool shortNextWrite = false;
    bool failNextWriteOpen = false;
    bool failPromoteOnce = false;
};

size_t MockFile::size() const
{
    if (!open_ || !filesystem_) return 0;
    return filesystem_->files[path_].size();
}

size_t MockFile::write(const uint8_t *data, size_t size)
{
    if (!open_ || !writable_ || !filesystem_) return 0;
    size_t accepted = size;
    if (filesystem_->shortNextWrite && size != 0) {
        filesystem_->shortNextWrite = false;
        accepted = size - 1;
    }
    filesystem_->files[path_].insert(filesystem_->files[path_].end(), data, data + accepted);
    position_ += accepted;
    return accepted;
}

size_t MockFile::read(uint8_t *data, size_t size)
{
    if (!open_ || writable_ || !filesystem_) return 0;
    const std::vector<uint8_t> &contents = filesystem_->files[path_];
    const size_t available = contents.size() - std::min(position_, contents.size());
    const size_t count = std::min(size, available);
    std::copy(contents.begin() + position_, contents.begin() + position_ + count, data);
    position_ += count;
    return count;
}

std::vector<uint8_t> bytes(const char *value)
{
    return std::vector<uint8_t>(value, value + strlen(value));
}

void test_success_keeps_previous_save_as_backup()
{
    MockFileSystem filesystem;
    filesystem.files["/game.sav"] = bytes("old-save");
    filesystem.files["/game.sav.bak"] = bytes("older-save");
    const std::vector<uint8_t> replacement = bytes("new-save");

    TEST_ASSERT_TRUE(monstermesh::atomic_sd_detail::atomicWriteFile(
        filesystem, "/game.sav", replacement.data(), replacement.size()));
    TEST_ASSERT_EQUAL_UINT8_ARRAY(replacement.data(), filesystem.files["/game.sav"].data(), replacement.size());
    const std::vector<uint8_t> expectedBackup = bytes("old-save");
    TEST_ASSERT_EQUAL_UINT8_ARRAY(
        expectedBackup.data(), filesystem.files["/game.sav.bak"].data(), expectedBackup.size());
    TEST_ASSERT_FALSE(filesystem.exists("/game.sav.tmp"));
}

void test_short_write_does_not_touch_existing_save()
{
    MockFileSystem filesystem;
    filesystem.files["/game.sav"] = bytes("known-good");
    filesystem.shortNextWrite = true;
    const std::vector<uint8_t> replacement = bytes("replacement");

    TEST_ASSERT_FALSE(monstermesh::atomic_sd_detail::atomicWriteFile(
        filesystem, "/game.sav", replacement.data(), replacement.size()));
    const std::vector<uint8_t> expected = bytes("known-good");
    TEST_ASSERT_EQUAL_UINT8_ARRAY(expected.data(), filesystem.files["/game.sav"].data(), expected.size());
    TEST_ASSERT_FALSE(filesystem.exists("/game.sav.tmp"));
}

void test_stale_temp_is_removed_and_missing_real_is_recovered_first()
{
    MockFileSystem filesystem;
    filesystem.files["/game.sav.tmp"] = bytes("partial");
    filesystem.files["/game.sav.bak"] = bytes("known-good");
    filesystem.failNextWriteOpen = true;
    const std::vector<uint8_t> replacement = bytes("replacement");

    TEST_ASSERT_FALSE(monstermesh::atomic_sd_detail::atomicWriteFile(
        filesystem, "/game.sav", replacement.data(), replacement.size()));
    const std::vector<uint8_t> expected = bytes("known-good");
    TEST_ASSERT_EQUAL_UINT8_ARRAY(expected.data(), filesystem.files["/game.sav"].data(), expected.size());
    TEST_ASSERT_FALSE(filesystem.exists("/game.sav.tmp"));
    TEST_ASSERT_FALSE(filesystem.exists("/game.sav.bak"));
}

void test_reader_recovers_interrupted_rotation_before_open()
{
    MockFileSystem filesystem;
    filesystem.files["/game.sav.tmp"] = bytes("verified-but-unpromoted");
    filesystem.files["/game.sav.bak"] = bytes("known-good");

    TEST_ASSERT_TRUE(monstermesh::atomic_sd_detail::recoverFile(
        filesystem, "/game.sav"));
    const std::vector<uint8_t> expected = bytes("known-good");
    TEST_ASSERT_EQUAL_UINT8_ARRAY(
        expected.data(), filesystem.files["/game.sav"].data(), expected.size());
    TEST_ASSERT_FALSE(filesystem.exists("/game.sav.bak"));
    // Recovery never promotes or deletes an untrusted temp file. The next
    // transaction will discard it before writing a fresh image.
    TEST_ASSERT_TRUE(filesystem.exists("/game.sav.tmp"));
}

void test_failed_promote_restores_backup_to_real_path()
{
    MockFileSystem filesystem;
    filesystem.files["/game.sav"] = bytes("known-good");
    filesystem.failPromoteOnce = true;
    const std::vector<uint8_t> replacement = bytes("replacement");

    TEST_ASSERT_FALSE(monstermesh::atomic_sd_detail::atomicWriteFile(
        filesystem, "/game.sav", replacement.data(), replacement.size()));
    const std::vector<uint8_t> expected = bytes("known-good");
    TEST_ASSERT_EQUAL_UINT8_ARRAY(expected.data(), filesystem.files["/game.sav"].data(), expected.size());
    TEST_ASSERT_FALSE(filesystem.exists("/game.sav.bak"));
    TEST_ASSERT_TRUE(filesystem.exists("/game.sav.tmp"));
}

void test_overlong_path_is_rejected_without_creating_files()
{
    MockFileSystem filesystem;
    std::string path = "/" + std::string(monstermesh::ATOMIC_SD_MAX_PATH_LENGTH, 'x');
    const std::vector<uint8_t> replacement = bytes("replacement");

    TEST_ASSERT_FALSE(monstermesh::atomic_sd_detail::atomicWriteFile(
        filesystem, path.c_str(), replacement.data(), replacement.size()));
    TEST_ASSERT_TRUE(filesystem.files.empty());
}

} // namespace

void setup()
{
    UNITY_BEGIN();
    RUN_TEST(test_success_keeps_previous_save_as_backup);
    RUN_TEST(test_short_write_does_not_touch_existing_save);
    RUN_TEST(test_stale_temp_is_removed_and_missing_real_is_recovered_first);
    RUN_TEST(test_reader_recovers_interrupted_rotation_before_open);
    RUN_TEST(test_failed_promote_restores_backup_to_real_path);
    RUN_TEST(test_overlong_path_is_rejected_without_creating_files);
    exit(UNITY_END());
}

void loop()
{
}
