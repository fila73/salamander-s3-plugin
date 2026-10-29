// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (c) 2026 Open Salamander Authors & Dupl3xx
// Precompiled header for S3 plugin

#pragma once

#define WIN32_LEAN_AND_MEAN
#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <windows.h>
#include <commdlg.h>
#include <shellapi.h>
#include <shlobj.h>
#ifdef _MSC_VER
#include <crtdbg.h>
#endif
#include <limits.h>
#include <process.h>
#include <commctrl.h>
#include <ostream>
#include <stdio.h>
#include <time.h>

#if defined(_DEBUG) && defined(_MSC_VER)
#define new new (_NORMAL_BLOCK, __FILE__, __LINE__)
#endif

#define S3_COMPATIBLE_WITH_500
#define S3_QUIET

#ifdef S3_COMPATIBLE_WITH_500
#define SALSDK_COMPATIBLE_WITH_VER 103
#endif

#include "versinfo.rh2"

#include "spl_com.h"
#include "spl_base.h"
#include "spl_arc.h"
#include "spl_gen.h"
#include "spl_fs.h"
#include "spl_menu.h"
#include "spl_thum.h"
#include "spl_view.h"
#include "spl_vers.h"
#include "spl_gui.h"

#include "dbg.h"
#include "mhandles.h"
#include "arraylt.h"
#include "winliblt.h"
#include "auxtools.h"
#include "plugindarkmode.h"

#include "s3.h"
#include "s3.rh"
#include "s3.rh2"
#include "lang/lang.rh"
