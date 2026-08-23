// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (c) 2026 Open Salamander Authors & Dupl3xx
// Amazon S3 Connection Dialog and FS Management

#include "precomp.h"
#include "s3glue.h"

char AssignedFSName[MAX_PATH] = "s3";
int AssignedFSNameLen = 2;

HIMAGELIST DFSImageList = NULL;

bool ShowConnectDialog(HWND parent);

static bool g_PwdShown = false;
static std::vector<AwsProfile> g_AwsProfiles;

BOOL InitFS()
{
    DFSImageList = ImageList_Create(16, 16, ILC_MASK | SalamanderGeneral->GetImageListColorFlags(), 2, 0);
    if (DFSImageList == NULL)
        return FALSE;

    ImageList_SetImageCount(DFSImageList, 2);
    ImageList_SetBkColor(DFSImageList, SalamanderGeneral->GetCurrentColor(SALCOL_ITEM_BK_NORMAL));
    ImageList_ReplaceIcon(DFSImageList, 0, LoadIcon(DLLInstance, MAKEINTRESOURCE(IDI_DIR)));
    ImageList_ReplaceIcon(DFSImageList, 1, LoadIcon(DLLInstance, MAKEINTRESOURCE(IDI_FILE)));

    return TRUE;
}

void ReleaseFS()
{
    if (DFSImageList != NULL)
    {
        ImageList_Destroy(DFSImageList);
        DFSImageList = NULL;
    }
}

CPluginFSInterfaceAbstract* WINAPI CPluginInterfaceForFS::OpenFS(const char* fsName, int fsNameIndex)
{
    return new CPluginFSInterface;
}

void WINAPI CPluginInterfaceForFS::CloseFS(CPluginFSInterfaceAbstract* fs)
{
    if (fs != NULL)
        delete (CPluginFSInterface*)fs;
}

void WINAPI CPluginInterfaceForFS::ExecuteChangeDriveMenuItem(int panel)
{
    if (ShowConnectDialog(SalamanderGeneral->GetMsgBoxParent()))
    {
        UpdateWindow(SalamanderGeneral->GetMainWindowHWND());
        char initialPath[MAX_PATH] = "/";
        if (!S3ActiveProfile.InitialBucket.empty())
        {
            if (S3ActiveProfile.InitialBucket[0] == '/')
                lstrcpynA(initialPath, S3ActiveProfile.InitialBucket.c_str(), MAX_PATH);
            else
                snprintf(initialPath, sizeof(initialPath), "/%s", S3ActiveProfile.InitialBucket.c_str());

            if (!S3ActiveProfile.InitialPath.empty() && S3ActiveProfile.InitialPath != "/")
            {
                S3Join(initialPath, S3ActiveProfile.InitialPath.c_str(), initialPath, MAX_PATH);
            }
        }
        else if (!S3ActiveProfile.InitialPath.empty())
        {
            lstrcpynA(initialPath, S3ActiveProfile.InitialPath.c_str(), MAX_PATH);
            S3Normalize(initialPath);
        }
        int failReason = 0;
        SalamanderGeneral->ChangePanelPathToPluginFS(panel, AssignedFSName, initialPath, &failReason);
    }
}

BOOL WINAPI CPluginInterfaceForFS::DisconnectFS(HWND parent, BOOL isInPanel, int panel,
                                                CPluginFSInterfaceAbstract* pluginFS,
                                                const char* pluginFSName, int pluginFSNameIndex)
{
    S3Conn.Disconnect();
    if (isInPanel)
    {
        SalamanderGeneral->DisconnectFSFromPanel(parent, panel);
    }
    return TRUE;
}

void WINAPI CPluginInterfaceForFS::ExecuteOnFS(int panel, CPluginFSInterfaceAbstract* pluginFS,
                                              const char* pluginFSName, int pluginFSNameIndex,
                                              CFileData& file, int isDir)
{
    CPluginFSInterface* fs = (CPluginFSInterface*)pluginFS;
    if (isDir) // subdirectory or up-dir
    {
        char newPath[MAX_PATH];
        if (isDir == 2) // parent directory
        {
            char currentPath[MAX_PATH];
            lstrcpynA(currentPath, fs->Path, MAX_PATH);
            S3Normalize(currentPath);
            size_t len = strlen(currentPath);
            if (len > 1 && currentPath[len - 1] == '/')
                currentPath[len - 1] = 0;
            const char* lastSlash = strrchr(currentPath, '/');
            char focusName[MAX_PATH] = "";
            if (lastSlash != NULL)
            {
                lstrcpynA(focusName, lastSlash + 1, MAX_PATH);
            }
            S3Parent(fs->Path, newPath, MAX_PATH);
            int failReason = 0;
            SalamanderGeneral->ChangePanelPathToPluginFS(panel, pluginFSName, newPath, &failReason, -1, focusName[0] ? focusName : NULL);
        }
        else // subdirectory
        {
            S3Join(fs->Path, file.Name, newPath, MAX_PATH);
            int failReason = 0;
            SalamanderGeneral->ChangePanelPathToPluginFS(panel, pluginFSName, newPath, &failReason);
        }
    }
    else // file: download to temp and open with associated application
    {
        SalamanderGeneral->SetUserWorkedOnPanelPath(panel);
        char remote[MAX_PATH];
        S3Join(fs->Path, file.Name, remote, MAX_PATH);
        char tmpDir[MAX_PATH], tmpFile[MAX_PATH];
        GetTempPathA(MAX_PATH, tmpDir);
        lstrcpynA(tmpFile, tmpDir, MAX_PATH);
        SalamanderGeneral->SalPathAppend(tmpFile, file.Name, MAX_PATH);
        HWND parent = SalamanderGeneral->GetMsgBoxParent();
        std::string err;
        S3ProgressBegin(parent);
        if (S3EnsureConnected(parent) && S3DownloadSingleFile(remote, tmpFile, err))
        {
            S3ProgressEnd();
            ShellExecuteA(SalamanderGeneral->GetMainWindowHWND(), "open", tmpFile, NULL, tmpDir, SW_SHOWNORMAL);
        }
        else
        {
            S3ProgressEnd();
            char msg[600];
            snprintf(msg, sizeof(msg), LoadStr(IDS_ERR_DOWNLOAD), err.c_str());
            SalamanderGeneral->SalMessageBox(parent, msg, LoadStr(IDS_PLUGINNAME), MB_OK | MB_ICONEXCLAMATION);
        }
    }
}

BOOL WINAPI CPluginInterfaceForFS::ChangeDriveMenuItemContextMenu(HWND parent, int panel, int x, int y,
                                                                 CPluginFSInterfaceAbstract* pluginFS,
                                                                 const char* pluginFSName, int pluginFSNameIndex,
                                                                 BOOL isDetachedFS, BOOL& refreshMenu,
                                                                 BOOL& closeMenu, int& postCmd, void*& postCmdParam)
{
    return FALSE;
}

void WINAPI CPluginInterfaceForFS::ExecuteChangeDrivePostCommand(int panel, int postCmd, void* postCmdParam)
{
}

static void PopulateAwsProfiles(HWND hDlg)
{
    HWND hCombo = GetDlgItem(hDlg, IDC_PROFILES_COMBO);
    SendMessage(hCombo, CB_RESETCONTENT, 0, 0);

    SendMessage(hCombo, CB_ADDSTRING, 0, (LPARAM)LoadStr(IDS_PROFILE_MANUAL));

    g_AwsProfiles = CAwsCredManager::LoadAllProfiles();
    for (const auto& p : g_AwsProfiles)
    {
        std::string label = "[" + p.Name + "] (" + (p.Region.empty() ? "us-east-1" : p.Region) + ")";
        SendMessage(hCombo, CB_ADDSTRING, 0, (LPARAM)label.c_str());
    }

    SendMessage(hCombo, CB_SETCURSEL, g_AwsProfiles.empty() ? 0 : 1, 0);
}

static void PopulateRegions(HWND hDlg)
{
    HWND hCombo = GetDlgItem(hDlg, IDC_REGION);
    SendMessage(hCombo, CB_RESETCONTENT, 0, 0);

    const char* regions[] = {
        "us-east-1", "us-east-2", "us-west-1", "us-west-2",
        "eu-central-1", "eu-west-1", "eu-west-2", "eu-west-3", "eu-north-1",
        "ap-south-1", "ap-northeast-1", "ap-northeast-2", "ap-southeast-1", "ap-southeast-2",
        "ca-central-1", "sa-east-1"
    };

    for (const char* r : regions)
    {
        SendMessage(hCombo, CB_ADDSTRING, 0, (LPARAM)r);
    }
    SendMessage(hCombo, CB_SETCURSEL, 0, 0);
}

static void LoadProfileToDialog(HWND hDlg, const S3Profile& prof)
{
    SetDlgItemTextA(hDlg, IDC_ACCESSKEY, prof.AccessKeyId.c_str());
    SetDlgItemTextA(hDlg, IDC_SECRETKEY, prof.SecretAccessKey.c_str());
    SetDlgItemTextA(hDlg, IDC_SESSIONTOKEN, prof.SessionToken.c_str());
    SetDlgItemTextA(hDlg, IDC_REGION, prof.Region.empty() ? "us-east-1" : prof.Region.c_str());
    SetDlgItemTextA(hDlg, IDC_ENDPOINT, prof.Endpoint.c_str());
    SetDlgItemTextA(hDlg, IDC_BUCKET, prof.InitialBucket.c_str());
    SetDlgItemTextA(hDlg, IDC_PATH, prof.InitialPath.empty() ? "/" : prof.InitialPath.c_str());

    CheckDlgButton(hDlg, IDC_PATHSTYLE, prof.PathStyle ? BST_CHECKED : BST_UNCHECKED);
    CheckDlgButton(hDlg, IDC_USESSL, prof.UseSSL ? BST_CHECKED : BST_UNCHECKED);
}

static void ReadProfileFromDialog(HWND hDlg, S3Profile& prof)
{
    char buf[512];

    GetDlgItemTextA(hDlg, IDC_ACCESSKEY, buf, sizeof(buf));
    prof.AccessKeyId = buf;

    GetDlgItemTextA(hDlg, IDC_SECRETKEY, buf, sizeof(buf));
    prof.SecretAccessKey = buf;

    GetDlgItemTextA(hDlg, IDC_SESSIONTOKEN, buf, sizeof(buf));
    prof.SessionToken = buf;

    GetDlgItemTextA(hDlg, IDC_REGION, buf, sizeof(buf));
    prof.Region = buf;
    if (prof.Region.empty()) prof.Region = "us-east-1";

    GetDlgItemTextA(hDlg, IDC_ENDPOINT, buf, sizeof(buf));
    prof.Endpoint = buf;

    GetDlgItemTextA(hDlg, IDC_BUCKET, buf, sizeof(buf));
    prof.InitialBucket = buf;

    GetDlgItemTextA(hDlg, IDC_PATH, buf, sizeof(buf));
    prof.InitialPath = buf;

    prof.PathStyle = (IsDlgButtonChecked(hDlg, IDC_PATHSTYLE) == BST_CHECKED);
    prof.UseSSL = (IsDlgButtonChecked(hDlg, IDC_USESSL) == BST_CHECKED);
}

static void PopulateSavedSessions(HWND hDlg)
{
    HWND hList = GetDlgItem(hDlg, IDC_SESSIONS);
    SendMessage(hList, LB_RESETCONTENT, 0, 0);
    for (int i = 0; i < S3SavedProfileCount; i++)
    {
        SendMessageA(hList, LB_ADDSTRING, 0, (LPARAM)S3SavedProfiles[i].Name.c_str());
    }
    if (S3SavedProfileCount > 0)
        SendMessage(hList, LB_SETCURSEL, 0, 0);
}

static void ShowConnectPage(HWND hDlg, int page)
{
    BOOL showPage0 = (page == 0);
    HWND hChild = GetWindow(hDlg, GW_CHILD);
    while (hChild != NULL)
    {
        int id = GetDlgCtrlID(hChild);
        if (id != IDC_CATTREE && id != IDOK && id != IDC_SESS_SAVE && id != IDCANCEL)
        {
            if (id == IDC_SESSIONS || id == IDC_SESS_NEW || id == IDC_SESS_EDIT ||
                id == IDC_SESS_DELETE || id == IDC_SESS_RENAME)
            {
                ShowWindow(hChild, showPage0 ? SW_HIDE : SW_SHOW);
            }
            else
            {
                ShowWindow(hChild, showPage0 ? SW_SHOW : SW_HIDE);
            }
        }
        hChild = GetWindow(hChild, GW_HWNDNEXT);
    }
    if (showPage0)
    {
        SetFocus(GetDlgItem(hDlg, IDC_ACCESSKEY));
    }
    else
    {
        PopulateSavedSessions(hDlg);
        SetFocus(GetDlgItem(hDlg, IDC_SESSIONS));
    }
    InvalidateRect(hDlg, NULL, TRUE);
}

static INT_PTR CALLBACK ConnectDlgProc(HWND hDlg, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
    switch (uMsg)
    {
    case WM_INITDIALOG:
    {
        PluginDarkMode_ApplyTitleBar(hDlg);
        PluginDarkMode_ApplyListTreeThemeRecursive(hDlg);

        PopulateAwsProfiles(hDlg);
        PopulateRegions(hDlg);

        HWND hTree = GetDlgItem(hDlg, IDC_CATTREE);
        TVINSERTSTRUCTA tvis = { 0 };
        tvis.hParent = TVI_ROOT;
        tvis.hInsertAfter = TVI_LAST;
        tvis.item.mask = TVIF_TEXT | TVIF_PARAM;
        tvis.item.pszText = LoadStr(IDS_TREE_CONNECTION);
        tvis.item.lParam = 0;
        HTREEITEM hItemConn = TreeView_InsertItem(hTree, &tvis);

        tvis.item.pszText = LoadStr(IDS_TREE_SAVED_SESSIONS);
        tvis.item.lParam = 1;
        TreeView_InsertItem(hTree, &tvis);

        TreeView_SelectItem(hTree, hItemConn);
        ShowConnectPage(hDlg, 0);

        if (!S3ActiveProfile.AccessKeyId.empty())
        {
            LoadProfileToDialog(hDlg, S3ActiveProfile);
        }
        else if (!g_AwsProfiles.empty())
        {
            S3Profile p;
            const auto& ap = g_AwsProfiles[0];
            p.AccessKeyId = ap.AccessKeyId;
            p.SecretAccessKey = ap.SecretAccessKey;
            p.SessionToken = ap.SessionToken;
            p.Region = ap.Region.empty() ? "us-east-1" : ap.Region;
            p.Endpoint = ap.EndpointUrl;
            p.UseSSL = true;
            p.PathStyle = ap.PathStyle;
            LoadProfileToDialog(hDlg, p);
        }
        else
        {
            CheckDlgButton(hDlg, IDC_USESSL, BST_CHECKED);
            SetDlgItemTextA(hDlg, IDC_REGION, "us-east-1");
            SetDlgItemTextA(hDlg, IDC_PATH, "/");
        }
        return TRUE;
    }

    case WM_THEMECHANGED:
    case WM_SETTINGCHANGE:
    {
        PluginDarkMode_HandleThemeMessage(hDlg, uMsg, lParam);
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

    case WM_NOTIFY:
    {
        NMHDR* pnm = (NMHDR*)lParam;
        if (pnm->idFrom == IDC_CATTREE && (pnm->code == TVN_SELCHANGEDA || pnm->code == TVN_SELCHANGEDW))
        {
            NMTREEVIEWA* pnmtv = (NMTREEVIEWA*)lParam;
            int page = (int)pnmtv->itemNew.lParam;
            ShowConnectPage(hDlg, page);
            return TRUE;
        }
        break;
    }

    case WM_COMMAND:
    {
        switch (LOWORD(wParam))
        {
        case IDC_PROFILES_COMBO:
        {
            if (HIWORD(wParam) == CBN_SELCHANGE)
            {
                int sel = (int)SendDlgItemMessage(hDlg, IDC_PROFILES_COMBO, CB_GETCURSEL, 0, 0);
                if (sel > 0 && sel - 1 < (int)g_AwsProfiles.size())
                {
                    const auto& ap = g_AwsProfiles[sel - 1];
                    SetDlgItemTextA(hDlg, IDC_ACCESSKEY, ap.AccessKeyId.c_str());
                    SetDlgItemTextA(hDlg, IDC_SECRETKEY, ap.SecretAccessKey.c_str());
                    SetDlgItemTextA(hDlg, IDC_SESSIONTOKEN, ap.SessionToken.c_str());
                    if (!ap.Region.empty())
                        SetDlgItemTextA(hDlg, IDC_REGION, ap.Region.c_str());
                    if (!ap.EndpointUrl.empty())
                        SetDlgItemTextA(hDlg, IDC_ENDPOINT, ap.EndpointUrl.c_str());
                    CheckDlgButton(hDlg, IDC_PATHSTYLE, ap.PathStyle ? BST_CHECKED : BST_UNCHECKED);
                }
            }
            return TRUE;
        }

        case IDC_SESS_RELOAD_AWS:
        {
            PopulateAwsProfiles(hDlg);
            return TRUE;
        }

        case IDC_PWDEYE:
        {
            g_PwdShown = !g_PwdShown;
            HWND hPwd = GetDlgItem(hDlg, IDC_SECRETKEY);
            SendMessage(hPwd, EM_SETPASSWORDCHAR, g_PwdShown ? 0 : (WPARAM)'*', 0);
            InvalidateRect(hPwd, NULL, TRUE);
            return TRUE;
        }

        case IDC_SESSIONS:
        {
            if (HIWORD(wParam) == LBN_DBLCLK)
            {
                int sel = (int)SendDlgItemMessage(hDlg, IDC_SESSIONS, LB_GETCURSEL, 0, 0);
                if (sel >= 0 && sel < S3SavedProfileCount)
                {
                    S3ActiveProfile = S3SavedProfiles[sel];
                    HWND hTree = GetDlgItem(hDlg, IDC_CATTREE);
                    HTREEITEM hRoot = TreeView_GetRoot(hTree);
                    TreeView_SelectItem(hTree, hRoot);
                    LoadProfileToDialog(hDlg, S3ActiveProfile);
                }
            }
            return TRUE;
        }

        case IDC_SESS_SAVE:
        {
            S3Profile prof;
            ReadProfileFromDialog(hDlg, prof);
            char nameBuf[128] = "Default";
            if (!prof.Name.empty())
                lstrcpynA(nameBuf, prof.Name.c_str(), sizeof(nameBuf));
            else if (!prof.InitialBucket.empty())
                snprintf(nameBuf, sizeof(nameBuf), "%s", prof.InitialBucket.c_str());
            else if (!prof.Region.empty())
                snprintf(nameBuf, sizeof(nameBuf), "S3-%s", prof.Region.c_str());

            prof.Name = nameBuf;
            int found = -1;
            for (int i = 0; i < S3SavedProfileCount; i++)
            {
                if (S3SavedProfiles[i].Name == prof.Name)
                {
                    found = i;
                    break;
                }
            }
            if (found >= 0)
            {
                S3SavedProfiles[found] = prof;
            }
            else if (S3SavedProfileCount < S3_MAX_PROFILES)
            {
                S3SavedProfiles[S3SavedProfileCount++] = prof;
            }
            S3SaveProfilesToRegistry(NULL);
            PopulateSavedSessions(hDlg);
            SalamanderGeneral->SalMessageBox(hDlg, LoadStr(IDS_MSG_SAVED), LoadStr(IDS_PLUGINNAME), MB_OK | MB_ICONINFORMATION);
            return TRUE;
        }

        case IDOK:
        {
            ReadProfileFromDialog(hDlg, S3ActiveProfile);

            if (S3ActiveProfile.AccessKeyId.empty() || S3ActiveProfile.SecretAccessKey.empty())
            {
                SalamanderGeneral->SalMessageBox(hDlg, LoadStr(IDS_MSG_KEYS_EMPTY), LoadStr(IDS_PLUGINNAME), MB_OK | MB_ICONWARNING);
                return TRUE;
            }

            std::string err;
            if (!S3Conn.Connect(S3ActiveProfile, err))
            {
                SalamanderGeneral->SalMessageBox(hDlg, err.c_str(), LoadStr(IDS_TITLE_CONN_FAILED), MB_OK | MB_ICONERROR);
                return TRUE;
            }

            EndDialog(hDlg, IDOK);
            return TRUE;
        }

        case IDCANCEL:
            EndDialog(hDlg, IDCANCEL);
            return TRUE;
        }
        break;
    }
    }
    return FALSE;
}

bool ShowConnectDialog(HWND parent)
{
    INT_PTR res = DialogBoxA(HLanguage, MAKEINTRESOURCEA(IDD_CONNECT), parent, ConnectDlgProc);
    return (res == IDOK);
}
