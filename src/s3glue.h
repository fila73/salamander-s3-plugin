// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (c) 2026 Open Salamander Authors & Dupl3xx
// S3 Glue layer between Salamander FS and CS3Connection + Path Helpers

#pragma once

#include "s3conn.h"
#include "awscred.h"
#include "s3safe.h"

extern CS3Connection S3Conn;
extern S3Profile S3ActiveProfile;

#define S3_MAX_PROFILES 100
extern S3Profile S3SavedProfiles[S3_MAX_PROFILES];
extern int S3SavedProfileCount;
extern char S3DefaultSession[128];

// Path helpers for S3
void S3Normalize(char* path);
void S3Join(const char* base, const char* name, char* out, int outSize);
void S3Parent(const char* path, char* out, int outSize);
bool S3IsRoot(const char* path);
bool S3ParsePath(const char* fullPath, std::string& outBucket, std::string& outPrefix);

// Connection helper (shows connect dialog if needed)
bool S3EnsureConnected(HWND parent);

// Profile loading & saving
void S3LoadProfilesFromRegistry(HKEY rootKey);
void S3SaveProfilesToRegistry(HKEY rootKey);

// Plugin menu operations
void S3EditFile(HWND parent, const char* remoteDir, const char* fileName);
void S3CalcSize(HWND parent, const char* remoteDir, int panel);

// File transfer and manipulation helpers
typedef bool (*S3GlueProgressFn)(void* ctx, const char* name, uint64_t done, uint64_t total);
void S3SetProgressCallback(S3GlueProgressFn cb, void* ctx);
void S3ProgressBegin(HWND parent);
void S3ProgressEnd();
bool S3ProgressUpdateStatus(const char* text);
bool S3ProgressCheckCancel();

bool S3DownloadSingleFile(const char* s3Path, const char* localFilePath, std::string& outError);
bool S3UploadSingleFile(const char* localFilePath, const char* s3Path, std::string& outError);
bool S3DownloadRecursive(const char* s3Path, const char* localTargetDir, bool isDir, std::string& outError,
                         S3TransferReport* report = nullptr);
bool S3UploadRecursive(const char* localPath, const char* s3TargetDir, bool isDir, std::string& outError);
bool S3DeleteRecursive(const char* s3Path, bool isDir, std::string& outError);
void S3LocalDeleteRecursive(const char* localPath, bool isDir);
