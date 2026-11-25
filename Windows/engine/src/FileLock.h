#pragma once

#include <atomic>
#include <filesystem>
#include <string>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#else
#include <fcntl.h>
#include <sys/file.h>
#include <unistd.h>
#endif

namespace rea {

/**
 * Holds [file].lock locked against other processes (and other handles in this one) for its
 * lifetime: every app the TIP is in saves the same learning files, each re-reading and merging
 * under it what the others saved. LockFileEx on Windows, flock elsewhere (the tests). The lock
 * goes with the handle, so a crashed saver never leaves it held. Not taken (the lock file can't
 * be opened or locked): locked() is false, and the caller doesn't write (its changes wait for the
 * next save), as an unlocked save could drop another app's.
 */
class FileLock {
public:
    explicit FileLock(const std::filesystem::path& file) {
        auto path = file;
        path += ".lock";
#ifdef _WIN32
        handle = CreateFileW(path.c_str(), GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
                             OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
        OVERLAPPED whole{};
        if (handle != INVALID_HANDLE_VALUE && !LockFileEx(handle, LOCKFILE_EXCLUSIVE_LOCK, 0, 1, 0, &whole)) {
            CloseHandle(handle);
            handle = INVALID_HANDLE_VALUE;
        }
#else
        fd = open(path.c_str(), O_RDWR | O_CREAT, 0644);
        if (fd >= 0 && flock(fd, LOCK_EX) != 0) { close(fd); fd = -1; }
#endif
    }
    ~FileLock() {
#ifdef _WIN32
        if (handle != INVALID_HANDLE_VALUE) CloseHandle(handle); // closing unlocks
#else
        if (fd >= 0) close(fd);
#endif
    }
    bool locked() const {
#ifdef _WIN32
        return handle != INVALID_HANDLE_VALUE;
#else
        return fd >= 0;
#endif
    }
    FileLock(const FileLock&) = delete;
    FileLock& operator=(const FileLock&) = delete;

    /** A temp file beside [file] no other save uses (this process's id and a count): written
     *  whole, then renamed over [file], so a crash mid-write keeps the old one. */
    static std::filesystem::path temp(const std::filesystem::path& file) {
        static std::atomic<unsigned> count{0};
#ifdef _WIN32
        auto pid = unsigned(GetCurrentProcessId());
#else
        auto pid = unsigned(getpid());
#endif
        auto out = file;
        out += "." + std::to_string(pid) + "." + std::to_string(++count) + ".tmp";
        return out;
    }

private:
#ifdef _WIN32
    HANDLE handle = INVALID_HANDLE_VALUE;
#else
    int fd = -1;
#endif
};

} // namespace rea
