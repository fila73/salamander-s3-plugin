// Standalone tests for src\s3safe.h (data-safety helpers). No network, uses a temp dir.
#include "../src/s3safe.h"
#include <cstdio>
#include <cstdlib>
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


static void TestExpectedSize()
{
    bool has; uint64_t exp;
    CHECK(S3ResolveExpectedSize(true, 10, -1, has, exp) && has && exp == 10);
    CHECK(S3ResolveExpectedSize(false, 0, 7, has, exp) && has && exp == 7);   // no header: listing size is used
    CHECK(S3ResolveExpectedSize(false, 0, -1, has, exp) && !has);
    CHECK(!S3ResolveExpectedSize(true, 10, 11, has, exp));                     // header vs listing mismatch
    CHECK(S3ResolveExpectedSize(true, 0, 0, has, exp) && has && exp == 0);
}

static void TestCopyResult()
{
    std::string e;
    CHECK(S3CopyResultOk(200, "<CopyObjectResult><ETag>x</ETag></CopyObjectResult>", e));
    CHECK(!S3CopyResultOk(0, "<CopyObjectResult/>", e));          // status unreadable
    CHECK(!S3CopyResultOk(204, "", e));
    CHECK(!S3CopyResultOk(200, "", e));                           // no body: not confirmed
    CHECK(!S3CopyResultOk(200, "<Error><Code>InternalError</Code></Error>", e));
    CHECK(!S3CopyResultOk(200, "<CopyObjectResult/><Error>x</Error>", e));
}

static void TestReservedNamesMore()
{
    std::string r;
    CHECK(!S3IsSafeLocalComponent("CONIN$", "k", r));
    CHECK(!S3IsSafeLocalComponent("conout$.txt", "k", r));
    CHECK(!S3IsSafeLocalComponent("CLOCK$", "k", r));
    CHECK(!S3IsSafeLocalComponent("COM0", "k", r));
    CHECK(!S3IsSafeLocalComponent("lpt0.txt", "k", r));
    CHECK(!S3IsSafeLocalComponent("COM\xC2\xB9", "k", r));       // COM + superscript one (UTF-8)
    CHECK(!S3IsSafeLocalComponent("LPT\xC2\xB3.log", "k", r));
    CHECK(!S3IsSafeLocalComponent("COM\xB2", "k", r));           // cp1252 form
    CHECK(S3IsSafeLocalComponent("COM10", "k", r));
    CHECK(S3IsSafeLocalComponent("CONSOLE", "k", r));
    CHECK(S3IsSafeLocalComponent(std::string(255, 'a'), "k", r));
    CHECK(!S3IsSafeLocalComponent(std::string(256, 'a'), "k", r));
}

static void TestWrittenSet()
{
    S3WrittenSet w;
    CHECK(w.Claim(L"C:\\t\\Report.txt"));
    CHECK(!w.Claim(L"C:\\t\\report.txt"));   // case-insensitive clash
    CHECK(!w.Claim(L"c:/t/REPORT.TXT"));
    CHECK(w.Claim(L"C:\\t\\other.txt"));
    w.Clear();
    CHECK(w.Claim(L"C:\\t\\report.txt"));
}

static std::string Narrow(const std::wstring& w)
{
    char b[1024];
    int n = WideCharToMultiByte(CP_ACP, 0, w.c_str(), (int)w.size(), b, sizeof b, NULL, NULL);
    return std::string(b, n > 0 ? n : 0);
}

static bool Junction(const std::wstring& link, const std::wstring& target)
{
    std::wstring cmd = L"cmd /c mklink /J \"" + link + L"\" \"" + target + L"\" >nul";
    return _wsystem(cmd.c_str()) == 0;
}

static void TestCloudReparseIsNotLink()
{
    // OneDrive folders are Directory|ReparsePoint with a cloud-files tag: not a link.
    char* up = getenv("USERPROFILE");
    if (!up) return;
    WIN32_FIND_DATAA fd;
    HANDLE h = FindFirstFileA((std::string(up) + "/OneDrive*").c_str(), &fd);
    if (h == INVALID_HANDLE_VALUE) { puts("note: no OneDrive folder, skipped"); return; }
    do
    {
        std::string od = std::string(up) + "/" + fd.cFileName;
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT)
        {
            printf("note: %s is a reparse point, tag=%08lx\n", od.c_str(), fd.dwReserved0);
            CHECK(!S3IsLinkPath(od.c_str()));
        }
    } while (FindNextFileA(h, &fd));
    FindClose(h);
}

static void TestDownloadableKey()
{
    CHECK(S3IsDownloadableKey("a/b/c"));
    CHECK(S3IsDownloadableKey("a/b/"));
    CHECK(!S3IsDownloadableKey(""));
    CHECK(!S3IsDownloadableKey("/x"));
    CHECK(!S3IsDownloadableKey("a//x"));
    CHECK(!S3IsDownloadableKey("a//"));
}

static void TestJunctionAndFinishMove()
{
    std::wstring root = g_dir + L"\\mv";
    std::wstring victim = g_dir + L"\\victim";
    CreateDirectoryW(root.c_str(), NULL);
    CreateDirectoryW(victim.c_str(), NULL);
    WriteTxt(victim + L"\\precious.txt", "keep me");
    WriteTxt(root + L"\\a.txt", "a");
    WriteTxt(root + L"\\late.txt", "created during transfer");
    std::wstring link = root + L"\\link";
    bool haveJ = Junction(link, victim);

    std::string rootA = Narrow(root);
    if (haveJ)
    {
        CHECK(S3IsReparseAttr(GetFileAttributesW(link.c_str())));
        CHECK(S3IsLinkPath(Narrow(link).c_str()));          // junction is a link
        CHECK(!S3IsLinkPath(Narrow(victim).c_str()));       // plain folder is not
        // nested junction is never followed
        S3LocalDeleteTree(rootA, true);
        CHECK(Exists(victim + L"\\precious.txt"));        // junction target untouched
        CHECK(Exists(link));                              // nested link kept
        CHECK(!Exists(root + L"\\a.txt"));
        // root item that is itself a junction: only the link goes away
        CHECK(S3LocalDeleteTree(Narrow(link), true));
        CHECK(!Exists(link));
        CHECK(Exists(victim + L"\\precious.txt"));
    }
    else
        printf("SKIP junction scenario (mklink /J unavailable)\n");

    // Move finish: only reported files + empty folders are removed
    CreateDirectoryW(root.c_str(), NULL);
    CreateDirectoryW((root + L"\\sub").c_str(), NULL);
    CreateDirectoryW((root + L"\\sub2").c_str(), NULL);
    WriteTxt(root + L"\\a.txt", "a");
    WriteTxt(root + L"\\late.txt", "late");
    WriteTxt(root + L"\\sub\\b.txt", "b");
    WriteTxt(root + L"\\sub2\\new.txt", "new");
    S3TransferReport rep;
    rep.DoneItems.push_back(rootA + "\\a.txt");
    rep.DoneItems.push_back(rootA + "\\sub\\b.txt");
    rep.DoneDirs.push_back(rootA + "\\sub");
    rep.DoneDirs.push_back(rootA + "\\sub2");
    rep.DoneDirs.push_back(rootA);
    S3LocalFinishMove(rep);
    CHECK(!Exists(root + L"\\a.txt"));
    CHECK(!Exists(root + L"\\sub"));                      // emptied -> removed
    CHECK(Exists(root + L"\\late.txt"));                  // created meanwhile -> stays
    CHECK(Exists(root + L"\\sub2\\new.txt"));
    CHECK(Exists(root));
    S3LocalDeleteTree(rootA, true);
    DeleteFileW((victim + L"\\precious.txt").c_str());
    RemoveDirectoryW(victim.c_str());
}

static void TestDeleteDepthLimit()
{
    std::string base = Narrow(g_dir);
    base += "\\deep";
    CreateDirectoryA(base.c_str(), NULL);
    std::string cur = base;
    for (int i = 0; i < 70; i++) { cur += "\\d"; CreateDirectoryA(cur.c_str(), NULL); }
    CHECK(!S3LocalDeleteTree(base, true));                // exceeds 64 levels: gives up, no crash
    // cleanup from the bottom
    while (cur.size() > base.size()) { RemoveDirectoryA(cur.c_str()); cur.resize(cur.size() - 2); }
    RemoveDirectoryA(base.c_str());
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
    TestExpectedSize();
    TestCopyResult();
    TestReservedNamesMore();
    TestWrittenSet();
    TestJunctionAndFinishMove();
    TestCloudReparseIsNotLink();
    TestDownloadableKey();
    TestDeleteDepthLimit();

    RemoveDirectoryW(g_dir.c_str());
    printf("passed=%d failed=%d\n", g_pass, g_fail);
    return g_fail ? 1 : 0;
}
