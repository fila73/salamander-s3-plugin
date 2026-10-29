// Pure, dependency-light helpers for data-safety decisions in the S3 plugin.
// Header-only so the standalone tests in test\ can compile it without the plugin.
#pragma once

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <cstdint>
#include <string>
#include <vector>

// ---------------------------------------------------------------------------
// Download integrity
// ---------------------------------------------------------------------------

// A download is complete only if the number of received bytes equals the
// Content-Length announced by the server (when the server sent one).
inline bool S3ReceivedSizeOk(uint64_t received, bool hasExpected, uint64_t expected)
{
    return !hasExpected || received == expected;
}

// Builds a unique temp file path next to the target (same directory => same volume,
// so the final MoveFileExW is a rename, not a copy).
inline std::wstring S3MakeTempPath(const std::wstring& target, unsigned attempt)
{
    wchar_t suffix[64];
    swprintf_s(suffix, L".s3part.%lu.%u", (unsigned long)GetCurrentProcessId(), attempt);
    return target + suffix;
}

// Atomically replaces target with tempFile. Returns false (and leaves tempFile) on failure.
inline bool S3ReplaceFile(const std::wstring& tempFile, const std::wstring& target, DWORD* outErr = nullptr)
{
    if (MoveFileExW(tempFile.c_str(), target.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
        return true;
    if (outErr) *outErr = GetLastError();
    return false;
}

// Writes the whole buffer; false if WriteFile fails or writes less than requested.
inline bool S3WriteAll(HANDLE h, const void* data, DWORD size)
{
    const char* p = (const char*)data;
    while (size > 0)
    {
        DWORD written = 0;
        if (!WriteFile(h, p, size, &written, NULL) || written == 0)
            return false;
        p += written;
        size -= written;
    }
    return true;
}

// ---------------------------------------------------------------------------
// Move (copy + delete source) decisions
// ---------------------------------------------------------------------------

// Result of transferring one top-level item (file or folder tree).
struct S3TransferReport
{
    std::vector<std::string> DoneItems;   // items fully transferred (files only)
    std::vector<std::string> FailedItems; // items that failed (with reason appended)
    bool Canceled = false;

    bool AllOk() const { return FailedItems.empty() && !Canceled; }
};

// The source of a Move may be removed as a whole only when everything was transferred.
inline bool S3MayDeleteWholeSource(const S3TransferReport& r)
{
    return r.AllOk();
}
