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

// ---------------------------------------------------------------------------
// Streaming upload helpers
// ---------------------------------------------------------------------------

#include <bcrypt.h>
#pragma comment(lib, "bcrypt.lib")

// Largest object a single PUT can carry (S3 limit). Bigger files need multipart upload,
// which the plugin does not implement; they are refused with a clear error instead.
static const uint64_t S3_MAX_SINGLE_PUT = 5ULL * 1024 * 1024 * 1024;
static const DWORD S3_IO_CHUNK = 256 * 1024;

inline bool S3CheckUploadSize(uint64_t size, std::string& outError)
{
    if (size > S3_MAX_SINGLE_PUT)
    {
        outError = "File is larger than 5 GiB (" + std::to_string(size) +
                   " bytes), the limit of a single S3 upload. Multipart upload is not supported.";
        return false;
    }
    return true;
}

// Size of the next chunk to process: min(chunk, remaining); 0 when done.
inline DWORD S3NextChunk(uint64_t done, uint64_t total, DWORD chunk)
{
    if (done >= total) return 0;
    uint64_t rest = total - done;
    return (DWORD)(rest < chunk ? rest : chunk);
}

// Incremental SHA-256 (BCrypt).
class S3Sha256Stream
{
public:
    S3Sha256Stream()
    {
        if (BCryptOpenAlgorithmProvider(&m_alg, BCRYPT_SHA256_ALGORITHM, NULL, 0) < 0) { m_alg = NULL; return; }
        if (BCryptCreateHash(m_alg, &m_hash, NULL, 0, NULL, 0, 0) < 0) m_hash = NULL;
    }
    ~S3Sha256Stream()
    {
        if (m_hash) BCryptDestroyHash(m_hash);
        if (m_alg) BCryptCloseAlgorithmProvider(m_alg, 0);
    }
    S3Sha256Stream(const S3Sha256Stream&) = delete;
    S3Sha256Stream& operator=(const S3Sha256Stream&) = delete;
    bool Ok() const { return m_hash != NULL; }
    bool Update(const void* d, DWORD n) { return m_hash && BCryptHashData(m_hash, (PUCHAR)d, n, 0) >= 0; }
    bool FinishHex(std::string& out)
    {
        unsigned char h[32];
        if (!m_hash || BCryptFinishHash(m_hash, h, 32, 0) < 0) return false;
        static const char* hx = "0123456789abcdef";
        out.clear();
        for (int i = 0; i < 32; i++) { out += hx[h[i] >> 4]; out += hx[h[i] & 15]; }
        return true;
    }
private:
    BCRYPT_ALG_HANDLE m_alg = NULL;
    BCRYPT_HASH_HANDLE m_hash = NULL;
};

// Reads exactly `size` bytes from the current position of h in chunks, calling
// sink(data, n) for each chunk. Fails on a ReadFile error, a short file (it shrank)
// or a sink failure. buf must hold at least S3_IO_CHUNK bytes.
template <class Sink>
inline bool S3ReadStream(HANDLE h, uint64_t size, char* buf, Sink sink, std::string& outError)
{
    uint64_t done = 0;
    while (DWORD n = S3NextChunk(done, size, S3_IO_CHUNK))
    {
        DWORD got = 0;
        if (!ReadFile(h, buf, n, &got, NULL))
        {
            outError = "Cannot read local file (error " + std::to_string(GetLastError()) + ").";
            return false;
        }
        if (got != n)
        {
            outError = "Local file changed while reading (shorter than expected).";
            return false;
        }
        if (!sink(buf, got))
            return false;
        done += got;
    }
    return true;
}

// SHA-256 of the next `size` bytes of h, streamed (no whole-file buffer).
inline bool S3HashFileStream(HANDLE h, uint64_t size, char* buf, std::string& outHex, std::string& outError)
{
    S3Sha256Stream sha;
    if (!sha.Ok()) { outError = "SHA-256 initialisation failed."; return false; }
    if (!S3ReadStream(h, size, buf, [&](const char* d, DWORD n) { return sha.Update(d, n); }, outError))
    {
        if (outError.empty()) outError = "SHA-256 calculation failed.";
        return false;
    }
    if (!sha.FinishHex(outHex)) { outError = "SHA-256 calculation failed."; return false; }
    return true;
}

// ---------------------------------------------------------------------------
// Folder rename / recursive delete helpers
// ---------------------------------------------------------------------------

// Maps a key below oldPrefix to the same relative key below newPrefix.
// Returns false if key is not inside oldPrefix (never guess with substr).
inline bool S3MapRenameKey(const std::string& oldPrefix, const std::string& newPrefix,
                           const std::string& key, std::string& outNewKey)
{
    if (oldPrefix.empty() || key.size() < oldPrefix.size() || key.compare(0, oldPrefix.size(), oldPrefix) != 0)
        return false;
    outNewKey = newPrefix + key.substr(oldPrefix.size());
    return true;
}

// A folder rename may delete the old objects only when every copy succeeded.
inline bool S3MayDeleteOldPrefix(size_t totalObjects, size_t copiedOk, bool listingOk)
{
    return listingOk && copiedOk == totalObjects;
}
