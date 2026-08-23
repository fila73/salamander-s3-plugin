// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (c) 2026 Open Salamander Authors & Dupl3xx
// Amazon S3 Menu Extensions

#include "precomp.h"
#include "s3glue.h"

DWORD WINAPI CPluginInterfaceForMenuExt::GetMenuItemState(int id, DWORD eventMask)
{
    if (id == MENUCMD_DISCONNECT_ACTIVE || id == MENUCMD_DISCONNECT_LEFT || id == MENUCMD_DISCONNECT_RIGHT)
    {
        return S3Conn.IsConnected() ? MENU_ITEM_STATE_ENABLED : 0;
    }
    return MENU_ITEM_STATE_ENABLED;
}

BOOL WINAPI CPluginInterfaceForMenuExt::ExecuteMenuItem(CSalamanderForOperationsAbstract* salamander,
                                                       HWND parent, int id, DWORD eventMask)
{
    switch (id)
    {
    case MENUCMD_DISCONNECT_ACTIVE:
    {
        S3Conn.Disconnect();
        SalamanderGeneral->DisconnectFSFromPanel(parent, PANEL_SOURCE);
        return TRUE;
    }
    case MENUCMD_DISCONNECT_LEFT:
    {
        S3Conn.Disconnect();
        SalamanderGeneral->DisconnectFSFromPanel(parent, PANEL_LEFT);
        return TRUE;
    }
    case MENUCMD_DISCONNECT_RIGHT:
    {
        S3Conn.Disconnect();
        SalamanderGeneral->DisconnectFSFromPanel(parent, PANEL_RIGHT);
        return TRUE;
    }
    case MENUCMD_CALCSIZE:
    {
        CPluginFSInterfaceAbstract* fs = SalamanderGeneral->GetPanelPluginFS(PANEL_SOURCE);
        if (fs != NULL)
        {
            S3CalcSize(parent, ((CPluginFSInterface*)fs)->Path, PANEL_SOURCE);
        }
        return TRUE;
    }
    }
    return FALSE;
}

BOOL WINAPI CPluginInterfaceForMenuExt::HelpForMenuItem(HWND parent, int id)
{
    return FALSE;
}

void WINAPI CPluginInterfaceForMenuExt::BuildMenu(HWND parent, CSalamanderBuildMenuAbstract* salamander)
{
    salamander->AddMenuItem(-1, LoadStr(IDS_MENU_CALCSIZE), 0, MENUCMD_CALCSIZE, FALSE,
                            MENU_EVENT_TRUE, MENU_EVENT_THIS_PLUGIN_FS, MENU_SKILLLEVEL_ALL);
    salamander->AddMenuItem(-1, LoadStr(IDS_MENU_DISCONNECT), 0, MENUCMD_DISCONNECT_ACTIVE, FALSE,
                            MENU_EVENT_TRUE, MENU_EVENT_THIS_PLUGIN_FS, MENU_SKILLLEVEL_ALL);
}
