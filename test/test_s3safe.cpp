// Standalone tests for src\s3safe.h (data-safety helpers). No network, uses a temp dir.
#include "../src/s3safe.h"
#include <cstdio>
#include <cstring>

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

    RemoveDirectoryW(g_dir.c_str());
    printf("passed=%d failed=%d\n", g_pass, g_fail);
    return g_fail ? 1 : 0;
}
