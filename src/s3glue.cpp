// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (c) 2026 Open Salamander Authors & Dupl3xx
// S3 Glue layer between Salamander FS and CS3Connection + Path Helpers

#include "precomp.h"
#include "s3glue.h"
#include <cstdio>
#include <cstring>
#include <algorithm>

CS3Connection S3Conn;
S3Profile S3ActiveProfile;
S3Profile S3SavedProfiles[S3_MAX_PROFILES];
int S3SavedProfileCount = 0;
char S3DefaultSession[128] = "";

void S3Normalize(char* path)
{
    if (!path) return;
    
    // Replace backslashes with slashes
    for (char* p = path; *p; ++p)
    {
        if (*p == '\\') *p = '/';
    }

    // Collapse consecutive slashes
    char* src = path;
    char* dst = path;
    bool prevSlash = false;
    while (*src)
    {
        if (*src == '/')
        {
            if (!prevSlash)
            {
                *dst++ = *src;
                prevSlash = true;
            }
        }
        else
        {
            *dst++ = *src;
            prevSlash = false;
        }
        src++;
    }
    *dst = '\0';

    // Ensure leading slash
    if (path[0] != '/')
    {
        size_t len = strlen(path);
        memmove(path + 1, path, len + 1);
        path[0] = '/';
    }

    // Strip trailing slash if not root
    size_t len = strlen(path);
    if (len > 1 && path[len - 1] == '/')
    {
        path[len - 1] = '\0';
    }
}

void S3Join(const char* base, const char* name, char* out, int outSize)
{
    if (!out || outSize <= 0) return;
    out[0] = '\0';

    if (!base || base[0] == '\0')
    {
        snprintf(out, outSize, "/%s", name ? name : "");
    }
    else
    {
        size_t baseLen = strlen(base);
        if (base[baseLen - 1] == '/')
        {
            snprintf(out, outSize, "%s%s", base, name ? name : "");
        }
        else
        {
            snprintf(out, outSize, "%s/%s", base, name ? name : "");
        }
    }
    S3Normalize(out);
}

void S3Parent(const char* path, char* out, int outSize)
{
    if (!out || outSize <= 0) return;
    out[0] = '\0';
    if (!path || strcmp(path, "/") == 0 || path[0] == '\0')
    {
        snprintf(out, outSize, "/");
        return;
    }

    char temp[MAX_PATH];
    strncpy_s(temp, path, _TRUNCATE);
    S3Normalize(temp);

    char* lastSlash = strrchr(temp, '/');
    if (!lastSlash || lastSlash == temp)
    {
        snprintf(out, outSize, "/");
    }
    else
    {
        *lastSlash = '\0';
        snprintf(out, outSize, "%s", temp);
    }
}

bool S3IsRoot(const char* path)
{
    if (!path) return true;
    return (strcmp(path, "/") == 0 || strcmp(path, "\\") == 0 || path[0] == '\0');
}

bool S3ParsePath(const char* fullPath, std::string& outBucket, std::string& outPrefix)
{
    outBucket.clear();
    outPrefix.clear();

    if (!fullPath || S3IsRoot(fullPath))
    {
        return true;
    }

    char norm[MAX_PATH];
    strncpy_s(norm, fullPath, _TRUNCATE);
    S3Normalize(norm);

    const char* p = norm;
    if (*p == '/') p++;
    if (*p == '\0') return true;

    const char* slash = strchr(p, '/');
    if (!slash)
    {
        outBucket = p;
        outPrefix = "";
    }
    else
    {
        outBucket = std::string(p, slash - p);
        outPrefix = (slash + 1);
    }

    return true;
}

bool S3EnsureConnected(HWND parent)
{
    if (S3Conn.IsConnected())
    {
        return true;
    }

    if (S3ActiveProfile.AccessKeyId.empty() && S3ActiveProfile.FromAwsCredentials)
    {
        AwsProfile ap;
        if (CAwsCredManager::FindProfile(S3ActiveProfile.AwsProfileName, ap))
        {
            S3ActiveProfile.AccessKeyId = ap.AccessKeyId;
            S3ActiveProfile.SecretAccessKey = ap.SecretAccessKey;
            S3ActiveProfile.SessionToken = ap.SessionToken;
            if (S3ActiveProfile.Region.empty()) S3ActiveProfile.Region = ap.Region;
            if (S3ActiveProfile.Endpoint.empty()) S3ActiveProfile.Endpoint = ap.EndpointUrl;
        }
    }

    if (S3ActiveProfile.AccessKeyId.empty() || S3ActiveProfile.SecretAccessKey.empty())
    {
        return false;
    }

    std::string err;
    if (!S3Conn.Connect(S3ActiveProfile, err))
    {
        if (parent)
        {
            SalamanderGeneral->SalMessageBox(parent, err.c_str(), LoadStr(IDS_TITLE_CONN_ERR), MB_OK | MB_ICONERROR);
        }
        return false;
    }

    return true;
}

void S3LoadProfilesFromRegistry(HKEY rootKey)
{
    // Profile loading logic for saved sessions in Windows Registry
    HKEY hKey = NULL;
    if (RegOpenKeyExA(rootKey, "Software\\OpenSalamander\\S3\\Profiles", 0, KEY_READ, &hKey) == ERROR_SUCCESS)
    {
        DWORD count = 0;
        DWORD type = REG_DWORD;
        DWORD cbData = sizeof(count);
        if (RegQueryValueExA(hKey, "Count", NULL, &type, (LPBYTE)&count, &cbData) == ERROR_SUCCESS)
        {
            S3SavedProfileCount = std::min((int)count, S3_MAX_PROFILES);
            for (int i = 0; i < S3SavedProfileCount; ++i)
            {
                char subKeyName[32];
                snprintf(subKeyName, sizeof(subKeyName), "Profile%d", i);
                HKEY hSub = NULL;
                if (RegOpenKeyExA(hKey, subKeyName, 0, KEY_READ, &hSub) == ERROR_SUCCESS)
                {
                    char buf[512];
                    DWORD bufSize = sizeof(buf);

                    bufSize = sizeof(buf);
                    if (RegQueryValueExA(hSub, "Name", NULL, NULL, (LPBYTE)buf, &bufSize) == ERROR_SUCCESS)
                        S3SavedProfiles[i].Name = buf;

                    bufSize = sizeof(buf);
                    if (RegQueryValueExA(hSub, "AccessKeyId", NULL, NULL, (LPBYTE)buf, &bufSize) == ERROR_SUCCESS)
                        S3SavedProfiles[i].AccessKeyId = buf;

                    bufSize = sizeof(buf);
                    if (RegQueryValueExA(hSub, "SecretAccessKey", NULL, NULL, (LPBYTE)buf, &bufSize) == ERROR_SUCCESS)
                        S3SavedProfiles[i].SecretAccessKey = buf;

                    bufSize = sizeof(buf);
                    if (RegQueryValueExA(hSub, "SessionToken", NULL, NULL, (LPBYTE)buf, &bufSize) == ERROR_SUCCESS)
                        S3SavedProfiles[i].SessionToken = buf;

                    bufSize = sizeof(buf);
                    if (RegQueryValueExA(hSub, "Region", NULL, NULL, (LPBYTE)buf, &bufSize) == ERROR_SUCCESS)
                        S3SavedProfiles[i].Region = buf;

                    bufSize = sizeof(buf);
                    if (RegQueryValueExA(hSub, "Endpoint", NULL, NULL, (LPBYTE)buf, &bufSize) == ERROR_SUCCESS)
                        S3SavedProfiles[i].Endpoint = buf;

                    bufSize = sizeof(buf);
                    if (RegQueryValueExA(hSub, "InitialBucket", NULL, NULL, (LPBYTE)buf, &bufSize) == ERROR_SUCCESS)
                        S3SavedProfiles[i].InitialBucket = buf;

                    DWORD dwVal = 0;
                    DWORD dwSize = sizeof(dwVal);
                    if (RegQueryValueExA(hSub, "UseSSL", NULL, NULL, (LPBYTE)&dwVal, &dwSize) == ERROR_SUCCESS)
                        S3SavedProfiles[i].UseSSL = (dwVal != 0);

                    dwSize = sizeof(dwVal);
                    if (RegQueryValueExA(hSub, "PathStyle", NULL, NULL, (LPBYTE)&dwVal, &dwSize) == ERROR_SUCCESS)
                        S3SavedProfiles[i].PathStyle = (dwVal != 0);

                    dwSize = sizeof(dwVal);
                    if (RegQueryValueExA(hSub, "FromAwsCred", NULL, NULL, (LPBYTE)&dwVal, &dwSize) == ERROR_SUCCESS)
                        S3SavedProfiles[i].FromAwsCredentials = (dwVal != 0);

                    bufSize = sizeof(buf);
                    if (RegQueryValueExA(hSub, "AwsProfileName", NULL, NULL, (LPBYTE)buf, &bufSize) == ERROR_SUCCESS)
                        S3SavedProfiles[i].AwsProfileName = buf;

                    RegCloseKey(hSub);
                }
            }
        }
        RegCloseKey(hKey);
    }
}

void S3SaveProfilesToRegistry(HKEY rootKey)
{
    HKEY hKey = NULL;
    if (RegCreateKeyExA(rootKey, "Software\\OpenSalamander\\S3\\Profiles", 0, NULL, 0, KEY_WRITE, NULL, &hKey, NULL) == ERROR_SUCCESS)
    {
        DWORD count = (DWORD)S3SavedProfileCount;
        RegSetValueExA(hKey, "Count", 0, REG_DWORD, (const BYTE*)&count, sizeof(count));

        for (int i = 0; i < S3SavedProfileCount; ++i)
        {
            char subKeyName[32];
            snprintf(subKeyName, sizeof(subKeyName), "Profile%d", i);
            HKEY hSub = NULL;
            if (RegCreateKeyExA(hKey, subKeyName, 0, NULL, 0, KEY_WRITE, NULL, &hSub, NULL) == ERROR_SUCCESS)
            {
                RegSetValueExA(hSub, "Name", 0, REG_SZ, (const BYTE*)S3SavedProfiles[i].Name.c_str(), (DWORD)S3SavedProfiles[i].Name.length() + 1);
                RegSetValueExA(hSub, "AccessKeyId", 0, REG_SZ, (const BYTE*)S3SavedProfiles[i].AccessKeyId.c_str(), (DWORD)S3SavedProfiles[i].AccessKeyId.length() + 1);
                RegSetValueExA(hSub, "SecretAccessKey", 0, REG_SZ, (const BYTE*)S3SavedProfiles[i].SecretAccessKey.c_str(), (DWORD)S3SavedProfiles[i].SecretAccessKey.length() + 1);
                RegSetValueExA(hSub, "SessionToken", 0, REG_SZ, (const BYTE*)S3SavedProfiles[i].SessionToken.c_str(), (DWORD)S3SavedProfiles[i].SessionToken.length() + 1);
                RegSetValueExA(hSub, "Region", 0, REG_SZ, (const BYTE*)S3SavedProfiles[i].Region.c_str(), (DWORD)S3SavedProfiles[i].Region.length() + 1);
                RegSetValueExA(hSub, "Endpoint", 0, REG_SZ, (const BYTE*)S3SavedProfiles[i].Endpoint.c_str(), (DWORD)S3SavedProfiles[i].Endpoint.length() + 1);
                RegSetValueExA(hSub, "InitialBucket", 0, REG_SZ, (const BYTE*)S3SavedProfiles[i].InitialBucket.c_str(), (DWORD)S3SavedProfiles[i].InitialBucket.length() + 1);

                DWORD dwVal = S3SavedProfiles[i].UseSSL ? 1 : 0;
                RegSetValueExA(hSub, "UseSSL", 0, REG_DWORD, (const BYTE*)&dwVal, sizeof(dwVal));

                dwVal = S3SavedProfiles[i].PathStyle ? 1 : 0;
                RegSetValueExA(hSub, "PathStyle", 0, REG_DWORD, (const BYTE*)&dwVal, sizeof(dwVal));

                dwVal = S3SavedProfiles[i].FromAwsCredentials ? 1 : 0;
                RegSetValueExA(hSub, "FromAwsCred", 0, REG_DWORD, (const BYTE*)&dwVal, sizeof(dwVal));

                RegSetValueExA(hSub, "AwsProfileName", 0, REG_SZ, (const BYTE*)S3SavedProfiles[i].AwsProfileName.c_str(), (DWORD)S3SavedProfiles[i].AwsProfileName.length() + 1);

                RegCloseKey(hSub);
            }
        }
        RegCloseKey(hKey);
    }
}

static std::wstring AnsiToWide(const std::string& str)
{
    if (str.empty()) return L"";
    int size = MultiByteToWideChar(CP_ACP, 0, str.c_str(), (int)str.size(), NULL, 0);
    std::wstring wstr(size, 0);
    MultiByteToWideChar(CP_ACP, 0, str.c_str(), (int)str.size(), &wstr[0], size);
    return wstr;
}

static S3GlueProgressFn g_S3ProgressCb = nullptr;
static void* g_S3ProgressCtx = nullptr;

void S3CalcSize(HWND parent, const char* remoteDir, int panel)
{
    if (!S3EnsureConnected(parent)) return;

    std::string bucket, prefix;
    S3ParsePath(remoteDir, bucket, prefix);

    int index = 0;
    BOOL isDir = FALSE;
    const CFileData* f = NULL;
    BOOL focused = (SalamanderGeneral->GetPanelSelectedItem(panel, &index, &isDir) == NULL);
    index = 0;

    uint64_t totalBytes = 0;
    int totalFiles = 0;
    int totalDirs = 0;
    int permErrors = 0;
    bool cancelled = false;

    S3ProgressBegin(parent);
    S3ProgressUpdateStatus("Calculating folder sizes...");

    while (true)
    {
        if (S3ProgressCheckCancel())
        {
            cancelled = true;
            break;
        }

        f = focused ? SalamanderGeneral->GetPanelFocusedItem(panel, &isDir)
                    : SalamanderGeneral->GetPanelSelectedItem(panel, &index, &isDir);
        if (f == NULL) break;

        if (strcmp(f->Name, "..") != 0)
        {
            if (isDir)
            {
                totalDirs++;
                uint64_t dirBytes = 0;
                int dirFiles = 0;
                std::string err;
                bool ok = false;

                char status[256];
                _snprintf_s(status, _TRUNCATE, "Calculating \"%s\"...", f->Name);
                S3ProgressUpdateStatus(status);

                auto progCb = [&](uint64_t cbBytes, int cbFiles) -> bool {
                    char cbStatus[256];
                    _snprintf_s(cbStatus, _TRUNCATE, "Scanning \"%s\": %d files (%.2f MB)...",
                                f->Name, cbFiles, cbBytes / 1048576.0);
                    return S3ProgressUpdateStatus(cbStatus);
                };

                if (bucket.empty())
                {
                    // At root: f->Name is a bucket name
                    ok = S3Conn.CalculatePrefixSize(f->Name, "", dirBytes, dirFiles, err, progCb);
                }
                else
                {
                    // In a bucket: subfolder
                    std::string subPrefix = prefix;
                    if (!subPrefix.empty() && subPrefix.back() != '/') subPrefix += "/";
                    subPrefix += f->Name;
                    subPrefix += "/";
                    ok = S3Conn.CalculatePrefixSize(bucket, subPrefix, dirBytes, dirFiles, err, progCb);
                }

                if (ok)
                {
                    CFileData* nonConstF = const_cast<CFileData*>(f);
                    nonConstF->Size.SetUI64(dirBytes);
                    nonConstF->SizeValid = 1;
                    nonConstF->Dirty = 1;
                    totalBytes += dirBytes;
                    totalFiles += dirFiles;
                }
                else
                {
                    if (S3ProgressCheckCancel())
                    {
                        cancelled = true;
                        break;
                    }
                    permErrors++;
                }
            }
            else
            {
                totalFiles++;
                totalBytes += f->Size.Value;
            }
        }

        if (focused) break;
    }

    S3ProgressEnd();

    // Repaint panel items so new sizes show up instead of DIR / ADR
    HWND hFocus = GetFocus();
    if (hFocus != NULL)
    {
        InvalidateRect(hFocus, NULL, TRUE);
        UpdateWindow(hFocus);
    }

    if (cancelled)
    {
        char msg[512];
        _snprintf_s(msg, _TRUNCATE,
                    "Calculation cancelled by user.\n\nPartial calculated size: %I64u bytes (%.2f MB)\nFiles counted: %d\nFolders: %d",
                    totalBytes, totalBytes / 1048576.0, totalFiles, totalDirs);
        SalamanderGeneral->SalMessageBox(parent, msg, LoadStr(IDS_PLUGINNAME), MB_OK | MB_ICONINFORMATION);
        return;
    }

    char msg[512];
    if (permErrors > 0)
    {
        _snprintf_s(msg, _TRUNCATE,
                    "Size: %I64u bytes (%.2f MB)\nFiles: %d\nFolders: %d\n\nNote: %d folder(s) could not be read (Access Denied / Permission error).",
                    totalBytes, totalBytes / 1048576.0, totalFiles, totalDirs, permErrors);
    }
    else
    {
        _snprintf_s(msg, _TRUNCATE,
                    "Size: %I64u bytes (%.2f MB)\nFiles: %d\nFolders: %d",
                    totalBytes, totalBytes / 1048576.0, totalFiles, totalDirs);
    }
    SalamanderGeneral->SalMessageBox(parent, msg, LoadStr(IDS_PLUGINNAME), MB_OK | MB_ICONINFORMATION);
}

void S3SetProgressCallback(S3GlueProgressFn cb, void* ctx)
{
    g_S3ProgressCb = cb;
    g_S3ProgressCtx = ctx;
}

static bool S3InternalProgress(uint64_t currentBytes, uint64_t totalBytes, void* userData)
{
    const char* filename = (const char*)userData;
    if (g_S3ProgressCb)
    {
        return g_S3ProgressCb(g_S3ProgressCtx, filename ? filename : "", currentBytes, totalBytes);
    }
    return true;
}

bool S3DownloadSingleFile(const char* s3Path, const char* localFilePath, std::string& outError, int64_t expectedSize)
{
    std::string bucket, prefix;
    if (!S3ParsePath(s3Path, bucket, prefix) || bucket.empty())
    {
        outError = "Invalid S3 file path.";
        return false;
    }
    std::wstring localW = AnsiToWide(localFilePath);
    const char* fileName = strrchr(localFilePath, '\\');
    const char* fileName2 = strrchr(localFilePath, '/');
    if (fileName2 && (!fileName || fileName2 > fileName)) fileName = fileName2;
    fileName = fileName ? fileName + 1 : localFilePath;
    return S3Conn.DownloadFile(bucket, prefix, localW.c_str(), 0, S3InternalProgress, (void*)fileName, outError, expectedSize);
}

bool S3UploadSingleFile(const char* localFilePath, const char* s3Path, std::string& outError)
{
    std::string bucket, prefix;
    if (!S3ParsePath(s3Path, bucket, prefix) || bucket.empty())
    {
        outError = "Invalid target S3 path.";
        return false;
    }
    std::wstring localW = AnsiToWide(localFilePath);
    const char* fileName = strrchr(localFilePath, '\\');
    const char* fileName2 = strrchr(localFilePath, '/');
    if (fileName2 && (!fileName || fileName2 > fileName)) fileName = fileName2;
    fileName = fileName ? fileName + 1 : localFilePath;
    return S3Conn.UploadFile(bucket, prefix, localW.c_str(), S3InternalProgress, (void*)fileName, outError);
}

static void S3ReportFail(S3TransferReport* report, const std::string& item, const std::string& err)
{
    if (report)
        report->FailedItems.push_back(item + ": " + err);
}

static bool S3IsCancelError(const std::string& err)
{
    return err.find("canceled") != std::string::npos;
}

// Per-operation state of downloads: local files written so far (case-insensitive) and the
// remembered "overwrite all" / "skip all" answer.
static S3WrittenSet g_S3Written;
static int g_S3OverwriteAnswer = 0; // 0 = ask, DIALOG_ALL = overwrite all, DIALOG_SKIPALL = skip all

void S3TransferOpReset(HWND parent)
{
    g_S3Written.Clear();
    (void)parent;
    g_S3OverwriteAnswer = 0;
}

// Decides whether localFile may be written. Fails the item on a case-insensitive name clash with a
// file written earlier in this operation, or when the user declines to overwrite an existing file.
static bool S3ClaimDownloadTarget(const char* localFile, const char* s3Path, int64_t expectedSize, std::string& outError)
{
    std::wstring w = AnsiToWide(localFile);
    if (!g_S3Written.Claim(w))
    {
        outError = std::string("Name clash: \"") + localFile + "\" differs only by letter case from a file already "
                   "written by this operation. The object was not downloaded and stays on S3.";
        return false;
    }
    if (GetFileAttributesA(localFile) == INVALID_FILE_ATTRIBUTES)
        return true;
    if (g_S3OverwriteAnswer == 0)
    {
        // Standard Salamander overwrite dialog (Yes / All / Skip / Skip all / Cancel). The owner is the
        // progress dialog: the main window is disabled while it is open.
        WIN32_FILE_ATTRIBUTE_DATA ex;
        char localData[64] = "";
        if (GetFileAttributesExA(localFile, GetFileExInfoStandard, &ex))
        {
            unsigned long long sz = ((unsigned long long)ex.nFileSizeHigh << 32) | ex.nFileSizeLow;
            snprintf(localData, sizeof(localData), "%llu bytes", sz);
        }
        char remoteData[64] = "";
        if (expectedSize >= 0)
            snprintf(remoteData, sizeof(remoteData), "%llu bytes", (unsigned long long)expectedSize);
        int a = SalamanderGeneral->DialogOverwrite(S3ProgressWindow(), BUTTONS_YESALLSKIPCANCEL,
                                                   localFile, localData, s3Path, remoteData);
        if (a == DIALOG_CANCEL || a == DIALOG_FAIL)
        {
            outError = "Download canceled by user.";
            return false;
        }
        if (a == DIALOG_YES)
            return true; // this file only
        if (a == DIALOG_ALL || a == DIALOG_SKIPALL)
            g_S3OverwriteAnswer = a;
        // DIALOG_SKIP: skip just this file (falls through to the skip below)
        if (a == DIALOG_SKIP)
            g_S3OverwriteAnswer = DIALOG_SKIP;
    }
    if (g_S3OverwriteAnswer == DIALOG_ALL)
        return true;
    if (g_S3OverwriteAnswer == DIALOG_SKIP)
        g_S3OverwriteAnswer = 0; // one-off skip, ask again next time
    outError = "Skipped, the file already exists locally.";
    return false;
}

static bool S3DownloadOneFile(const std::string& s3Path, const char* localFile, int64_t expectedSize,
                              std::string& outError, S3TransferReport* report)
{
    bool ok = S3ClaimDownloadTarget(localFile, s3Path.c_str(), expectedSize, outError) &&
              S3DownloadSingleFile(s3Path.c_str(), localFile, outError, expectedSize);
    if (ok)
    {
        if (report) report->DoneItems.push_back(s3Path);
        return true;
    }
    if (S3IsCancelError(outError) && report) report->Canceled = true;
    S3ReportFail(report, s3Path, outError);
    return false;
}

bool S3DownloadRecursive(const char* s3Path, const char* localTargetDir, bool isDir, std::string& outError,
                         S3TransferReport* report, int64_t expectedSize, int depth)
{
    if (!isDir)
    {
        const char* slash = strrchr(s3Path, '/');
        const char* name = slash ? slash + 1 : s3Path;
        if (!S3IsSafeLocalComponent(name, s3Path, outError))
        {
            S3ReportFail(report, s3Path, outError);
            return false;
        }
        char localTargetFile[MAX_PATH * 2];
        snprintf(localTargetFile, sizeof(localTargetFile), "%s\\%s", localTargetDir, name);
        return S3DownloadOneFile(s3Path, localTargetFile, expectedSize, outError, report);
    }

    if (depth >= S3_MAX_RECURSION_DEPTH)
    {
        outError = "Skipped, folders are nested deeper than 64 levels.";
        S3ReportFail(report, s3Path, outError);
        return false;
    }

    std::string bucket, prefix;
    S3ParsePath(s3Path, bucket, prefix);
    if (bucket.empty())
    {
        outError = "Cannot download root container directly.";
        return false;
    }

    std::string dirName = prefix;
    while (!dirName.empty() && dirName.back() == '/') dirName.pop_back();
    size_t lastSlash = dirName.rfind('/');
    if (lastSlash != std::string::npos) dirName = dirName.substr(lastSlash + 1);
    if (dirName.empty()) dirName = bucket;
    if (!S3IsSafeLocalComponent(dirName, s3Path, outError))
    {
        S3ReportFail(report, s3Path, outError);
        return false;
    }

    char subLocalDir[MAX_PATH * 2];
    snprintf(subLocalDir, sizeof(subLocalDir), "%s\\%s", localTargetDir, dirName.c_str());
    CreateDirectoryA(subLocalDir, NULL);

    std::vector<S3ObjectInfo> items;
    if (!S3Conn.ListDirectory(bucket, prefix, items, outError))
    {
        S3ReportFail(report, s3Path, "listing failed: " + outError);
        return false;
    }

    // Keep going after a failed child so that everything that can be downloaded is, but
    // remember every failure: the caller must not delete the source unless all succeeded.
    bool allOk = true;
    std::string firstError;
    for (const auto& item : items)
    {
        char itemS3Path[MAX_PATH * 2];
        snprintf(itemS3Path, sizeof(itemS3Path), "/%s/%s", bucket.c_str(), item.Key.c_str());
        std::string childErr;
        bool ok;
        if (!S3IsDownloadableKey(item.Key))
        {
            // Empty name / "//" in the key: the path would be normalized to a different (or the same)
            // prefix, which could re-list it forever. Refuse; the object stays on S3.
            ok = false;
            childErr = "Skipped, the key \"" + item.Key + "\" has an empty path component.";
            S3ReportFail(report, itemS3Path, childErr);
        }
        else if (item.IsDirectory)
        {
            ok = S3DownloadRecursive(itemS3Path, subLocalDir, true, childErr, report, -1, depth + 1);
        }
        else if (!S3IsSafeLocalComponent(item.DisplayName, item.Key, childErr))
        {
            ok = false;
            S3ReportFail(report, itemS3Path, childErr);
        }
        else
        {
            char targetLocalFile[MAX_PATH * 2];
            snprintf(targetLocalFile, sizeof(targetLocalFile), "%s\\%s", subLocalDir, item.DisplayName.c_str());
            ok = S3DownloadOneFile(itemS3Path, targetLocalFile, (int64_t)item.Size, childErr, report);
        }
        if (!ok)
        {
            allOk = false;
            if (firstError.empty()) firstError = childErr;
            if (S3IsCancelError(childErr))
                break; // user canceled: stop everything
        }
    }

    // Folder traversed (children first): on Move the folder is removed only if it is empty by then.
    if (report) report->DoneDirs.push_back(s3Path);

    if (!allOk)
    {
        outError = firstError;
        return false;
    }
    return true;
}

static bool S3UploadRecursiveImpl(const char* localPath, const char* s3TargetDir, bool isDir, std::string& outError,
                                  S3TransferReport* report, int depth);

bool S3UploadRecursive(const char* localPath, const char* s3TargetDir, bool isDir, std::string& outError,
                       S3TransferReport* report)
{
    return S3UploadRecursiveImpl(localPath, s3TargetDir, isDir, outError, report, 0);
}

static bool S3UploadRecursiveImpl(const char* localPath, const char* s3TargetDir, bool isDir, std::string& outError,
                                  S3TransferReport* report, int depth)
{
    const char* bs = strrchr(localPath, '\\');
    const char* slash = strrchr(localPath, '/');
    const char* base = (bs && (!slash || bs > slash)) ? bs : slash;
    const char* name = base ? base + 1 : localPath;

    char remoteDest[MAX_PATH * 2];
    S3Join(s3TargetDir, name, remoteDest, sizeof(remoteDest));

    if (!isDir)
    {
        if (S3UploadSingleFile(localPath, remoteDest, outError))
        {
            if (report) report->DoneItems.push_back(localPath);
            return true;
        }
        if (report)
        {
            if (S3IsCancelError(outError)) report->Canceled = true;
            report->FailedItems.push_back(std::string(localPath) + ": " + outError);
        }
        return false;
    }

    // Junctions / directory symlinks are leaves: never followed, never reported as transferred,
    // so they (and every parent folder) stay on disk after a Move.
    if (S3IsLinkPath(localPath))
    {
        outError = "Skipped, folder is a junction or symbolic link (not followed).";
        S3ReportFail(report, localPath, outError);
        return false;
    }
    if (depth >= S3_MAX_RECURSION_DEPTH)
    {
        outError = "Skipped, folders are nested deeper than 64 levels.";
        S3ReportFail(report, localPath, outError);
        return false;
    }

    std::string bucket, prefix;
    S3ParsePath(remoteDest, bucket, prefix);
    if (bucket.empty())
    {
        outError = "Target bucket is not specified.";
        return false;
    }

    if (!prefix.empty())
    {
        std::string folderKey = prefix;
        if (folderKey.back() != '/') folderKey += "/";
        std::string folderErr;
        if (!S3Conn.CreateFolder(bucket, folderKey, folderErr))
        {
            outError = folderErr;
            S3ReportFail(report, localPath, "cannot create folder: " + folderErr);
            return false;
        }
    }

    char searchPattern[MAX_PATH * 2];
    snprintf(searchPattern, sizeof(searchPattern), "%s\\*.*", localPath);

    WIN32_FIND_DATAA fd;
    HANDLE hFind = FindFirstFileA(searchPattern, &fd);
    if (hFind == INVALID_HANDLE_VALUE)
    {
        DWORD le = GetLastError();
        if (le == ERROR_FILE_NOT_FOUND || le == ERROR_NO_MORE_FILES) return true; // empty folder
        outError = "Cannot list local folder (error " + std::to_string(le) + ").";
        S3ReportFail(report, localPath, outError);
        return false;
    }

    // Continue after a failed child, but remember it: the caller must not delete the
    // local source of a Move unless everything was uploaded.
    bool allOk = true;
    std::string firstError;
    do
    {
        if (strcmp(fd.cFileName, ".") == 0 || strcmp(fd.cFileName, "..") == 0) continue;

        char subLocal[MAX_PATH * 2];
        snprintf(subLocal, sizeof(subLocal), "%s\\%s", localPath, fd.cFileName);
        bool subIsDir = (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0;

        std::string childErr;
        if (!S3UploadRecursiveImpl(subLocal, remoteDest, subIsDir, childErr, report, depth + 1))
        {
            allOk = false;
            if (firstError.empty()) firstError = childErr;
            if (S3IsCancelError(childErr)) break;
        }
    } while (FindNextFileA(hFind, &fd));

    FindClose(hFind);
    if (report) report->DoneDirs.push_back(localPath);
    if (!allOk)
    {
        outError = firstError;
        return false;
    }
    return true;
}

// Deletes the S3 folder (marker, or the bucket for a bucket root) only if nothing is left in it.
bool S3DeleteFolderIfEmpty(const char* s3Path, std::string& outError)
{
    std::string bucket, prefix;
    if (!S3ParsePath(s3Path, bucket, prefix) || bucket.empty())
    {
        outError = "Invalid folder path.";
        return false;
    }
    std::vector<S3ObjectInfo> items;
    if (!S3Conn.ListDirectory(bucket, prefix, items, outError))
        return false;
    if (!items.empty())
    {
        outError = "Folder is not empty.";
        return false;
    }
    if (prefix.empty())
        return S3Conn.DeleteBucket(bucket, outError);
    std::string folderKey = prefix;
    if (folderKey.back() != '/') folderKey += "/";
    return S3Conn.DeleteObject(bucket, folderKey, outError);
}

bool S3DeleteRecursive(const char* s3Path, bool isDir, std::string& outError)
{
    std::string bucket, prefix;
    if (!S3ParsePath(s3Path, bucket, prefix) || bucket.empty())
    {
        outError = "Cannot delete root.";
        return false;
    }

    if (!isDir)
    {
        return S3Conn.DeleteObject(bucket, prefix, outError);
    }

    if (prefix.empty())
    {
        return S3Conn.DeleteBucket(bucket, outError);
    }

    // A failed listing must not look like "folder is empty": stop and report it.
    std::vector<S3ObjectInfo> items;
    std::string listErr;
    if (!S3Conn.ListDirectory(bucket, prefix, items, listErr))
    {
        outError = "Cannot list \"" + prefix + "\": " + listErr;
        return false;
    }

    // Stop at the first failing child; the folder marker is removed only when all children are gone.
    for (const auto& item : items)
    {
        char itemS3Path[MAX_PATH * 2];
        snprintf(itemS3Path, sizeof(itemS3Path), "/%s/%s", bucket.c_str(), item.Key.c_str());
        std::string childErr;
        if (!S3DeleteRecursive(itemS3Path, item.IsDirectory, childErr))
        {
            outError = "Cannot delete \"" + item.Key + "\": " + childErr;
            return false;
        }
    }

    // Only the folder marker ("prefix/") belongs to this folder. A plain object named "prefix"
    // is a different object and must not be touched.
    std::string folderKey = prefix;
    if (folderKey.back() != '/') folderKey += "/";
    std::string markerErr;
    if (!S3Conn.DeleteObject(bucket, folderKey, markerErr))
    {
        outError = "Cannot delete folder marker \"" + folderKey + "\": " + markerErr;
        return false;
    }
    return true;
}

