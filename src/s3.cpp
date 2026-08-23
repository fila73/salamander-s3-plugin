// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (c) 2026 Open Salamander Authors & fila-73
// Amazon S3 Plugin for Open Salamander

#include "precomp.h"
#include "s3glue.h"

CPluginInterface PluginInterface;
CPluginInterfaceForMenuExt InterfaceForMenuExt;
CPluginInterfaceForFS InterfaceForFS;

const char* PluginNameEN = "Amazon S3";
const char* PluginNameShort = "S3";

unsigned char* LowerCase = NULL;
unsigned char* UpperCase = NULL;

HINSTANCE DLLInstance = NULL;
HINSTANCE HLanguage = NULL;

CSalamanderGeneralAbstract* SalamanderGeneral = NULL;
CSalamanderDebugAbstract* SalamanderDebug = NULL;
int SalamanderVersion = 0;
CSalamanderGUIAbstract* SalamanderGUI = NULL;

BOOL WINAPI DllMain(HINSTANCE hinstDLL, DWORD fdwReason, LPVOID lpvReserved)
{
    if (fdwReason == DLL_PROCESS_ATTACH)
    {
        DLLInstance = hinstDLL;

        INITCOMMONCONTROLSEX initCtrls;
        initCtrls.dwSize = sizeof(INITCOMMONCONTROLSEX);
        initCtrls.dwICC = ICC_BAR_CLASSES;
        InitCommonControlsEx(&initCtrls);
    }
    return TRUE;
}

char* LoadStr(int resID)
{
    if (SalamanderGeneral != NULL && HLanguage != NULL)
        return SalamanderGeneral->LoadStr(HLanguage, resID);
    static char buf[1024];
    buf[0] = 0;
    LoadStringA(HLanguage ? HLanguage : DLLInstance, resID, buf, sizeof(buf));
    return buf;
}

extern "C" {

int WINAPI SalamanderPluginGetReqVer()
{
    return 103;
}

int WINAPI SalamanderPluginGetSDKVer()
{
    return LAST_VERSION_OF_SALAMANDER;
}

CPluginInterfaceAbstract* WINAPI SalamanderPluginEntry(CSalamanderPluginEntryAbstract* salamander)
{
    SalamanderDebug = salamander->GetSalamanderDebug();
    SalamanderVersion = salamander->GetVersion();

    HLanguage = salamander->LoadLanguageModule(salamander->GetParentWindow(), PluginNameEN);
    if (HLanguage == NULL)
        HLanguage = DLLInstance;

    if (SalamanderVersion < 102)
    {
        MessageBoxA(salamander->GetParentWindow(),
                    LoadStr(IDS_REQUIRE_SAL500),
                    PluginNameEN, MB_OK | MB_ICONERROR);
        return NULL;
    }

    SalamanderGeneral = salamander->GetSalamanderGeneral();
    SalamanderGeneral->GetLowerAndUpperCase(&LowerCase, &UpperCase);
    SalamanderGUI = salamander->GetSalamanderGUI();

    if (!InitializeWinLib(PluginNameEN, DLLInstance, SalamanderGeneral))
        return NULL;

    BOOL useDark = FALSE;
    if (SalamanderGeneral->GetConfigParameter(SALCFG_USEWINDOWSDARKMODE, &useDark, sizeof(useDark), NULL))
    {
        PluginDarkMode_SetHostPolicyAvailable(TRUE, useDark);
    }

    if (!InitFS())
        return NULL;

    salamander->SetBasicPluginData(LoadStr(IDS_PLUGINNAME),
                                   FUNCTION_LOADSAVECONFIGURATION | FUNCTION_FILESYSTEM,
                                   VERSINFO_VERSION_NO_PLATFORM,
                                   VERSINFO_COPYRIGHT,
                                   LoadStr(IDS_PLUGIN_DESCRIPTION),
                                   PluginNameShort, NULL, "s3");

    SalamanderGeneral->GetPluginFSName(AssignedFSName, 0);
    AssignedFSNameLen = (int)strlen(AssignedFSName);

    return &PluginInterface;
}

} // extern "C"

void WINAPI CPluginInterface::About(HWND parent)
{
    char msg[512];
    snprintf(msg, sizeof(msg),
        "Amazon S3 File-System Plugin for Open Salamander\n"
        "Version %s\n\n"
        "%s\n\n"
        "Supports Amazon AWS S3 and S3-compatible cloud storage (MinIO, Wasabi, Cloudflare R2, Backblaze B2).\n"
        "Credentials can be loaded automatically from ~/.aws/credentials or configured manually.",
        VERSINFO_VERSION_NO_PLATFORM, VERSINFO_COPYRIGHT);
    SalamanderGeneral->SalMessageBox(parent, msg, LoadStr(IDS_PLUGINNAME), MB_OK | MB_ICONINFORMATION);
}

BOOL WINAPI CPluginInterface::Release(HWND parent, BOOL force)
{
    ReleaseFS();
    S3Conn.Disconnect();
#ifdef USE_DARKMODELIB
    ReleaseWinLibDarkMode();
#endif
    ReleaseWinLib(DLLInstance);
    return TRUE;
}

void WINAPI CPluginInterface::LoadConfiguration(HWND parent, HKEY regKey, CSalamanderRegistryAbstract* registry)
{
    if (regKey != NULL)
    {
        S3LoadProfilesFromRegistry(regKey);
    }
}

void WINAPI CPluginInterface::SaveConfiguration(HWND parent, HKEY regKey, CSalamanderRegistryAbstract* registry)
{
    if (regKey != NULL)
    {
        S3SaveProfilesToRegistry(regKey);
    }
}

void WINAPI CPluginInterface::Configuration(HWND parent)
{
    About(parent);
}

void WINAPI CPluginInterface::Connect(HWND parent, CSalamanderConnectAbstract* salamander)
{
    salamander->AddMenuItem(-1, "Calculate &Size (server)", 0, MENUCMD_CALCSIZE, FALSE,
                            MENU_EVENT_TRUE, MENU_EVENT_THIS_PLUGIN_FS, MENU_SKILLLEVEL_ALL);
    salamander->AddMenuItem(-1, NULL, 0, 0, FALSE, 0, 0, MENU_SKILLLEVEL_ALL);
    salamander->AddMenuItem(-1, "&Disconnect", 0, MENUCMD_DISCONNECT_ACTIVE, FALSE,
                            MENU_EVENT_TRUE, MENU_EVENT_THIS_PLUGIN_FS, MENU_SKILLLEVEL_ALL);

    CGUIIconListAbstract* iconList = SalamanderGUI->CreateIconList();
    iconList->Create(16, 16, 1);
    HICON hIcon = (HICON)LoadImage(DLLInstance, MAKEINTRESOURCE(IDI_FS), IMAGE_ICON, 16, 16, SalamanderGeneral->GetIconLRFlags());
    iconList->ReplaceIcon(0, hIcon);
    DestroyIcon(hIcon);
    salamander->SetIconListForGUI(iconList);

    salamander->SetChangeDriveMenuItem("\tAmazon S3", 0);
    salamander->SetPluginIcon(0);
    salamander->SetPluginMenuAndToolbarIcon(0);
}

void WINAPI CPluginInterface::ReleasePluginDataInterface(CPluginDataInterfaceAbstract* pluginData)
{
    if (pluginData != NULL)
        delete pluginData;
}

CPluginInterfaceForMenuExtAbstract* WINAPI CPluginInterface::GetInterfaceForMenuExt()
{
    return &InterfaceForMenuExt;
}

CPluginInterfaceForFSAbstract* WINAPI CPluginInterface::GetInterfaceForFS()
{
    return &InterfaceForFS;
}

void WINAPI CPluginInterface::Event(int event, DWORD param)
{
    switch (event)
    {
    case PLUGINEVENT_COLORSCHANGED:
    case PLUGINEVENT_SETTINGCHANGE:
    {
#ifdef USE_DARKMODELIB
        RefreshWinLibDarkModeFromHost();
#endif
        break;
    }
    }
}

void WINAPI CPluginInterface::ClearHistory(HWND parent)
{
}
