// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (c) 2026 Open Salamander Authors & Dupl3xx
// Amazon S3 & S3-compatible REST API client using WinHTTP and SigV4.

#pragma once

#include <windows.h>
#include <winhttp.h>
#include <string>
#include <vector>
#include <map>
#include <cstdint>
#include <functional>

#pragma comment(lib, "winhttp.lib")

struct S3ObjectInfo
{
    std::string Key;           // e.g. "photos/beach.jpg" or virtual folder name "photos/"
    std::string DisplayName;   // e.g. "beach.jpg" or "photos"
    uint64_t Size = 0;
    std::string LastModified;  // "2026-08-20T12:00:00.000Z"
    FILETIME LastWriteTime = { 0, 0 };
    std::string ETag;
    std::string StorageClass;  // "STANDARD", "INTELLIGENT_TIERING", "GLACIER", etc.
    bool IsDirectory = false;
};

struct S3BucketInfo
{
    std::string Name;
    std::string CreationDate;
    FILETIME CreationTime = { 0, 0 };
};

struct S3Profile
{
    std::string Name;              // Profile display name
    std::string AccessKeyId;
    std::string SecretAccessKey;
    std::string SessionToken;      // Optional (STS)
    std::string Region;            // e.g. "us-east-1", "eu-central-1"
    std::string Endpoint;          // e.g. "s3.amazonaws.com" or "http://minio.local:9000"
    std::string InitialBucket;     // Optional default bucket
    std::string InitialPath;       // Optional default path
    bool UseSSL = true;
    bool PathStyle = false;        // Force path style (required for MinIO/LocalStack/IP addresses)
    int Port = 0;                  // 0 = default (443 for HTTPS, 80 for HTTP)
    bool FromAwsCredentials = false;
    std::string AwsProfileName;    // Name of section in ~/.aws/credentials
};

typedef bool (*S3ProgressCallback)(uint64_t currentBytes, uint64_t totalBytes, void* userData);

class CS3Connection
{
private:
    HINTERNET m_hSession = NULL;
    S3Profile m_profile;
    bool m_connected = false;

    // Parsed endpoint host and path
    std::string m_endpointHost;
    std::string m_endpointBasePath;
    int m_endpointPort = 443;
    bool m_useSsl = true;

    void ParseEndpoint();
    std::string BuildHostHeader(const std::string& bucket);
    std::string BuildRequestPath(const std::string& bucket, const std::string& key);

    bool SendS3Request(
        const std::string& method,
        const std::string& bucket,
        const std::string& key,
        const std::map<std::string, std::string>& queryParams,
        const std::map<std::string, std::string>& extraHeaders,
        const void* payloadData,
        size_t payloadSize,
        std::string& outResponseBody,
        std::map<std::string, std::string>& outResponseHeaders,
        DWORD& outStatusCode,
        std::string& outError
    );

    bool UploadFileStreaming(
        const std::string& bucket,
        const std::string& key,
        const wchar_t* localFilePath,
        S3ProgressCallback progressCb,
        void* userData,
        std::string& outError
    );

public:
    CS3Connection();
    ~CS3Connection();

    bool Connect(const S3Profile& profile, std::string& outError);
    void Disconnect();
    bool IsConnected() const { return m_connected; }
    const S3Profile& GetProfile() const { return m_profile; }

    // S3 Operations
    bool ListBuckets(std::vector<S3BucketInfo>& outBuckets, std::string& outError);
    
    bool ListDirectory(
        const std::string& bucket,
        const std::string& prefix,
        std::vector<S3ObjectInfo>& outItems,
        std::string& outError
    );

    bool DownloadFile(
        const std::string& bucket,
        const std::string& key,
        const wchar_t* localFilePath,
        uint64_t resumeOffset,
        S3ProgressCallback progressCb,
        void* userData,
        std::string& outError,
        int64_t expectedSize = -1 // size from the listing, -1 = unknown
    );

    bool UploadFile(
        const std::string& bucket,
        const std::string& key,
        const wchar_t* localFilePath,
        S3ProgressCallback progressCb,
        void* userData,
        std::string& outError
    );

    bool DeleteObject(const std::string& bucket, const std::string& key, std::string& outError);
    bool DeleteBucket(const std::string& bucket, std::string& outError);
    bool CreateBucket(const std::string& bucket, const std::string& region, std::string& outError);
    bool CreateFolder(const std::string& bucket, const std::string& prefix, std::string& outError);
    bool CopyObject(const std::string& srcBucket, const std::string& srcKey, const std::string& dstBucket, const std::string& dstKey, std::string& outError);
    bool StatObject(const std::string& bucket, const std::string& key, S3ObjectInfo& outInfo, std::string& outError);
    bool CalculatePrefixSize(
        const std::string& bucket,
        const std::string& prefix,
        uint64_t& outTotalBytes,
        int& outFilesCount,
        std::string& outError,
        std::function<bool(uint64_t totalBytes, int filesCount)> progressCb = nullptr
    );
    bool ListRecursiveObjects(const std::string& bucket, const std::string& prefix, std::vector<S3ObjectInfo>& outItems, std::string& outError);
};

FILETIME Iso8601ToFileTime(const std::string& isoStr);
