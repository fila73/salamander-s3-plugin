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

// Combines the Content-Length header (if any) with the size known from the listing
// (listedSize < 0 = unknown). Outputs the size to verify against; false if both are
// known and disagree (object changed since it was listed).
inline bool S3ResolveExpectedSize(bool hasHeader, uint64_t header, int64_t listedSize,
                                  bool& outHas, uint64_t& outExpected)
{
    outHas = false;
    outExpected = 0;
    if (hasHeader && listedSize >= 0 && header != (uint64_t)listedSize)
        return false;
    if (hasHeader) { outHas = true; outExpected = header; }
    else if (listedSize >= 0) { outHas = true; outExpected = (uint64_t)listedSize; }
    return true;
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
    std::vector<std::string> DoneDirs;    // folders traversed, children before parents (removed on Move only if empty)
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

// ---------------------------------------------------------------------------
// Local path component safety (S3 keys are arbitrary strings)
// ---------------------------------------------------------------------------

#include <cctype>

// True if `name` can be used as ONE local path component without escaping the target
// directory or hitting a device. On false, `reason` says why. `key` is only used in the message.
inline bool S3IsSafeLocalComponent(const std::string& name, const std::string& key, std::string& reason)
{
    auto fail = [&](const char* why)
    {
        reason = std::string("Unsafe object name \"") + key + "\": " + why + ".";
        return false;
    };
    if (name.empty()) return fail("empty name");
    if (name == "." || name == "..") return fail("'.' and '..' are not allowed");
    for (unsigned char c : name)
    {
        if (c < 32) return fail("control character");
        if (c == '\\' || c == '/') return fail("contains a path separator");
        if (c == ':') return fail("contains ':'");
        if (c == '*' || c == '?' || c == '"' || c == '<' || c == '>' || c == '|') return fail("contains a character not allowed in file names");
    }
    char last = name.back();
    if (last == '.' || last == ' ') return fail("name ends with a dot or space");

    // names longer than 255 UTF-16 units cannot be created
    {
        int wl = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, name.c_str(), (int)name.size(), NULL, 0);
        size_t units = wl > 0 ? (size_t)wl : name.size();
        if (units > 255) return fail("name is longer than 255 characters");
    }

    // reserved device names, with or without extension: CON, PRN, AUX, NUL, CONIN$, CONOUT$, CLOCK$,
    // COM0-9, LPT0-9 (also with superscript digits 1-3)
    std::string stem = name.substr(0, name.find('.'));
    while (!stem.empty() && stem.back() == ' ') stem.pop_back();
    std::string up;
    for (unsigned char c : stem) up += (char)std::toupper(c);
    bool reserved = (up == "CON" || up == "PRN" || up == "AUX" || up == "NUL" ||
                     up == "CONIN$" || up == "CONOUT$" || up == "CLOCK$");
    if (up.size() >= 4 && (up.compare(0, 3, "COM") == 0 || up.compare(0, 3, "LPT") == 0))
    {
        std::string d = up.substr(3);
        if (d.size() == 1 && d[0] >= '0' && d[0] <= '9') reserved = true;
        if (d == "\xC2\xB9" || d == "\xC2\xB2" || d == "\xC2\xB3") reserved = true; // UTF-8 superscript 1,2,3
        if (d.size() == 1 && ((unsigned char)d[0] == 0xB9 || (unsigned char)d[0] == 0xB2 || (unsigned char)d[0] == 0xB3))
            reserved = true; // ANSI (cp1252) superscript 1,2,3
    }
    if (reserved) return fail("reserved device name");
    return true;
}

// Last component of an S3 path/key ("a/b/c.txt" -> "c.txt", trailing '/' ignored).
inline std::string S3LastComponent(const std::string& path)
{
    std::string p = path;
    while (!p.empty() && p.back() == '/') p.pop_back();
    size_t s = p.rfind('/');
    return s == std::string::npos ? p : p.substr(s + 1);
}

// ---------------------------------------------------------------------------
// Local collision / reparse point / Move-cleanup helpers
// ---------------------------------------------------------------------------

#include <set>
#pragma comment(lib, "user32.lib")

static const int S3_MAX_RECURSION_DEPTH = 64;

inline bool S3IsReparseAttr(DWORD attr)
{
    return attr != INVALID_FILE_ATTRIBUTES && (attr & FILE_ATTRIBUTE_REPARSE_POINT) != 0;
}

// Case-insensitive form of a local path (NTFS is case-insensitive).
inline std::wstring S3FoldPath(const std::wstring& path)
{
    std::wstring r = path;
    if (!r.empty()) CharUpperBuffW(&r[0], (DWORD)r.size());
    for (auto& c : r) if (c == L'/') c = L'\\';
    return r;
}

// Set of local files written by one operation. Claim() fails if a name that differs only
// by case (or is identical) was already claimed: the second item must not overwrite the first.
class S3WrittenSet
{
public:
    bool Claim(const std::wstring& path) { return m_set.insert(S3FoldPath(path)).second; }
    void Clear() { m_set.clear(); }
private:
    std::set<std::wstring> m_set;
};

// Deletes a local tree for a completed Move. Never follows reparse points (junctions, dir symlinks):
// a reparse-point folder is only removed (link itself) when it is the root item, nested ones are kept.
// Returns true if path is gone afterwards.
inline bool S3LocalDeleteTree(const std::string& path, bool isDir, int depth = 0)
{
    if (!isDir)
    {
        SetFileAttributesA(path.c_str(), FILE_ATTRIBUTE_NORMAL);
        return DeleteFileA(path.c_str()) != 0;
    }
    DWORD attr = GetFileAttributesA(path.c_str());
    if (S3IsReparseAttr(attr))
        return depth == 0 && RemoveDirectoryA(path.c_str()) != 0; // only the link, never the target
    if (depth >= S3_MAX_RECURSION_DEPTH)
        return false;

    WIN32_FIND_DATAA fd;
    HANDLE h = FindFirstFileA((path + "\\*.*").c_str(), &fd);
    if (h != INVALID_HANDLE_VALUE)
    {
        do
        {
            if (strcmp(fd.cFileName, ".") == 0 || strcmp(fd.cFileName, "..") == 0) continue;
            bool sub = (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
            if (sub && S3IsReparseAttr(fd.dwFileAttributes)) continue; // keep nested links
            S3LocalDeleteTree(path + "\\" + fd.cFileName, sub, depth + 1);
        } while (FindNextFileA(h, &fd));
        FindClose(h);
    }
    return RemoveDirectoryA(path.c_str()) != 0;
}

// Move cleanup after an upload: delete exactly the files that were uploaded, then the folders
// that were traversed - but only those that are empty now (RemoveDirectory fails otherwise).
// Anything created during the transfer, and anything not transferred, stays.
inline void S3LocalFinishMove(const S3TransferReport& r)
{
    for (const auto& f : r.DoneItems)
        S3LocalDeleteTree(f, false);
    for (const auto& d : r.DoneDirs)
        RemoveDirectoryA(d.c_str());
}

// A server-side copy succeeded only with HTTP 200 and a <CopyObjectResult> body. S3 may answer 200
// with an <Error> body when the copy fails mid-way; status 0 means the status could not be read.
inline bool S3CopyResultOk(unsigned long status, const std::string& body, std::string& outError)
{
    if (status != 200)
    {
        outError = "Copy failed: unexpected HTTP status " + std::to_string(status) + ".";
        return false;
    }
    if (body.find("<Error>") != std::string::npos || body.find("<Error ") != std::string::npos)
    {
        outError = "Copy failed: server returned an error in the response body.";
        return false;
    }
    if (body.find("<CopyObjectResult") == std::string::npos)
    {
        outError = "Copy not confirmed: response has no <CopyObjectResult>.";
        return false;
    }
    return true;
}
