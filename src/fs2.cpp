// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (c) 2026 Open Salamander Authors & Dupl3xx
// Amazon S3 FS Operations

#include "precomp.h"
#include "s3glue.h"

extern bool ShowConnectDialog(HWND parent);
extern HIMAGELIST DFSImageList;

//
// ****************************************************************************
// Transfer Progress Dialog ("pumpa")
//

class CDeleteProgressDlg : public CDialog
{
protected:
    CGUIProgressBarAbstract* ProgressBar;
    BOOL WantCancel;
    DWORD LastTickCount;
    char TextCache[MAX_PATH];
    BOOL TextCacheIsDirty;
    DWORD ProgressCache;
    BOOL ProgressCacheIsDirty;

public:
    CDeleteProgressDlg(HWND parent, CObjectOrigin origin = ooStandard)
        : CDialog(HLanguage, IDD_PROGRESSDLG, parent, origin)
    {
        ProgressBar = NULL;
        WantCancel = FALSE;
        LastTickCount = 0;
        TextCache[0] = 0;
        TextCacheIsDirty = FALSE;
        ProgressCache = 0;
        ProgressCacheIsDirty = FALSE;
    }

    virtual ~CDeleteProgressDlg() {}

    void Set(const char* fileName, DWORD progress, BOOL delayedPaint)
    {
        lstrcpynA(TextCache, fileName != NULL ? fileName : "", MAX_PATH);
        TextCacheIsDirty = TRUE;

        if (progress != ProgressCache)
        {
            ProgressCache = progress;
            ProgressCacheIsDirty = TRUE;
        }

        if (!delayedPaint)
            FlushDataToControls();
    }

    void EnableCancel(BOOL enable)
    {
        if (HWindow != NULL)
        {
            HWND cancel = GetDlgItem(HWindow, IDCANCEL);
            if (IsWindowEnabled(cancel) != enable)
            {
                EnableWindow(cancel, enable);
                if (enable)
                    SetFocus(cancel);
                PostMessage(cancel, BM_SETSTYLE, enable ? BS_DEFPUSHBUTTON : BS_PUSHBUTTON, TRUE);

                MSG msg;
                while (PeekMessage(&msg, NULL, 0, 0, PM_REMOVE))
                {
                    if (!IsWindow(HWindow) || !IsDialogMessage(HWindow, &msg))
                    {
                        TranslateMessage(&msg);
                        DispatchMessage(&msg);
                    }
                }
            }
        }
    }

    BOOL GetWantCancel()
    {
        MSG msg;
        while (PeekMessage(&msg, NULL, 0, 0, TRUE))
        {
            if (!IsWindow(HWindow) || !IsDialogMessage(HWindow, &msg))
            {
                TranslateMessage(&msg);
                DispatchMessage(&msg);
            }
        }

        DWORD ticks = GetTickCount();
        if (ticks - LastTickCount > 100)
        {
            LastTickCount = ticks;
            FlushDataToControls();
        }

        return WantCancel;
    }

    void FlushDataToControls()
    {
        if (HWindow != NULL)
        {
            if (TextCacheIsDirty)
            {
                SetDlgItemTextA(HWindow, IDT_FILENAME, TextCache);
                TextCacheIsDirty = FALSE;
            }

            if (ProgressCacheIsDirty)
            {
                if (ProgressBar != NULL)
                    ProgressBar->SetProgress(ProgressCache, NULL);
                ProgressCacheIsDirty = FALSE;
            }
        }
    }

protected:
    virtual INT_PTR DialogProc(UINT uMsg, WPARAM wParam, LPARAM lParam)
    {
        switch (uMsg)
        {
        case WM_INITDIALOG:
        {
            PluginDarkMode_ApplyTitleBar(HWindow);
            PluginDarkMode_ApplyListTreeThemeRecursive(HWindow);
            ProgressBar = SalamanderGUI->AttachProgressBar(HWindow, IDP_PROGRESSBAR);
            if (ProgressBar == NULL)
            {
                DestroyWindow(HWindow);
                return FALSE;
            }
            break;
        }
        case WM_THEMECHANGED:
        case WM_SETTINGCHANGE:
        {
            PluginDarkMode_HandleThemeMessage(HWindow, uMsg, lParam);
            return TRUE;
        }
        case WM_CTLCOLORDLG:
        case WM_CTLCOLORSTATIC:
        case WM_CTLCOLORBTN:
        case WM_CTLCOLOREDIT:
        case WM_CTLCOLORLISTBOX:
        {
            LRESULT brush = 0;
            if (PluginDarkMode_HandleCtlColor(uMsg, wParam, lParam, &brush))
                return (INT_PTR)brush;
            break;
        }
        case WM_COMMAND:
        {
            if (LOWORD(wParam) == IDCANCEL)
            {
                if (!WantCancel)
                {
                    FlushDataToControls();
                    if (SalamanderGeneral->SalMessageBox(HWindow, LoadStr(IDS_CONFIRM_CANCEL_OP), LoadStr(IDS_PLUGINNAME),
                                                         MB_YESNO | MB_ICONQUESTION) == IDYES)
                    {
                        WantCancel = TRUE;
                        EnableCancel(FALSE);
                    }
                }
                return TRUE;
            }
            break;
        }
        }
        return CDialog::DialogProc(uMsg, wParam, lParam);
    }
};

static CDeleteProgressDlg* g_ProgDlg = NULL;
static HWND g_ProgMainWnd = NULL;
static char g_ProgFile[MAX_PATH] = "";
static DWORD g_ProgStartTick = 0;

static bool S3GlueTransferProgress(void* ctx, const char* name, uint64_t done, uint64_t total)
{
    if (g_ProgDlg == NULL)
        return true;

    if (strcmp(g_ProgFile, name) != 0)
    {
        lstrcpynA(g_ProgFile, name, MAX_PATH);
        g_ProgStartTick = GetTickCount();
    }
    DWORD elapsed = GetTickCount() - g_ProgStartTick;
    double speedMB = elapsed > 0 ? ((double)done * 1000.0 / elapsed) / 1048576.0 : 0.0;
    const char* base = strrchr(name, '\\');
    const char* base2 = strrchr(name, '/');
    if (base2 > base)
        base = base2;
    base = (base != NULL) ? base + 1 : name;

    char txt[2 * MAX_PATH];
    if (total > 0)
        snprintf(txt, sizeof(txt), "%s   %llu / %llu kB   (%.2f MB/s)", base, (unsigned long long)(done / 1024), (unsigned long long)(total / 1024), speedMB);
    else
        snprintf(txt, sizeof(txt), "%s   %llu kB   (%.2f MB/s)", base, (unsigned long long)(done / 1024), speedMB);

    DWORD prog = total > 0 ? (DWORD)(done * 1000 / total) : 0;
    g_ProgDlg->Set(txt, prog, TRUE);
    return !g_ProgDlg->GetWantCancel();
}

HWND S3ProgressWindow()
{
    if (g_ProgDlg != NULL && g_ProgDlg->HWindow != NULL)
        return g_ProgDlg->HWindow;
    return SalamanderGeneral->GetMsgBoxParent();
}

void S3ProgressBegin(HWND parent)
{
    S3TransferOpReset(parent);
    g_ProgMainWnd = parent;
    HWND pw;
    while ((pw = GetParent(g_ProgMainWnd)) != NULL && IsWindowEnabled(pw))
        g_ProgMainWnd = pw;
    EnableWindow(g_ProgMainWnd, FALSE);
    g_ProgFile[0] = 0;
    g_ProgStartTick = GetTickCount();

    g_ProgDlg = new CDeleteProgressDlg(g_ProgMainWnd, ooStatic);
    if (g_ProgDlg != NULL && g_ProgDlg->Create() != NULL)
    {
        SetForegroundWindow(g_ProgDlg->HWindow);
        g_ProgDlg->Set("Connecting...", 0, FALSE);
        S3SetProgressCallback(S3GlueTransferProgress, NULL);
    }
    else
    {
        if (g_ProgDlg != NULL)
            delete g_ProgDlg;
        g_ProgDlg = NULL;
        EnableWindow(g_ProgMainWnd, TRUE);
    }
}

void S3ProgressEnd()
{
    S3SetProgressCallback(NULL, NULL);
    if (g_ProgDlg != NULL)
    {
        EnableWindow(g_ProgMainWnd, TRUE);
        DestroyWindow(g_ProgDlg->HWindow);
        delete g_ProgDlg;
        g_ProgDlg = NULL;
    }
}

bool S3ProgressUpdateStatus(const char* text)
{
    if (g_ProgDlg != NULL)
    {
        g_ProgDlg->Set(text, 0, FALSE);
        return !g_ProgDlg->GetWantCancel();
    }
    return true;
}

bool S3ProgressCheckCancel()
{
    if (g_ProgDlg != NULL)
    {
        return g_ProgDlg->GetWantCancel() != FALSE;
    }
    return false;
}

CPluginFSInterface::CPluginFSInterface()
{
    Path[0] = '/';
    Path[1] = 0;
    PathError = FALSE;
    FatalError = FALSE;
    CalledFromDisconnectDialog = FALSE;
}

BOOL WINAPI CPluginFSInterface::GetCurrentPath(char* userPart)
{
    lstrcpynA(userPart, Path, MAX_PATH);
    return TRUE;
}

BOOL WINAPI CPluginFSInterface::GetFullName(CFileData& file, int isDir, char* buf, int bufSize)
{
    if (isDir == 2) // up-dir ".."
    {
        S3Parent(Path, buf, bufSize);
    }
    else
    {
        S3Join(Path, file.Name, buf, bufSize);
    }
    return TRUE;
}

BOOL WINAPI CPluginFSInterface::GetFullFSPath(HWND parent, const char* fsName, char* path, int pathSize, BOOL& success)
{
    char full[MAX_PATH];
    if (path[0] == '/')
    {
        lstrcpynA(full, path, MAX_PATH);
    }
    else
    {
        S3Join(Path[0] != 0 ? Path : "/", path, full, MAX_PATH);
    }
    S3Normalize(full);
    success = (int)(strlen(full) + strlen(fsName) + 2) < pathSize;
    if (success)
    {
        snprintf(path, pathSize, "%s:%s", fsName, full);
    }
    return TRUE;
}

BOOL WINAPI CPluginFSInterface::GetRootPath(char* userPart)
{
    strcpy(userPart, "/");
    return TRUE;
}

BOOL WINAPI CPluginFSInterface::IsCurrentPath(int currentFSNameIndex, int fsNameIndex, const char* userPart)
{
    if (currentFSNameIndex != fsNameIndex) return FALSE;
    char normalized[MAX_PATH];
    lstrcpynA(normalized, userPart ? userPart : "/", MAX_PATH);
    S3Normalize(normalized);
    return (strcmp(Path, normalized) == 0);
}

BOOL WINAPI CPluginFSInterface::IsOurPath(int currentFSNameIndex, int fsNameIndex, const char* userPart)
{
    return TRUE;
}

BOOL WINAPI CPluginFSInterface::ChangePath(int currentFSNameIndex, char* fsName, int fsNameIndex,
                                           const char* userPart, char* cutFileName, BOOL* pathWasCut,
                                           BOOL forceRefresh, int mode)
{
    if (mode != 3 && (pathWasCut != NULL || cutFileName != NULL))
    {
        mode = 3;
    }
    if (pathWasCut != NULL) *pathWasCut = FALSE;
    if (cutFileName != NULL) *cutFileName = '\0';

    if (fsName != NULL)
    {
        lstrcpynA(fsName, AssignedFSName, MAX_PATH);
    }

    if (FatalError)
    {
        FatalError = FALSE;
        return FALSE;
    }

    BOOL wasPathError = PathError;
    PathError = FALSE;

    if (!S3Conn.IsConnected())
    {
        if (!ShowConnectDialog(SalamanderGeneral->GetMsgBoxParent()))
        {
            return FALSE;
        }
    }

    char target[MAX_PATH];
    if (userPart == NULL || userPart[0] == 0)
    {
        if (!S3ActiveProfile.InitialBucket.empty())
        {
            if (S3ActiveProfile.InitialBucket[0] == '/')
                lstrcpynA(target, S3ActiveProfile.InitialBucket.c_str(), MAX_PATH);
            else
                snprintf(target, sizeof(target), "/%s", S3ActiveProfile.InitialBucket.c_str());

            if (!S3ActiveProfile.InitialPath.empty() && S3ActiveProfile.InitialPath != "/")
            {
                S3Join(target, S3ActiveProfile.InitialPath.c_str(), target, MAX_PATH);
            }
        }
        else if (!S3ActiveProfile.InitialPath.empty())
        {
            lstrcpynA(target, S3ActiveProfile.InitialPath.c_str(), MAX_PATH);
            S3Normalize(target);
        }
        else
        {
            strcpy(target, "/");
        }
    }
    else
    {
        if (userPart[0] != '/' && userPart[0] != '\\')
        {
            S3Join(Path[0] != 0 ? Path : "/", userPart, target, MAX_PATH);
        }
        else
        {
            lstrcpynA(target, userPart, MAX_PATH);
        }
    }
    S3Normalize(target);

    if (wasPathError)
    {
        if (strcmp(target, "/") == 0 || target[0] == 0)
        {
            return FALSE;
        }

        char temp[MAX_PATH];
        lstrcpynA(temp, target, MAX_PATH);
        size_t len = strlen(temp);
        if (len > 1 && temp[len - 1] == '/')
            temp[len - 1] = 0;
        const char* lastSlash = strrchr(temp, '/');
        if (cutFileName != NULL && lastSlash != NULL)
        {
            lstrcpynA(cutFileName, lastSlash + 1, MAX_PATH);
        }

        char parentPath[MAX_PATH];
        S3Parent(temp, parentPath, MAX_PATH);
        lstrcpynA(target, parentPath, MAX_PATH);
        S3Normalize(target);

        if (pathWasCut != NULL)
            *pathWasCut = TRUE;
    }

    lstrcpynA(Path, target, MAX_PATH);

    return TRUE;
}

BOOL WINAPI CPluginFSInterface::ListCurrentPath(CSalamanderDirectoryAbstract* dir,
                                               CPluginDataInterfaceAbstract*& pluginData,
                                               int& iconsType, BOOL forceRefresh)
{
    HWND parent = SalamanderGeneral->GetMsgBoxParent();
    if (!S3EnsureConnected(parent))
    {
        PathError = TRUE;
        return FALSE;
    }

    pluginData = new CPluginFSDataInterface(Path);
    if (pluginData == NULL)
    {
        FatalError = TRUE;
        return FALSE;
    }
    iconsType = pitSimple;

    dir->SetValidData(VALID_DATA_EXTENSION | VALID_DATA_SIZE | VALID_DATA_TYPE |
                      VALID_DATA_DATE | VALID_DATA_TIME | VALID_DATA_ATTRIBUTES |
                      VALID_DATA_HIDDEN);

    // up-dir ".." if not at root
    if (strcmp(Path, "/") != 0)
    {
        CFileData up = {};
        up.Name = SalamanderGeneral->DupStr("..");
        up.NameLen = 2;
        up.Ext = up.Name + up.NameLen;
        up.Size = CQuadWord(0, 0);
        up.Attr = FILE_ATTRIBUTE_DIRECTORY;
        up.DosName = NULL;
        up.PluginData = (DWORD_PTR)new CFSData("", "");
        up.IconOverlayIndex = ICONOVERLAYINDEX_NOTUSED;
        up.NameW = NULL;
        if (up.Name != NULL)
        {
            dir->AddDir(NULL, up, pluginData);
        }
    }

    std::string bucket, prefix;
    S3ParsePath(Path, bucket, prefix);

    std::string err;

    if (bucket.empty())
    {
        // Root level: list buckets
        std::vector<S3BucketInfo> buckets;
        if (!S3Conn.ListBuckets(buckets, err))
        {
            SalamanderGeneral->SalMessageBox(parent, err.c_str(), LoadStr(IDS_PLUGINNAME), MB_OK | MB_ICONERROR);
            PathError = TRUE;
            return FALSE;
        }

        for (size_t i = 0; i < buckets.size(); ++i)
        {
            const auto& b = buckets[i];
            CFileData fd = {};
            fd.Name = SalamanderGeneral->DupStr(b.Name.c_str());
            fd.NameLen = (int)b.Name.length();
            fd.Ext = fd.Name + fd.NameLen;
            fd.Attr = FILE_ATTRIBUTE_DIRECTORY;
            fd.Size = CQuadWord(0, 0);
            fd.LastWrite = b.CreationTime;
            fd.PluginData = (DWORD_PTR)new CFSData("BUCKET", "");
            fd.IconOverlayIndex = ICONOVERLAYINDEX_NOTUSED;
            fd.DosName = NULL;
            fd.NameW = NULL;

            dir->AddDir(NULL, fd, pluginData);
        }
    }
    else
    {
        // Inside a bucket: list prefixes and objects
        std::vector<S3ObjectInfo> items;
        if (!S3Conn.ListDirectory(bucket, prefix, items, err))
        {
            SalamanderGeneral->SalMessageBox(parent, err.c_str(), LoadStr(IDS_PLUGINNAME), MB_OK | MB_ICONERROR);
            PathError = TRUE;
            return FALSE;
        }

        for (const auto& item : items)
        {
            CFileData fd = {};
            fd.Name = SalamanderGeneral->DupStr(item.DisplayName.c_str());
            fd.NameLen = (int)item.DisplayName.length();
            if (item.IsDirectory)
            {
                fd.Ext = fd.Name + fd.NameLen;
                fd.Attr = FILE_ATTRIBUTE_DIRECTORY;
                fd.Size = CQuadWord(0, 0);
            }
            else
            {
                const char* dot = strrchr(fd.Name, '.');
                fd.Ext = dot ? (char*)dot + 1 : fd.Name + fd.NameLen;
                fd.Attr = FILE_ATTRIBUTE_ARCHIVE;
                fd.Size = CQuadWord((DWORD)(item.Size & 0xFFFFFFFF), (DWORD)(item.Size >> 32));
            }
            fd.LastWrite = item.LastWriteTime;
            fd.PluginData = (DWORD_PTR)new CFSData(item.StorageClass.c_str(), item.ETag.c_str());
            fd.IconOverlayIndex = ICONOVERLAYINDEX_NOTUSED;
            fd.DosName = NULL;
            fd.NameW = NULL;

            if (item.IsDirectory)
                dir->AddDir(NULL, fd, pluginData);
            else
                dir->AddFile(NULL, fd, pluginData);
        }
    }

    return TRUE;
}

BOOL WINAPI CPluginFSInterface::TryCloseOrDetach(BOOL forceClose, BOOL canDetach, BOOL& detach, int reason)
{
    detach = FALSE;
    return TRUE;
}

void WINAPI CPluginFSInterface::Event(int event, DWORD param)
{
}

void WINAPI CPluginFSInterface::ReleaseObject(HWND parent)
{
}

DWORD WINAPI CPluginFSInterface::GetSupportedServices()
{
    return FS_SERVICE_VIEWFILE | FS_SERVICE_DELETE | FS_SERVICE_CREATEDIR |
           FS_SERVICE_QUICKRENAME | FS_SERVICE_COPYFROMDISKTOFS | FS_SERVICE_COPYFROMFS;
}

BOOL WINAPI CPluginFSInterface::GetChangeDriveOrDisconnectItem(const char* fsName, char*& title, HICON& icon, BOOL& destroyIcon)
{
    destroyIcon = FALSE;
    icon = LoadIcon(DLLInstance, MAKEINTRESOURCE(IDI_FS));
    title = SalamanderGeneral->DupStr(LoadStr(IDS_PLUGINNAME));
    return TRUE;
}

HICON WINAPI CPluginFSInterface::GetFSIcon(BOOL& destroyIcon)
{
    destroyIcon = FALSE;
    return LoadIcon(DLLInstance, MAKEINTRESOURCE(IDI_FS));
}

void WINAPI CPluginFSInterface::GetDropEffect(const char* srcFSPath, const char* tgtFSPath,
                                              DWORD allowedEffects, DWORD keyState,
                                              DWORD* dropEffect)
{
    if (dropEffect) *dropEffect = DROPEFFECT_COPY;
}

void WINAPI CPluginFSInterface::GetFSFreeSpace(CQuadWord* retValue)
{
    if (retValue) *retValue = CQuadWord(0, 0);
}

BOOL WINAPI CPluginFSInterface::GetNextDirectoryLineHotPath(const char* text, int pathLen, int& offset)
{
    return FALSE;
}

void WINAPI CPluginFSInterface::ShowInfoDialog(const char* fsName, HWND parent)
{
    char msg[1024];
    snprintf(msg, sizeof(msg),
        "Amazon S3 Connection Info\n\n"
        "Active Profile: %s\n"
        "Access Key ID: %s\n"
        "Region: %s\n"
        "Endpoint: %s\n"
        "Current Path: %s\n"
        "SSL Enabled: %s\n"
        "Path Style: %s",
        S3ActiveProfile.AwsProfileName.empty() ? "(Custom)" : S3ActiveProfile.AwsProfileName.c_str(),
        S3ActiveProfile.AccessKeyId.c_str(),
        S3ActiveProfile.Region.c_str(),
        S3ActiveProfile.Endpoint.empty() ? "AWS Default" : S3ActiveProfile.Endpoint.c_str(),
        Path,
        S3ActiveProfile.UseSSL ? "Yes" : "No",
        S3ActiveProfile.PathStyle ? "Yes" : "No");

    SalamanderGeneral->SalMessageBox(parent, msg, LoadStr(IDS_TITLE_CONN_INFO), MB_OK | MB_ICONINFORMATION);
}

BOOL WINAPI CPluginFSInterface::ExecuteCommandLine(HWND parent, char* command, int& selFrom, int& selTo)
{
    return FALSE;
}

// True if the object/folder at key exists. Fails closed: any error other than "404" counts as
// "cannot verify" (outUnknown) so that the caller refuses instead of overwriting.
static bool S3KeyExists(const std::string& bucket, const std::string& key, bool isFolder, bool& outUnknown)
{
    outUnknown = false;
    std::string err;
    if (isFolder)
    {
        std::vector<S3ObjectInfo> items;
        if (!S3Conn.ListDirectory(bucket, key, items, err)) { outUnknown = true; return false; }
        if (!items.empty()) return true;
    }
    S3ObjectInfo info;
    DWORD httpStatus = 0;
    if (S3Conn.StatObject(bucket, key, info, err, &httpStatus))
        return true;
    if (httpStatus != 404)
        outUnknown = true;
    return false;
}

BOOL WINAPI CPluginFSInterface::QuickRename(const char* fsName, int mode, HWND parent, CFileData& file, BOOL isDir,
                                            char* newName, BOOL& cancel)
{
    cancel = FALSE;
    if (mode == 1)
        return FALSE; // Request Salamander to open standard in-place / quick rename edit

    if (!S3EnsureConnected(parent)) return FALSE;

    std::string bucket, prefix;
    S3ParsePath(Path, bucket, prefix);
    if (bucket.empty())
    {
        SalamanderGeneral->SalMessageBox(parent, LoadStr(IDS_WARN_RENAME_BUCKET), LoadStr(IDS_PLUGINNAME), MB_OK | MB_ICONWARNING);
        return FALSE;
    }

    if (!isDir)
    {
        std::string oldKey = prefix;
        if (!oldKey.empty() && oldKey.back() != '/') oldKey += "/";
        oldKey += file.Name;

        std::string newKey = prefix;
        if (!newKey.empty() && newKey.back() != '/') newKey += "/";
        newKey += newName;

        std::string err;
        if (newKey == oldKey || newKey.empty())
        {
            SalamanderGeneral->SalMessageBox(parent, "The new name is the same as the old one (or empty); nothing was renamed.",
                                             LoadStr(IDS_TITLE_RENAME_ERR), MB_OK | MB_ICONWARNING);
            return FALSE;
        }
        bool unknown = false;
        if (S3KeyExists(bucket, newKey, false, unknown) || unknown)
        {
            std::string msg = unknown ? "Cannot verify that the new name is free, nothing was renamed."
                                      : "An object named \"" + newKey + "\" already exists; nothing was renamed.";
            SalamanderGeneral->SalMessageBox(parent, msg.c_str(), LoadStr(IDS_TITLE_RENAME_ERR), MB_OK | MB_ICONERROR);
            return FALSE;
        }
        if (!S3Conn.CopyObject(bucket, oldKey, bucket, newKey, err))
        {
            SalamanderGeneral->SalMessageBox(parent, err.c_str(), LoadStr(IDS_TITLE_RENAME_ERR), MB_OK | MB_ICONERROR);
            return FALSE;
        }
        {
            // The copy must be visible before the source is deleted.
            S3ObjectInfo chk;
            std::string chkErr;
            if (!S3Conn.StatObject(bucket, newKey, chk, chkErr))
            {
                std::string msg = "The copy could not be verified, the old object was NOT deleted:\n" + chkErr;
                SalamanderGeneral->SalMessageBox(parent, msg.c_str(), LoadStr(IDS_TITLE_RENAME_ERR), MB_OK | MB_ICONERROR);
                SalamanderGeneral->PostRefreshPanelFS(this, FALSE);
                return FALSE;
            }
        }

        if (!S3Conn.DeleteObject(bucket, oldKey, err))
        {
            std::string msg = "The file was copied to the new name, but the old object \"" + oldKey +
                              "\" could not be deleted:\n" + err;
            SalamanderGeneral->SalMessageBox(parent, msg.c_str(), LoadStr(IDS_TITLE_RENAME_ERR), MB_OK | MB_ICONERROR);
            SalamanderGeneral->PostRefreshPanelFS(this, FALSE);
            return FALSE;
        }
    }
    else
    {
        std::string oldPrefix = prefix;
        if (!oldPrefix.empty() && oldPrefix.back() != '/') oldPrefix += "/";
        oldPrefix += file.Name;
        oldPrefix += "/";

        std::string newPrefix = prefix;
        if (!newPrefix.empty() && newPrefix.back() != '/') newPrefix += "/";
        newPrefix += newName;
        newPrefix += "/";

        // Never rename a folder onto itself or into itself (copy+delete would destroy the data).
        if (newPrefix.compare(0, oldPrefix.size(), oldPrefix) == 0 || newPrefix == "/" || newName[0] == 0)
        {
            SalamanderGeneral->SalMessageBox(parent, "The new folder name is the same as the old one, empty, or inside the old folder; nothing was renamed.",
                                             LoadStr(IDS_TITLE_RENAME_ERR), MB_OK | MB_ICONWARNING);
            return FALSE;
        }
        {
            bool unknown = false;
            if (S3KeyExists(bucket, newPrefix, true, unknown) || unknown)
            {
                std::string msg = unknown ? "Cannot verify that the new folder name is free, nothing was renamed."
                                          : "A folder named \"" + newPrefix + "\" already exists; nothing was renamed.";
                SalamanderGeneral->SalMessageBox(parent, msg.c_str(), LoadStr(IDS_TITLE_RENAME_ERR), MB_OK | MB_ICONERROR);
                return FALSE;
            }
        }

        std::vector<S3ObjectInfo> objects;
        std::string err;
        if (!S3Conn.ListRecursiveObjects(bucket, oldPrefix, objects, err))
        {
            // Without a complete listing nothing is copied or deleted.
            std::string msg = "Cannot list the folder, nothing was renamed:\n" + err;
            SalamanderGeneral->SalMessageBox(parent, msg.c_str(), LoadStr(IDS_TITLE_RENAME_ERR), MB_OK | MB_ICONERROR);
            return FALSE;
        }

        // Phase 1: copy everything; stop at the first failure and keep the old prefix untouched.
        size_t copiedOk = 0;
        std::string failMsg;
        for (const auto& obj : objects)
        {
            std::string targetKey;
            std::string cErr;
            if (!S3MapRenameKey(oldPrefix, newPrefix, obj.Key, targetKey))
            {
                failMsg = "Unexpected key \"" + obj.Key + "\" in listing.";
                break;
            }
            if (!S3Conn.CopyObject(bucket, obj.Key, bucket, targetKey, cErr))
            {
                failMsg = "Copy of \"" + obj.Key + "\" failed: " + cErr;
                break;
            }
            copiedOk++;
        }
        if (failMsg.empty() && !S3Conn.CreateFolder(bucket, newPrefix, err))
            failMsg = "Cannot create the new folder: " + err;

        if (!failMsg.empty() || !S3MayDeleteOldPrefix(objects.size(), copiedOk, true))
        {
            std::string msg = failMsg + "\n\nThe old folder was NOT deleted (" + std::to_string(copiedOk) + " of " +
                              std::to_string(objects.size()) + " objects were already copied to the new name).";
            SalamanderGeneral->SalMessageBox(parent, msg.c_str(), LoadStr(IDS_TITLE_RENAME_ERR), MB_OK | MB_ICONERROR);
            SalamanderGeneral->PostRefreshPanelFS(this, FALSE);
            return FALSE;
        }

        // Phase 2: every copy succeeded, now remove the old objects; report failures.
        std::string delFail;
        size_t delFailCount = 0;
        for (const auto& obj : objects)
        {
            std::string dErr;
            if (obj.Key == oldPrefix)
                continue; // the old folder marker is deleted below
            if (!S3Conn.DeleteObject(bucket, obj.Key, dErr))
            {
                if (delFailCount++ < 3) delFail += "\n\"" + obj.Key + "\": " + dErr;
            }
        }
        std::string mErr;
        if (delFailCount == 0 && !S3Conn.DeleteObject(bucket, oldPrefix, mErr))
        {
            delFail += "\n\"" + oldPrefix + "\": " + mErr;
            delFailCount++;
        }
        if (delFailCount > 0)
        {
            std::string msg = "The folder was copied to the new name, but " + std::to_string(delFailCount) +
                              " old object(s) could not be deleted:" + delFail;
            SalamanderGeneral->SalMessageBox(parent, msg.c_str(), LoadStr(IDS_TITLE_RENAME_ERR), MB_OK | MB_ICONERROR);
            SalamanderGeneral->PostRefreshPanelFS(this, FALSE);
            return FALSE;
        }
    }

    SalamanderGeneral->PostRefreshPanelFS(this, FALSE);
    return TRUE;
}

void WINAPI CPluginFSInterface::AcceptChangeOnPathNotification(const char* fsName, const char* path, BOOL includingSubdirs)
{
}

BOOL WINAPI CPluginFSInterface::CreateDir(const char* fsName, int mode, HWND parent, char* newName, BOOL& cancel)
{
    cancel = FALSE;
    if (mode == 1)
        return FALSE; // Request Salamander to open standard create directory dialog

    if (!S3EnsureConnected(parent)) return FALSE;

    std::string bucket, prefix;
    S3ParsePath(Path, bucket, prefix);

    std::string err;
    if (bucket.empty())
    {
        if (!S3Conn.CreateBucket(newName, S3ActiveProfile.Region, err))
        {
            SalamanderGeneral->SalMessageBox(parent, err.c_str(), LoadStr(IDS_TITLE_CREATE_BUCKET_ERR), MB_OK | MB_ICONERROR);
            return FALSE;
        }
    }
    else
    {
        std::string newPrefix = prefix;
        if (!newPrefix.empty() && newPrefix.back() != '/') newPrefix += "/";
        newPrefix += newName;
        newPrefix += "/";

        if (!S3Conn.CreateFolder(bucket, newPrefix, err))
        {
            SalamanderGeneral->SalMessageBox(parent, err.c_str(), LoadStr(IDS_TITLE_CREATE_FOLDER_ERR), MB_OK | MB_ICONERROR);
            return FALSE;
        }
    }

    SalamanderGeneral->PostRefreshPanelFS(this, FALSE);
    return TRUE;
}

void WINAPI CPluginFSInterface::ViewFile(const char* fsName, HWND parent,
                                        CSalamanderForViewFileOnFSAbstract* salamander,
                                        CFileData& file)
{
    {
        std::string nameErr;
        if (!S3IsSafeLocalComponent(file.Name, std::string(Path) + "/" + file.Name, nameErr))
        {
            SalamanderGeneral->SalMessageBox(parent, nameErr.c_str(), LoadStr(IDS_PLUGINNAME), MB_OK | MB_ICONEXCLAMATION);
            return;
        }
    }

    char uniqueFileName[2 * MAX_PATH];
    strcpy(uniqueFileName, fsName);
    strcat(uniqueFileName, ":");
    strcat(uniqueFileName, Path);
    SalamanderGeneral->SalPathAppend(uniqueFileName + strlen(fsName) + 1, file.Name, MAX_PATH);
    SalamanderGeneral->ToLowerCase(uniqueFileName);

    BOOL fileExists = FALSE;
    const char* tmpFileName = salamander->AllocFileNameInCache(parent, uniqueFileName, file.Name, NULL, fileExists);
    if (tmpFileName == NULL)
        return;

    BOOL newFileOK = FALSE;
    CQuadWord newFileSize(0, 0);
    if (!fileExists)
    {
        char remote[MAX_PATH];
        S3Join(Path, file.Name, remote, MAX_PATH);
        std::string err;
        S3ProgressBegin(parent);
        if (S3EnsureConnected(parent) && S3DownloadSingleFile(remote, tmpFileName, err))
        {
            newFileOK = TRUE;
            HANDLE hFile = CreateFileA(tmpFileName, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE,
                                      NULL, OPEN_EXISTING, 0, NULL);
            if (hFile != INVALID_HANDLE_VALUE)
            {
                DWORD errCode;
                SalamanderGeneral->SalGetFileSize(hFile, newFileSize, errCode);
                CloseHandle(hFile);
            }
        }
        else
        {
            char errorText[3 * MAX_PATH + 100];
            snprintf(errorText, sizeof(errorText), "Cannot download file %s.\n%s", file.Name, err.c_str());
            SalamanderGeneral->SalMessageBox(parent, errorText, LoadStr(IDS_PLUGINNAME), MB_OK | MB_ICONEXCLAMATION);
        }
        S3ProgressEnd();
    }

    HANDLE fileLock = NULL;
    BOOL fileLockOwner = FALSE;
    if ((!fileExists && !newFileOK) || !salamander->OpenViewer(parent, tmpFileName, &fileLock, &fileLockOwner))
    {
        fileLock = NULL;
        fileLockOwner = FALSE;
    }

    salamander->FreeFileNameInCache(uniqueFileName, fileExists, newFileOK,
                                    newFileSize, fileLock, fileLockOwner, FALSE);
}

BOOL WINAPI CPluginFSInterface::Delete(const char* fsName, int mode, HWND parent, int panel,
                                      int selectedFiles, int selectedDirs, BOOL& cancelOrError)
{
    cancelOrError = FALSE;
    if (mode == 1)
        return FALSE; // request standard delete confirmation dialog from Salamander

    if (!S3EnsureConnected(parent))
    {
        cancelOrError = TRUE;
        return FALSE;
    }

    BOOL focused = (selectedFiles == 0 && selectedDirs == 0);
    int index = 0;
    BOOL isDir = FALSE;
    BOOL success = TRUE;
    const CFileData* f;

    while (1)
    {
        if (focused)
            f = SalamanderGeneral->GetPanelFocusedItem(panel, &isDir);
        else
            f = SalamanderGeneral->GetPanelSelectedItem(panel, &index, &isDir);
        if (f == NULL)
            break;

        if (isDir && strcmp(f->Name, "..") == 0)
            continue;

        char s3ItemPath[MAX_PATH];
        S3Join(Path, f->Name, s3ItemPath, MAX_PATH);

        std::string err;
        if (!S3DeleteRecursive(s3ItemPath, isDir != FALSE, err))
        {
            char msg[512];
            snprintf(msg, sizeof(msg), "Error deleting \"%s\":\n%s\n\nContinue with other items?", f->Name, err.c_str());
            if (SalamanderGeneral->SalMessageBox(parent, msg, LoadStr(IDS_PLUGINNAME), MB_YESNO | MB_ICONEXCLAMATION) == IDNO)
            {
                success = FALSE;
                cancelOrError = TRUE;
                break;
            }
        }

        if (focused)
            break;
    }

    SalamanderGeneral->PostChangeOnPathNotification(Path, TRUE);
    SalamanderGeneral->PostRefreshPanelFS(this, FALSE);
    cancelOrError = !success;
    return success;
}

// Move cleanup after a download: delete exactly the downloaded objects, then the traversed folders
// that are empty now. Returns the number of removed objects; failed deletions go to removeFailed.
static int S3RemoveDownloaded(const S3TransferReport& report, int& removeFailed)
{
    int removed = 0;
    for (const auto& done : report.DoneItems)
    {
        std::string delErr;
        if (S3DeleteRecursive(done.c_str(), false, delErr)) removed++; else removeFailed++;
    }
    for (const auto& dir : report.DoneDirs)
    {
        std::string dErr;
        S3DeleteFolderIfEmpty(dir.c_str(), dErr); // non-empty / failure: the folder simply stays
    }
    return removed;
}

BOOL WINAPI CPluginFSInterface::CopyOrMoveFromFS(BOOL copy, int mode, const char* fsName, HWND parent,
                                                int panel, int selectedFiles, int selectedDirs,
                                                char* targetPath, BOOL& operationMask,
                                                BOOL& cancelOrHandlePath, HWND dropTarget)
{
    operationMask = FALSE;
    cancelOrHandlePath = FALSE;

    if (mode == 1) // first call: let Salamander offer target and show standard dialog
        return FALSE;
    if (mode == 4) // path processing error
        return FALSE;

    // trim mask (*.* etc.) from target path
    char target[2 * MAX_PATH];
    lstrcpynA(target, targetPath, 2 * MAX_PATH);
    char* lastBs = strrchr(target, '\\');
    char* comp = (lastBs != NULL) ? lastBs + 1 : target;
    if (strchr(comp, '*') != NULL || strchr(comp, '?') != NULL)
    {
        if (lastBs != NULL)
            *lastBs = 0;
        else
            target[0] = 0;
    }

    if (!S3EnsureConnected(parent))
    {
        cancelOrHandlePath = TRUE;
        return FALSE;
    }

    BOOL focused = (selectedFiles == 0 && selectedDirs == 0);
    int index = 0;
    BOOL isDir = FALSE;
    BOOL success = TRUE;
    const CFileData* f;

    S3ProgressBegin(parent);

    while (1)
    {
        if (focused)
            f = SalamanderGeneral->GetPanelFocusedItem(panel, &isDir);
        else
            f = SalamanderGeneral->GetPanelSelectedItem(panel, &index, &isDir);
        if (f == NULL)
            break;

        if (isDir && strcmp(f->Name, "..") == 0)
            continue;

        char s3ItemPath[MAX_PATH];
        S3Join(Path, f->Name, s3ItemPath, MAX_PATH);

        std::string err;
        S3TransferReport report;
        if (!S3DownloadRecursive(s3ItemPath, target, isDir != FALSE, err, &report, isDir ? -1 : (int64_t)f->Size.Value))
        {
            // Move: remove only what was fully downloaded; the failed items (and the folders
            // that still contain them) stay on S3.
            std::string moveNote;
            if (!copy)
            {
                int removeFailed = 0;
                int removed = S3RemoveDownloaded(report, removeFailed);
                moveNote = "\n\nMove: " + std::to_string(removed) + " downloaded file(s) removed from S3";
                if (removeFailed > 0) moveNote += ", " + std::to_string(removeFailed) + " could not be removed";
                moveNote += "; the rest was left on S3.";
            }
            std::string failList;
            for (size_t i = 0; i < report.FailedItems.size() && i < 5; i++)
                failList += "\n" + report.FailedItems[i];
            if (report.FailedItems.size() > 5)
                failList += "\n... (" + std::to_string(report.FailedItems.size() - 5) + " more)";
            std::string msg = std::string("Error downloading \"") + f->Name + "\":\n" + err + failList + moveNote;
            msg += "\n\nContinue with other items?";
            if (SalamanderGeneral->SalMessageBox(parent, msg.c_str(), LoadStr(IDS_PLUGINNAME), MB_YESNO | MB_ICONEXCLAMATION) == IDNO || report.Canceled)
            {
                success = FALSE;
                cancelOrHandlePath = TRUE;
                break;
            }
        }
        else if (!copy && S3MayDeleteWholeSource(report))
        {
            // Delete exactly what was downloaded (not a fresh listing: objects created meanwhile stay).
            int removeFailed = 0;
            S3RemoveDownloaded(report, removeFailed);
            if (removeFailed > 0)
            {
                std::string msg = std::string("Downloaded \"") + f->Name + "\" but " + std::to_string(removeFailed) +
                                  " object(s) could not be removed from S3.";
                SalamanderGeneral->SalMessageBox(parent, msg.c_str(), LoadStr(IDS_PLUGINNAME), MB_OK | MB_ICONEXCLAMATION);
            }
        }

        if (focused)
            break;
    }

    S3ProgressEnd();

    if (!copy)
    {
        SalamanderGeneral->PostChangeOnPathNotification(Path, TRUE);
        SalamanderGeneral->PostRefreshPanelFS(this, FALSE);
    }

    return success;
}

BOOL WINAPI CPluginFSInterface::CopyOrMoveFromDiskToFS(BOOL copy, int mode, const char* fsName, HWND parent,
                                                      const char* sourcePath, SalEnumSelection2 next,
                                                      void* nextParam, int sourceFiles, int sourceDirs,
                                                      char* targetPath, BOOL* invalidPathOrCancel)
{
    if (invalidPathOrCancel != NULL)
        *invalidPathOrCancel = FALSE;

    if (mode == 1)
    {
        SalamanderGeneral->SalPathAppend(targetPath, "*.*", 2 * MAX_PATH);
        return TRUE;
    }

    char remoteDir[MAX_PATH];
    char* up = strchr(targetPath, ':');
    lstrcpynA(remoteDir, (up != NULL) ? up + 1 : targetPath, MAX_PATH);
    for (char* p = remoteDir; *p; p++)
    {
        if (*p == '\\') *p = '/';
    }
    char* lastSlash = strrchr(remoteDir, '/');
    char* comp = (lastSlash != NULL) ? lastSlash + 1 : remoteDir;
    if (strchr(comp, '*') != NULL || strchr(comp, '?') != NULL)
    {
        if (lastSlash != NULL)
            *lastSlash = 0;
        else
            remoteDir[0] = 0;
    }
    S3Normalize(remoteDir);

    if (!S3EnsureConnected(parent))
    {
        if (invalidPathOrCancel != NULL)
            *invalidPathOrCancel = TRUE;
        return FALSE;
    }

    const char* name;
    const char* dosName;
    BOOL isDir;
    CQuadWord size;
    DWORD attr;
    FILETIME lastWrite;
    BOOL success = TRUE;

    S3ProgressBegin(parent);

    while ((name = next(NULL, 0, &dosName, &isDir, &size, &attr, &lastWrite, nextParam, NULL)) != NULL)
    {
        char local[2 * MAX_PATH];
        lstrcpynA(local, sourcePath, 2 * MAX_PATH);
        SalamanderGeneral->SalPathAppend(local, name, 2 * MAX_PATH);

        std::string err;
        S3TransferReport report;
        if (!S3UploadRecursive(local, remoteDir, isDir != FALSE, err, &report))
        {
            // Move: delete only the local files that were actually uploaded; failed files
            // and the folders containing them stay on disk.
            std::string moveNote;
            if (!copy)
            {
                S3LocalFinishMove(report);
                moveNote = "\n\nMove: only the successfully uploaded files (and folders left empty) were deleted locally.";
            }
            std::string failList;
            for (size_t i = 0; i < report.FailedItems.size() && i < 5; i++)
                failList += "\n" + report.FailedItems[i];
            if (report.FailedItems.size() > 5)
                failList += "\n... (" + std::to_string(report.FailedItems.size() - 5) + " more)";
            std::string eb = std::string("Error uploading \"") + name + "\":\n" + err + failList + moveNote;
            eb += "\n\nContinue with other items?";
            if (SalamanderGeneral->SalMessageBox(parent, eb.c_str(), LoadStr(IDS_PLUGINNAME), MB_YESNO | MB_ICONEXCLAMATION) == IDNO || report.Canceled)
            {
                success = FALSE;
                if (invalidPathOrCancel != NULL)
                    *invalidPathOrCancel = TRUE;
                break;
            }
            continue;
        }

        if (!copy && S3MayDeleteWholeSource(report))
        {
            // Delete exactly what was uploaded; files created during the transfer stay.
            S3LocalFinishMove(report);
        }
    }

    S3ProgressEnd();

    SalamanderGeneral->PostChangeOnPathNotification(remoteDir, TRUE);
    SalamanderGeneral->PostRefreshPanelFS(this, FALSE);

    return success;
}

BOOL WINAPI CPluginFSInterface::ChangeAttributes(const char* fsName, HWND parent, int panel,
                                                int selectedFiles, int selectedDirs)
{
    return TRUE;
}

void WINAPI CPluginFSInterface::ShowProperties(const char* fsName, HWND parent, int panel,
                                              int selectedFiles, int selectedDirs)
{
    BOOL isDir = FALSE;
    const CFileData* f = SalamanderGeneral->GetPanelFocusedItem(panel, &isDir);
    if (!f) return;

    std::string bucket, prefix;
    S3ParsePath(Path, bucket, prefix);

    char msg[1024];
    if (bucket.empty())
    {
        snprintf(msg, sizeof(msg),
            "Bucket Name: %s\n"
            "Region: %s\n"
            "Endpoint: %s",
            f->Name,
            S3ActiveProfile.Region.c_str(),
            S3ActiveProfile.Endpoint.empty() ? "AWS Default" : S3ActiveProfile.Endpoint.c_str());
    }
    else
    {
        char s3Key[MAX_PATH];
        if (!prefix.empty())
            snprintf(s3Key, sizeof(s3Key), "%s/%s", prefix.c_str(), f->Name);
        else
            lstrcpynA(s3Key, f->Name, MAX_PATH);

        CFSData* pData = (CFSData*)f->PluginData;
        snprintf(msg, sizeof(msg),
            "Name: %s\n"
            "Type: %s\n"
            "Bucket: %s\n"
            "S3 Key: %s\n"
            "Size: %llu bytes\n"
            "Storage Class: %s\n"
            "ETag: %s",
            f->Name,
            isDir ? "Folder" : "Object / File",
            bucket.c_str(),
            s3Key,
            (unsigned long long)f->Size.Value,
            pData ? pData->StorageClass : "STANDARD",
            pData ? pData->ETag : "-");
    }

    SalamanderGeneral->SalMessageBox(parent, msg, LoadStr(IDS_TITLE_PROPERTIES), MB_OK | MB_ICONINFORMATION);
}

void WINAPI CPluginFSInterface::ContextMenu(const char* fsName, HWND parent, int menuX, int menuY, int type,
                                            int panel, int selectedFiles, int selectedDirs)
{
}

void WINAPI CPluginFSInterface::ShowSecurityInfo(HWND parent)
{
    ShowInfoDialog(AssignedFSName, parent);
}

// CPluginFSDataInterface
CPluginFSDataInterface::CPluginFSDataInterface(const char* path)
{
    lstrcpynA(Path, path ? path : "/", MAX_PATH);
    Name = Path;
}

BOOL WINAPI CPluginFSDataInterface::CallReleaseForFiles()
{
    return TRUE;
}

BOOL WINAPI CPluginFSDataInterface::CallReleaseForDirs()
{
    return TRUE;
}

void WINAPI CPluginFSDataInterface::ReleasePluginData(CFileData& file, BOOL isDir)
{
    if (file.PluginData != 0)
    {
        delete (CFSData*)file.PluginData;
        file.PluginData = 0;
    }
}

void WINAPI CPluginFSDataInterface::GetFileDataForUpDir(const char* archivePath, CFileData& upDir)
{
}

BOOL WINAPI CPluginFSDataInterface::GetFileDataForNewDir(const char* dirName, CFileData& dir)
{
    return FALSE;
}

HIMAGELIST WINAPI CPluginFSDataInterface::GetSimplePluginIcons(int iconSize)
{
    return DFSImageList;
}

BOOL WINAPI CPluginFSDataInterface::HasSimplePluginIcon(CFileData& file, BOOL isDir)
{
    return FALSE;
}

HICON WINAPI CPluginFSDataInterface::GetPluginIcon(const CFileData* file, int iconSize, BOOL& destroyIcon)
{
    destroyIcon = FALSE;
    return NULL;
}

int WINAPI CPluginFSDataInterface::CompareFilesFromFS(const CFileData* file1, const CFileData* file2)
{
    if (file1 == NULL || file1->Name == NULL) return -1;
    if (file2 == NULL || file2->Name == NULL) return 1;
    return strcmp(file1->Name, file2->Name);
}

void WINAPI CPluginFSDataInterface::SetupView(BOOL leftPanel, CSalamanderViewAbstract* view,
                                             const char* archivePath, const CFileData* upperDir)
{
}

void WINAPI CPluginFSDataInterface::ColumnFixedWidthShouldChange(BOOL leftPanel, const CColumn* column, int newFixedWidth)
{
}

void WINAPI CPluginFSDataInterface::ColumnWidthWasChanged(BOOL leftPanel, const CColumn* column, int newWidth)
{
}

BOOL WINAPI CPluginFSDataInterface::GetInfoLineContent(int panel, const CFileData* file, BOOL isDir,
                                                      int selectedFiles, int selectedDirs, BOOL displaySize,
                                                      const CQuadWord& selectedSize, char* buffer,
                                                      DWORD* hotTexts, int& hotTextsCount)
{
    if (file != NULL && file->PluginData != 0)
    {
        CFSData* pData = (CFSData*)file->PluginData;
        if (pData != NULL && pData->StorageClass[0] != 0)
        {
            snprintf(buffer, 256, "Class: %s  ETag: %s", pData->StorageClass, pData->ETag);
            hotTextsCount = 0;
            return TRUE;
        }
    }
    return FALSE;
}

BOOL WINAPI CPluginFSDataInterface::CanBeCopiedToClipboard()
{
    return TRUE;
}

BOOL WINAPI CPluginFSDataInterface::GetByteSize(const CFileData* file, BOOL isDir, CQuadWord* size)
{
    return FALSE;
}

BOOL WINAPI CPluginFSDataInterface::GetLastWriteDate(const CFileData* file, BOOL isDir, SYSTEMTIME* date)
{
    return FALSE;
}

BOOL WINAPI CPluginFSDataInterface::GetLastWriteTime(const CFileData* file, BOOL isDir, SYSTEMTIME* time)
{
    return FALSE;
}
