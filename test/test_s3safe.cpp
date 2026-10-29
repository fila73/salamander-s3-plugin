// Standalone tests for src\s3safe.h (data-safety helpers). No network, uses a temp dir.
#include "../src/s3safe.h"
#include <cstdio>
#include <cstring>
#include <vector>

static int g_fail = 0, g_pass = 0;
#define CHECK(c) do { if (c) g_pass++; else { g_fail++; printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #c); } } while (0)

static std::wstring g_dir;

static bool WriteTxt(const std::wstring& p, const char* data)
{
    HANDLE h = CreateFileW(p.c_str(), GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (h == INVALID_HANDLE_VALUE) return false;
    bool ok = S3WriteAll(h, data, (DWORD)strlen(data));
    CloseHandle(h);
    return ok;
}
static std::string ReadTxt(const std::wstring& p)
{
    std::string r;
    HANDLE h = CreateFileW(p.c_str(), GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
    if (h == INVALID_HANDLE_VALUE) return "<missing>";
    char buf[256]; DWORD n;
    while (ReadFile(h, buf, sizeof buf, &n, NULL) && n) r.append(buf, n);
    CloseHandle(h);
    return r;
}
static bool Exists(const std::wstring& p) { return GetFileAttributesW(p.c_str()) != INVALID_FILE_ATTRIBUTES; }

static void TestContentLength()
{
    CHECK(S3ReceivedSizeOk(100, true, 100));
    CHECK(!S3ReceivedSizeOk(99, true, 100));   // truncated
    CHECK(!S3ReceivedSizeOk(101, true, 100));  // too long
    CHECK(S3ReceivedSizeOk(5, false, 0));      // no Content-Length: cannot verify
    CHECK(S3ReceivedSizeOk(0, true, 0));       // empty object
    CHECK(!S3ReceivedSizeOk(0, true, 10));
}

static void TestReplace()
{
    std::wstring target = g_dir + L"\\target.bin";
    std::wstring tmp0 = S3MakeTempPath(target, 0), tmp1 = S3MakeTempPath(target, 1);
    CHECK(tmp0 != tmp1);
    CHECK(tmp0.compare(0, target.size(), target) == 0); // next to the target

    // replace existing
    CHECK(WriteTxt(target, "old"));
    CHECK(WriteTxt(tmp0, "new-complete"));
    CHECK(S3ReplaceFile(tmp0, target));
    CHECK(ReadTxt(target) == "new-complete");
    CHECK(!Exists(tmp0));

    // replace when target absent
    DeleteFileW(target.c_str());
    CHECK(WriteTxt(tmp0, "fresh"));
    CHECK(S3ReplaceFile(tmp0, target));
    CHECK(ReadTxt(target) == "fresh");

    // failed replace (target locked) leaves target intact and reports error
    CHECK(WriteTxt(tmp1, "other"));
    HANDLE lock = CreateFileW(target.c_str(), GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
    DWORD err = 0;
    CHECK(!S3ReplaceFile(tmp1, target, &err));
    CHECK(err != 0);
    CloseHandle(lock);
    CHECK(ReadTxt(target) == "fresh");
    DeleteFileW(tmp1.c_str());
    DeleteFileW(target.c_str());
}

static void TestWriteAll()
{
    std::wstring p = g_dir + L"\\w.bin";
    std::string big(300000, 'x');
    HANDLE h = CreateFileW(p.c_str(), GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, 0, NULL);
    CHECK(S3WriteAll(h, big.data(), (DWORD)big.size()));
    CloseHandle(h);
    CHECK(ReadTxt(p).size() == big.size());
    // write to a read-only handle must fail
    HANDLE r = CreateFileW(p.c_str(), GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
    CHECK(!S3WriteAll(r, "abc", 3));
    CloseHandle(r);
    DeleteFileW(p.c_str());
}

static void TestMoveDecision()
{
    S3TransferReport r;
    CHECK(S3MayDeleteWholeSource(r));
    r.DoneItems.push_back("/b/a");
    CHECK(S3MayDeleteWholeSource(r));
    r.FailedItems.push_back("/b/c: truncated");
    CHECK(!S3MayDeleteWholeSource(r));
    S3TransferReport c;
    c.Canceled = true;
    CHECK(!S3MayDeleteWholeSource(c));
}

static void TestStreamingUpload()
{
    // chunk iteration
    CHECK(S3NextChunk(0, 10, 4) == 4);
    CHECK(S3NextChunk(8, 10, 4) == 2);
    CHECK(S3NextChunk(10, 10, 4) == 0);
    CHECK(S3NextChunk(0, 0, 4) == 0);
    CHECK(S3NextChunk(5000000000ULL, 6000000000ULL, S3_IO_CHUNK) == S3_IO_CHUNK); // 64-bit offsets
    uint64_t total = 1000003, done = 0, chunks = 0;
    while (DWORD n = S3NextChunk(done, total, S3_IO_CHUNK)) { done += n; chunks++; }
    CHECK(done == total && chunks == 4);

    // size limit
    std::string e;
    CHECK(S3CheckUploadSize(S3_MAX_SINGLE_PUT, e));
    CHECK(!S3CheckUploadSize(S3_MAX_SINGLE_PUT + 1, e) && e.find("5 GiB") != std::string::npos);

    // streamed hash of a real file (1,000,000 x 'a' spans several chunks)
    std::wstring p = g_dir + L"\\hash.bin";
    std::string big(1000000, 'a');
    CHECK(WriteTxt(p, big.c_str()));
    std::vector<char> buf(S3_IO_CHUNK);
    HANDLE h = CreateFileW(p.c_str(), GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
    std::string hex, err;
    CHECK(S3HashFileStream(h, big.size(), buf.data(), hex, err));
    CHECK(hex == "cdc76e5c9914fb9281a1c7e284d73e67f1809a48a497200e046d39ccc7112cd0");

    // abc
    CloseHandle(h);
    CHECK(WriteTxt(p, "abc"));
    h = CreateFileW(p.c_str(), GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
    CHECK(S3HashFileStream(h, 3, buf.data(), hex, err));
    CHECK(hex == "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
    // empty
    CHECK(S3HashFileStream(h, 0, buf.data(), hex, err));
    CHECK(hex == "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");
    // file shorter than announced size -> failure, not silent success
    LARGE_INTEGER z; z.QuadPart = 0; SetFilePointerEx(h, z, NULL, FILE_BEGIN);
    err.clear();
    CHECK(!S3HashFileStream(h, 10, buf.data(), hex, err) && !err.empty());
    // read error (write-only handle) is reported
    CloseHandle(h);
    HANDLE w = CreateFileW(p.c_str(), GENERIC_WRITE, 0, NULL, OPEN_EXISTING, 0, NULL);
    err.clear();
    CHECK(!S3HashFileStream(w, 3, buf.data(), hex, err) && !err.empty());
    CloseHandle(w);
    DeleteFileW(p.c_str());
}

static void TestRenameHelpers()
{
    std::string out;
    CHECK(S3MapRenameKey("a/old/", "a/new/", "a/old/x/y.txt", out) && out == "a/new/x/y.txt");
    CHECK(S3MapRenameKey("a/old/", "a/new/", "a/old/", out) && out == "a/new/");
    CHECK(!S3MapRenameKey("a/old/", "a/new/", "a/other/x", out));
    CHECK(!S3MapRenameKey("a/old/", "a/new/", "a/old", out)); // object named like the folder, not inside it
    CHECK(!S3MapRenameKey("", "n/", "x", out));
    CHECK(S3MayDeleteOldPrefix(3, 3, true));
    CHECK(!S3MayDeleteOldPrefix(3, 2, true));   // one copy failed
    CHECK(!S3MayDeleteOldPrefix(3, 3, false));  // listing failed
    CHECK(S3MayDeleteOldPrefix(0, 0, true));
}

static bool Safe(const char* n) { std::string r; return S3IsSafeLocalComponent(n, "k/" + std::string(n), r); }

static void TestSafeNames()
{
    CHECK(Safe("file.txt"));
    CHECK(Safe("my file (1).tar.gz"));
    CHECK(Safe(".gitignore"));
    CHECK(Safe("console.txt"));     // not a reserved name
    CHECK(Safe("COM10"));           // only COM1-9 are reserved
    CHECK(!Safe(""));
    CHECK(!Safe("."));
    CHECK(!Safe(".."));
    CHECK(!Safe("..\\evil"));
    CHECK(!Safe("a/b"));
    CHECK(!Safe("a\\b"));
    CHECK(!Safe("C:evil"));
    CHECK(!Safe("name."));
    CHECK(!Safe("name "));
    CHECK(!Safe("a*b"));
    CHECK(!Safe("a|b"));
    CHECK(!Safe("a\x01" "b"));
    CHECK(!Safe("CON"));
    CHECK(!Safe("con"));
    CHECK(!Safe("NUL.txt"));
    CHECK(!Safe("Aux.tar.gz"));
    CHECK(!Safe("com1"));
    CHECK(!Safe("LPT9.log"));
    CHECK(!Safe("CON .txt"));
    std::string r;
    S3IsSafeLocalComponent("..", "dir/..", r);
    CHECK(r.find("dir/..") != std::string::npos); // message lists the key
    CHECK(S3LastComponent("a/b/c.txt") == "c.txt");
    CHECK(S3LastComponent("a/b/") == "b");
    CHECK(S3LastComponent("x") == "x");
}

int main()
{
    wchar_t tmp[MAX_PATH];
    GetTempPathW(MAX_PATH, tmp);
    g_dir = std::wstring(tmp) + L"s3safe_test_" + std::to_wstring(GetCurrentProcessId());
    CreateDirectoryW(g_dir.c_str(), NULL);

    TestContentLength();
    TestReplace();
    TestWriteAll();
    TestMoveDecision();
    TestStreamingUpload();
    TestRenameHelpers();
    TestSafeNames();

    RemoveDirectoryW(g_dir.c_str());
    printf("passed=%d failed=%d\n", g_pass, g_fail);
    return g_fail ? 1 : 0;
}
