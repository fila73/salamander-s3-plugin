// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (c) 2026 Open Salamander Authors & Dupl3xx

#include "precomp.h"
#include <dwmapi.h>
#include <uxtheme.h>
#include <commctrl.h>
#include "../../darkmode.h"
#include "../../darkmode_backend_darkmodelib.h"
#include "../../third_party/darkmodelib/include/Darkmodelib.h"

static DarkModeColors gColors = {GetSysColor(COLOR_BTNTEXT), GetSysColor(COLOR_BTNFACE), GetSysColor(COLOR_BTNTEXT), false};
static bool gEnabled = false;
static HBRUSH gDialogBrush = NULL;
static COLORREF gDialogTextColor = CLR_INVALID;
static COLORREF gDialogBgColor = CLR_INVALID;

static const wchar_t* DARKMODELIB_TREE_STATE_PROP = L"Salamander.DarkModeLib.TreeState";
static const wchar_t* DARKMODELIB_MENU_DARK_PROP = L"Salamander.DarkModeLib.MenuDark";
static const wchar_t* DARKMODELIB_CUSTOM_TAB_PROP = L"Salamander.DarkModeLib.CustomTab";

namespace DarkModeBackendDarkModelib
{

static void RemoveControlSubclass(HWND hwnd)
{
    if (hwnd == NULL)
        return;

    wchar_t className[64] = {0};
    if (GetClassNameW(hwnd, className, _countof(className)) == 0)
        return;

    if (lstrcmpiW(className, L"Button") == 0)
    {
        dmlib::removeCheckboxOrRadioBtnCtrlSubclass(hwnd);
        dmlib::removeGroupboxCtrlSubclass(hwnd);
    }
    else if (lstrcmpiW(className, L"Static") == 0)
        dmlib::removeStaticTextCtrlSubclass(hwnd);
    else if (lstrcmpiW(className, L"ComboBox") == 0)
        dmlib::removeComboBoxCtrlSubclass(hwnd);
    else if (lstrcmpiW(className, L"ComboBoxEx32") == 0)
        dmlib::removeComboBoxExCtrlSubclass(hwnd);
    else if (lstrcmpiW(className, L"Edit") == 0 || lstrcmpiW(className, L"ListBox") == 0)
        dmlib::removeCustomBorderForListBoxOrEditCtrlSubclass(hwnd);
    else if (lstrcmpiW(className, L"SysListView32") == 0)
        dmlib::removeListViewCtrlSubclass(hwnd);
    else if (lstrcmpiW(className, L"SysHeader32") == 0)
        dmlib::removeHeaderCtrlSubclass(hwnd);
    else if (lstrcmpiW(className, L"msctls_updown32") == 0)
        dmlib::removeUpDownCtrlSubclass(hwnd);
    else if (lstrcmpiW(className, L"SysTabControl32") == 0)
        dmlib::removeTabCtrlSubclass(hwnd);
    else if (lstrcmpiW(className, L"msctls_statusbar32") == 0)
        dmlib::removeStatusBarCtrlSubclass(hwnd);
    else if (lstrcmpiW(className, L"msctls_progress32") == 0)
        dmlib::removeProgressBarCtrlSubclass(hwnd);
    else if (lstrcmpiW(className, L"SysIPAddress32") == 0)
        dmlib::removeIPAddressCtrlSubclass(hwnd);
    else if (lstrcmpiW(className, L"msctls_hotkey32") == 0)
        dmlib::removeHotKeyCtrlSubclass(hwnd);
    else if (lstrcmpiW(className, L"SysDateTimePick32") == 0)
        dmlib::removeDTPCtrlSubclass(hwnd);
}

static BOOL CALLBACK RemoveControlSubclassProc(HWND hwnd, LPARAM)
{
    RemoveControlSubclass(hwnd);
    return TRUE;
}

static void RestoreCustomTabControl(HWND hwnd)
{
    if (hwnd == NULL || GetPropW(hwnd, DARKMODELIB_CUSTOM_TAB_PROP) == NULL)
        return;

    dmlib::removeTabCtrlSubclass(hwnd);
    dmlib::removeTabCtrlUpDownSubclass(hwnd);
}

static BOOL CALLBACK RestoreCustomTabControlProc(HWND hwnd, LPARAM)
{
    RestoreCustomTabControl(hwnd);
    return TRUE;
}

static void RemoveWindowAndChildSubclasses(HWND hwnd)
{
    dmlib::removeWindowCtlColorSubclass(hwnd);
    dmlib::removeWindowNotifyCustomDrawSubclass(hwnd);
    dmlib::removeWindowSettingChangeSubclass(hwnd);
    dmlib::removeWindowEraseBgSubclass(hwnd);
    dmlib::removeWindowMenuBarSubclass(hwnd);
    EnumChildWindows(hwnd, RemoveControlSubclassProc, 0);
}

static COLORREF EnsureReadableForBackground(COLORREF text, COLORREF background)
{
    const int bgLum = (GetRValue(background) * 30 + GetGValue(background) * 59 + GetBValue(background) * 11) / 100;
    const int fgLum = (GetRValue(text) * 30 + GetGValue(text) * 59 + GetBValue(text) * 11) / 100;
    if (bgLum < 128 && fgLum < bgLum + 40)
        return RGB(0xF0, 0xF0, 0xF0);
    if (bgLum >= 128 && fgLum > bgLum - 40)
        return RGB(0x20, 0x20, 0x20);
    return text;
}

bool IsAvailable()
{
    return true;
}

static void ConfigureDarkModelib(bool dark)
{
    static bool dmlibInitialized = false;
    if (!dmlibInitialized)
    {
        dmlib::initDarkMode();
        dmlibInitialized = true;
    }
    dmlib::setDarkModeConfigEx(static_cast<UINT>(dark ? dmlib::DarkModeType::dark : dmlib::DarkModeType::classic));
    dmlib::setDefaultColors(true);
}

void ApplyTree(HWND hwnd)
{
    if (hwnd != NULL)
    {
        const bool dark = gEnabled;
        HANDLE appliedState = GetPropW(hwnd, DARKMODELIB_TREE_STATE_PROP);
        const bool wasApplied = appliedState != NULL;
        const bool wasDark = appliedState == reinterpret_cast<HANDLE>(2);
        if (wasApplied && wasDark == dark)
            return;

        ConfigureDarkModelib(dark);
        if (dark)
        {
            if (!wasDark)
            {
                dmlib::setDarkWndNotifySafe(hwnd);
                dmlib::setChildCtrlsSubclassAndTheme(hwnd);
                HWND pb = GetDlgItem(hwnd, 542);
                if (pb != NULL)
                {
                    dmlib::removeStaticTextCtrlSubclass(pb);
                    dmlib::removeProgressBarCtrlSubclass(pb);
                }
                SetPropW(hwnd, DARKMODELIB_TREE_STATE_PROP, reinterpret_cast<HANDLE>(2));
            }
        }
        else
        {
            if (wasDark)
            {
                RemoveWindowAndChildSubclasses(hwnd);
                dmlib::setChildCtrlsTheme(hwnd);
            }
            SetPropW(hwnd, DARKMODELIB_TREE_STATE_PROP, reinterpret_cast<HANDLE>(1));
        }
        ApplyMenuBar(hwnd, dark);
        dmlib::setDarkTitleBar(hwnd);
        if (wasApplied && wasDark != dark)
            RedrawWindow(hwnd, NULL, NULL, RDW_INVALIDATE | RDW_ALLCHILDREN);
        RestoreCustomTabControls(hwnd);
    }
}

void RemoveTree(HWND hwnd)
{
    if (hwnd != NULL)
    {
        RemoveWindowAndChildSubclasses(hwnd);
        dmlib::setChildCtrlsTheme(hwnd);
        RemovePropW(hwnd, DARKMODELIB_TREE_STATE_PROP);
        ApplyMenuBar(hwnd, false);
    }
}

void ApplyMenuBar(HWND hwnd, bool enableDark)
{
    if (hwnd != NULL)
    {
        const bool wasDark = GetPropW(hwnd, DARKMODELIB_MENU_DARK_PROP) != NULL;
        if (wasDark == enableDark)
            return;
        if (enableDark)
        {
            dmlib::setWindowMenuBarSubclass(hwnd);
            SetPropW(hwnd, DARKMODELIB_MENU_DARK_PROP, reinterpret_cast<HANDLE>(1));
        }
        else
        {
            dmlib::removeWindowMenuBarSubclass(hwnd);
            RemovePropW(hwnd, DARKMODELIB_MENU_DARK_PROP);
        }
        DrawMenuBar(hwnd);
    }
}

void ApplyCheckboxOrRadioButton(HWND hwnd, bool enableDark)
{
    if (hwnd != NULL)
    {
        ConfigureDarkModelib(enableDark);
        if (enableDark)
            dmlib::setCheckboxOrRadioBtnCtrlSubclass(hwnd);
        else
            dmlib::removeCheckboxOrRadioBtnCtrlSubclass(hwnd);
    }
}

void ApplyStatusBar(HWND hwnd, bool enableDark)
{
    if (hwnd != NULL)
    {
        ConfigureDarkModelib(enableDark);
        if (enableDark)
            dmlib::setStatusBarCtrlSubclass(hwnd);
        else
            dmlib::removeStatusBarCtrlSubclass(hwnd);
        InvalidateRect(hwnd, NULL, TRUE);
    }
}

void ApplyProgressBar(HWND hwnd, bool enableDark)
{
    if (hwnd != NULL)
    {
        ConfigureDarkModelib(enableDark);
        if (enableDark)
            dmlib::setProgressBarCtrlSubclass(hwnd);
        else
            dmlib::removeProgressBarCtrlSubclass(hwnd);
        InvalidateRect(hwnd, NULL, TRUE);
    }
}

bool HandleCtlColor(UINT message, WPARAM wParam, LPARAM lParam, LRESULT& result,
                    const DarkModeColors& colors, HBRUSH dialogBrush)
{
    if (message == WM_CTLCOLORSTATIC || message == WM_CTLCOLORBTN || message == WM_CTLCOLOREDIT ||
        message == WM_CTLCOLORLISTBOX || message == WM_CTLCOLORDLG || message == WM_CTLCOLORMSGBOX)
    {
        HDC hdc = reinterpret_cast<HDC>(wParam);
        if (hdc != NULL)
        {
            COLORREF textColor = colors.readableText;
            if (message == WM_CTLCOLORBTN && lParam != 0)
            {
                HWND ctrl = reinterpret_cast<HWND>(lParam);
                wchar_t className[16] = {0};
                if (GetClassNameW(ctrl, className, _countof(className)) != 0 && lstrcmpiW(className, L"Button") == 0)
                {
                    LONG_PTR style = GetWindowLongPtr(ctrl, GWL_STYLE);
                    LONG_PTR type = style & BS_TYPEMASK;
                    if (type == BS_AUTORADIOBUTTON || type == BS_RADIOBUTTON ||
                        type == BS_AUTOCHECKBOX || type == BS_CHECKBOX ||
                        type == BS_AUTO3STATE || type == BS_3STATE)
                    {
                        textColor = gEnabled ? RGB(0xF0, 0xF0, 0xF0)
                                             : EnsureReadableForBackground(colors.readableText, colors.background);
                    }
                }
            }
            SetTextColor(hdc, textColor);
            SetBkColor(hdc, colors.background);
            SetBkMode(hdc, TRANSPARENT);
            result = reinterpret_cast<LRESULT>(dialogBrush != NULL ? dialogBrush : GetSysColorBrush(COLOR_BTNFACE));
            return true;
        }
    }
    return false;
}

void ApplyStaticTextColors(HWND hwndParent, HWND specificCtrl, const DarkModeColors& colors)
{
    (void)colors;
    if (specificCtrl != NULL)
        InvalidateRect(specificCtrl, NULL, TRUE);
    else if (hwndParent != NULL)
        InvalidateRect(hwndParent, NULL, TRUE);
}

void UpdateTabControlOverflowButtons(HWND tabControl, bool enableDark)
{
    if (tabControl != NULL)
    {
        if (enableDark)
            dmlib::setTabCtrlUpDownSubclass(tabControl);
        else
            dmlib::removeTabCtrlUpDownSubclass(tabControl);

        HWND upDown = NULL;
        while ((upDown = FindWindowEx(tabControl, upDown, UPDOWN_CLASS, NULL)) != NULL)
        {
            if (enableDark)
                dmlib::setUpDownCtrlSubclass(upDown);
            else
                dmlib::removeUpDownCtrlSubclass(upDown);

            InvalidateRect(upDown, NULL, TRUE);
        }
    }
}

void UpdateListViewColors(HWND listView, COLORREF textColor, COLORREF backgroundColor, bool applyHeaderColors)
{
    if (listView != NULL)
    {
        ListView_SetTextColor(listView, textColor);
        const COLORREF bg = gEnabled ? dmlib::getCtrlBackgroundColor() : backgroundColor;
        ListView_SetTextBkColor(listView, bg);
        ListView_SetBkColor(listView, bg);

        if (applyHeaderColors && gEnabled)
        {
            const COLORREF headerBackground = RGB(0x20, 0x20, 0x20);
            const COLORREF headerEdge = RGB(0x38, 0x38, 0x38);
            dmlib::setViewBackgroundColor(gEnabled ? dmlib::getCtrlBackgroundColor() : backgroundColor);
            dmlib::setViewTextColor(textColor);
            dmlib::setHeaderBackgroundColor(headerBackground);
            dmlib::setHeaderHotBackgroundColor(RGB(0x2A, 0x2A, 0x2A));
            dmlib::setHeaderTextColor(textColor);
            dmlib::setHeaderEdgeColor(headerEdge);
            dmlib::updateViewBrushesAndPens();
            dmlib::replaceClientEdgeWithBorderSafe(listView);
            dmlib::setDarkListView(listView);
            dmlib::setDarkListViewCheckboxes(listView);
            dmlib::setListViewCtrlSubclass(listView);

            HWND header = ListView_GetHeader(listView);
            if (header != NULL)
            {
                dmlib::setHeaderCtrlSubclass(header);
                InvalidateRect(header, NULL, TRUE);
            }
        }
        else
        {
            dmlib::removeListViewCtrlSubclass(listView);
            dmlib::replaceClientEdgeWithBorderSafeEx(listView, false);
            HWND header = ListView_GetHeader(listView);
            if (header != NULL)
            {
                dmlib::removeHeaderCtrlSubclass(header);
                InvalidateRect(header, NULL, TRUE);
            }
        }

        InvalidateRect(listView, NULL, TRUE);
    }
}

void MarkCustomTabControl(HWND tabControl)
{
    if (tabControl != NULL)
    {
        SetPropW(tabControl, DARKMODELIB_CUSTOM_TAB_PROP, reinterpret_cast<HANDLE>(1));
        RestoreCustomTabControl(tabControl);
    }
}

void RestoreCustomTabControls(HWND hwndParent)
{
    if (hwndParent != NULL)
    {
        RestoreCustomTabControl(hwndParent);
        EnumChildWindows(hwndParent, RestoreCustomTabControlProc, 0);
    }
}

} // namespace DarkModeBackendDarkModelib

static COLORREF ResolveReadableForeground(COLORREF foreground, COLORREF background)
{
    const int bgLum = (GetRValue(background) * 30 + GetGValue(background) * 59 + GetBValue(background) * 11) / 100;
    const int fgLum = (GetRValue(foreground) * 30 + GetGValue(foreground) * 59 + GetBValue(foreground) * 11) / 100;
    if (bgLum < 128 && fgLum < bgLum + 40)
        return RGB(0xF0, 0xF0, 0xF0);
    if (bgLum >= 128 && fgLum > bgLum - 40)
        return RGB(0x20, 0x20, 0x20);
    return foreground;
}

COLORREF DarkModeEnsureReadableForeground(COLORREF foreground, COLORREF background)
{
    return ResolveReadableForeground(foreground, background);
}

void DarkModeSetEnabled(bool enabled)
{
    gEnabled = enabled;
    dmlib::setDarkModeConfigEx(enabled ? static_cast<UINT>(dmlib::DarkModeType::dark) : static_cast<UINT>(dmlib::DarkModeType::classic));
    dmlib::setDefaultColors(true);
}

bool DarkModeShouldUseDarkColors()
{
    return gEnabled;
}

BOOL DarkMode_ShouldUseDark()
{
    return gEnabled ? TRUE : FALSE;
}

void DarkModeApplyWindow(HWND hwnd)
{
    if (hwnd == NULL) return;
    dmlib::setDarkTitleBar(hwnd);
    dmlib::setDarkWndNotifySafe(hwnd);
}

void DarkModeApplyTree(HWND hwnd)
{
    if (hwnd == NULL) return;
    DarkModeBackendDarkModelib::ApplyTree(hwnd);
}

void DarkModeApplyMenuBar(HWND hwnd)
{
    if (hwnd == NULL) return;
    DarkModeBackendDarkModelib::ApplyMenuBar(hwnd, gEnabled);
}

void DarkModeRefreshTitleBar(HWND hwnd)
{
    if (hwnd == NULL) return;
    dmlib::setDarkTitleBar(hwnd);
}

bool DarkModeHandleSettingChange(UINT message, LPARAM lParam)
{
    if (message == WM_THEMECHANGED || message == WM_SETTINGCHANGE)
        return true;
    return false;
}

void DarkModeAllowDarkScrollbars(HWND hwnd)
{
    if (hwnd == NULL) return;
    dmlib::enableDarkScrollBarForWindowAndChildren(hwnd);
    dmlib::setDarkScrollBar(hwnd);
}

void DarkModeDisallowDarkScrollbars(HWND hwnd)
{
    if (hwnd == NULL) return;
    dmlib::disableDarkScrollBarForWindowAndChildren(hwnd);
}

void DarkModeConfigureDialogColors(COLORREF textColor, COLORREF backgroundColor, HBRUSH dialogBrush)
{
    gDialogTextColor = textColor;
    gDialogBgColor = backgroundColor;
    gDialogBrush = dialogBrush;
    gColors.text = textColor;
    gColors.background = backgroundColor;
    gColors.readableText = ResolveReadableForeground(textColor, backgroundColor);
    gColors.usingSchemeColors = true;
    dmlib::setDlgBackgroundColor(backgroundColor);
    dmlib::setTextColor(gColors.readableText);
}

void DarkModeSetConfiguredColors(COLORREF schemeTextColor, COLORREF schemeBackgroundColor,
                                 COLORREF fallbackTextColor, COLORREF fallbackBackgroundColor)
{
    const COLORREF text = (schemeTextColor == CLR_INVALID) ? fallbackTextColor : schemeTextColor;
    const COLORREF background = (schemeBackgroundColor == CLR_INVALID) ? fallbackBackgroundColor : schemeBackgroundColor;
    gColors.text = text;
    gColors.background = background;
    gColors.readableText = ResolveReadableForeground(text, background);
    gColors.usingSchemeColors = (schemeTextColor != CLR_INVALID) && (schemeBackgroundColor != CLR_INVALID);
    gDialogTextColor = gColors.text;
    gDialogBgColor = gColors.background;
    dmlib::setDlgBackgroundColor(background);
    dmlib::setTextColor(gColors.readableText);
}

const DarkModeColors& DarkModeGetColors()
{
    return gColors;
}

COLORREF DarkModeGetDialogTextColor()
{
    return (gDialogTextColor != CLR_INVALID) ? gDialogTextColor : GetSysColor(COLOR_BTNTEXT);
}

COLORREF DarkModeGetDialogBackgroundColor()
{
    return (gDialogBgColor != CLR_INVALID) ? gDialogBgColor : GetSysColor(COLOR_BTNFACE);
}

bool DarkModeHandleCtlColor(UINT message, WPARAM wParam, LPARAM lParam, LRESULT& result)
{
    return DarkModeBackendDarkModelib::HandleCtlColor(message, wParam, lParam, result, gColors, gDialogBrush);
}

void DarkModeApplyStaticTextColors(HWND hwndParent, HWND specificCtrl)
{
    DarkModeBackendDarkModelib::ApplyStaticTextColors(hwndParent, specificCtrl, gColors);
}

void DarkModeUpdateTabControlOverflowButtons(HWND tabControl)
{
    DarkModeBackendDarkModelib::UpdateTabControlOverflowButtons(tabControl, gEnabled);
}

void DarkModeUpdateListViewColors(HWND listView)
{
    DarkModeBackendDarkModelib::UpdateListViewColors(listView, gColors.readableText, gColors.background, true);
}

void DarkModeUpdateListViewColors(HWND listView, COLORREF textColor, COLORREF backgroundColor, bool applyHeaderColors)
{
    DarkModeBackendDarkModelib::UpdateListViewColors(listView, textColor, backgroundColor, applyHeaderColors);
}

bool DarkModeInitialize() { return true; }
void DarkModeShutdown() {}
bool DarkModeIsSupported() { return true; }
bool DarkModeIsWindowsDarkSchemeSelected() { return gEnabled; }
void DarkModeFixScrollbars() {}
HBRUSH DarkModeGetPanelFrameBrush() { return NULL; }
void DarkModePreserveCustomTreeView(HWND) {}
void DarkModeApplyRebarSeparators(HWND) {}
void DarkModeEnableAutoSuggestSupport(HWND) {}
void DarkModeSetAutocompleteSelectedColors(COLORREF, COLORREF) {}
