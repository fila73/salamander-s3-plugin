// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (c) 2026 Open Salamander Authors & Dupl3xx
// Amazon S3 & S3-compatible REST API client using WinHTTP and SigV4.

#include "s3conn.h"
#include "s3sigv4.h"
#include "s3safe.h"
#include <sstream>
#include <algorithm>
#include <cctype>

static std::wstring ToWide(const std::string& str)
{
    if (str.empty()) return L"";
    int count = MultiByteToWideChar(CP_UTF8, 0, str.c_str(), (int)str.length(), NULL, 0);
    if (count <= 0) return L"";
    std::wstring result(count, 0);
    MultiByteToWideChar(CP_UTF8, 0, str.c_str(), (int)str.length(), &result[0], count);
    return result;
}

static std::string ToUtf8(const std::wstring& wstr)
{
    if (wstr.empty()) return "";
    int count = WideCharToMultiByte(CP_UTF8, 0, wstr.c_str(), (int)wstr.length(), NULL, 0, NULL, NULL);
    if (count <= 0) return "";
    std::string result(count, 0);
    WideCharToMultiByte(CP_UTF8, 0, wstr.c_str(), (int)wstr.length(), &result[0], count, NULL, NULL);
    return result;
}

static std::string ExtractTag(const std::string& xml, const std::string& tag, size_t& searchPos)
{
    std::string startTag = "<" + tag + ">";
    std::string endTag = "</" + tag + ">";
    size_t start = xml.find(startTag, searchPos);
    if (start == std::string::npos) return "";
    start += startTag.length();
    size_t end = xml.find(endTag, start);
    if (end == std::string::npos) return "";
    searchPos = end + endTag.length();
    return xml.substr(start, end - start);
}

static std::string ExtractXmlBlock(const std::string& xml, const std::string& tag, size_t& searchPos)
{
    std::string startTag = "<" + tag;
    std::string endTag = "</" + tag + ">";
    size_t start = xml.find(startTag, searchPos);
    if (start == std::string::npos) return "";
    size_t tagClose = xml.find('>', start);
    if (tagClose == std::string::npos) return "";
    size_t end = xml.find(endTag, tagClose + 1);
    if (end == std::string::npos) return "";
    searchPos = end + endTag.length();
    return xml.substr(tagClose + 1, end - (tagClose + 1));
}

FILETIME Iso8601ToFileTime(const std::string& isoStr)
{
    FILETIME ft = { 0, 0 };
    if (isoStr.length() < 19) return ft;

    SYSTEMTIME st = { 0 };
    int year = 0, month = 0, day = 0, hour = 0, min = 0, sec = 0;
    if (sscanf_s(isoStr.c_str(), "%d-%d-%dT%d:%d:%d", &year, &month, &day, &hour, &min, &sec) >= 6)
    {
        st.wYear = (WORD)year;
        st.wMonth = (WORD)month;
        st.wDay = (WORD)day;
        st.wHour = (WORD)hour;
        st.wMinute = (WORD)min;
        st.wSecond = (WORD)sec;
        SystemTimeToFileTime(&st, &ft);
    }
    return ft;
}

CS3Connection::CS3Connection()
{
    m_hSession = WinHttpOpen(
        L"OpenSalamander-S3-Plugin/1.0",
        WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
        WINHTTP_NO_PROXY_NAME,
        WINHTTP_NO_PROXY_BYPASS,
        0
    );
}

CS3Connection::~CS3Connection()
{
    Disconnect();
    if (m_hSession)
    {
        WinHttpCloseHandle(m_hSession);
        m_hSession = NULL;
    }
}

void CS3Connection::ParseEndpoint()
{
    m_useSsl = m_profile.UseSSL;
    m_endpointPort = m_profile.Port;
    std::string ep = m_profile.Endpoint;

    if (ep.empty())
    {
        if (m_profile.Region.empty() || m_profile.Region == "us-east-1")
        {
            m_endpointHost = "s3.amazonaws.com";
        }
        else
        {
            m_endpointHost = "s3." + m_profile.Region + ".amazonaws.com";
        }
        m_endpointBasePath = "";
        m_endpointPort = m_useSsl ? 443 : 80;
        return;
    }

    // Check URL scheme
    if (ep.find("http://") == 0)
    {
        m_useSsl = false;
        ep = ep.substr(7);
        if (m_endpointPort == 0) m_endpointPort = 80;
    }
    else if (ep.find("https://") == 0)
    {
        m_useSsl = true;
        ep = ep.substr(8);
        if (m_endpointPort == 0) m_endpointPort = 443;
    }
    else
    {
        if (m_endpointPort == 0) m_endpointPort = m_useSsl ? 443 : 80;
    }

    // Split host and path
    size_t slashPos = ep.find('/');
    if (slashPos != std::string::npos)
    {
        m_endpointHost = ep.substr(0, slashPos);
        m_endpointBasePath = ep.substr(slashPos);
        while (m_endpointBasePath.length() > 1 && m_endpointBasePath.back() == '/')
        {
            m_endpointBasePath.pop_back();
        }
    }
    else
    {
        m_endpointHost = ep;
        m_endpointBasePath = "";
    }

    // Split host and port
    size_t colonPos = m_endpointHost.find(':');
    if (colonPos != std::string::npos)
    {
        m_endpointPort = atoi(m_endpointHost.substr(colonPos + 1).c_str());
        m_endpointHost = m_endpointHost.substr(0, colonPos);
    }
}

std::string CS3Connection::BuildHostHeader(const std::string& bucket)
{
    if (!bucket.empty() && !m_profile.PathStyle && m_endpointHost.find("amazonaws.com") != std::string::npos)
    {
        return bucket + "." + m_endpointHost;
    }
    return m_endpointHost;
}

std::string CS3Connection::BuildRequestPath(const std::string& bucket, const std::string& key)
{
    std::string path = m_endpointBasePath;
    if (!bucket.empty())
    {
        if (m_profile.PathStyle || m_endpointHost.find("amazonaws.com") == std::string::npos)
        {
            path += "/" + bucket;
        }
    }
    if (!key.empty())
    {
        if (key[0] != '/') path += "/";
        path += key;
    }
    if (path.empty()) path = "/";
    return path;
}

bool CS3Connection::Connect(const S3Profile& profile, std::string& outError)
{
    Disconnect();
    m_profile = profile;
    if (m_profile.Region.empty()) m_profile.Region = "us-east-1";

    ParseEndpoint();

    if (!m_hSession)
    {
        m_hSession = WinHttpOpen(
            L"OpenSalamander-S3-Plugin/1.0",
            WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
            WINHTTP_NO_PROXY_NAME,
            WINHTTP_NO_PROXY_BYPASS,
            0
        );
        if (!m_hSession)
        {
            outError = "Failed to initialize WinHTTP session.";
            return false;
        }
    }

    // Test connection by attempting to list buckets or stat initial bucket
    if (m_profile.InitialBucket.empty())
    {
        std::vector<S3BucketInfo> buckets;
        if (!ListBuckets(buckets, outError))
        {
            return false;
        }
    }
    else
    {
        std::vector<S3ObjectInfo> items;
        if (!ListDirectory(m_profile.InitialBucket, m_profile.InitialPath, items, outError))
        {
            return false;
        }
    }

    m_connected = true;
    return true;
}

void CS3Connection::Disconnect()
{
    m_connected = false;
}

bool CS3Connection::SendS3Request(
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
)
{
    if (!m_hSession)
    {
        outError = "WinHTTP session is not initialized.";
        return false;
    }

    std::string host = BuildHostHeader(bucket);
    std::string path = BuildRequestPath(bucket, key);

    // Prepare SigV4 Request
    S3Auth::SigV4Request sigReq;
    sigReq.HttpMethod = method;
    sigReq.Path = path;
    sigReq.QueryParams = queryParams;
    sigReq.AccessKeyId = m_profile.AccessKeyId;
    sigReq.SecretAccessKey = m_profile.SecretAccessKey;
    sigReq.SessionToken = m_profile.SessionToken;
    sigReq.Region = m_profile.Region;
    sigReq.Service = "s3";
    sigReq.PayloadHashHex = S3Auth::Sha256Hex(payloadData, payloadSize);

    sigReq.Headers["host"] = host;
    for (auto it = extraHeaders.begin(); it != extraHeaders.end(); ++it)
    {
        sigReq.Headers[it->first] = it->second;
    }

    if (!S3Auth::SignRequest(sigReq))
    {
        outError = "Failed to sign S3 request with SigV4.";
        return false;
    }

    // Connect to Host
    HINTERNET hConnect = WinHttpConnect(
        m_hSession,
        ToWide(host).c_str(),
        (INTERNET_PORT)m_endpointPort,
        0
    );
    if (!hConnect)
    {
        outError = "WinHttpConnect failed: " + std::to_string(GetLastError());
        return false;
    }

    // Construct full URI with query string
    std::string fullPath = path;
    if (!queryParams.empty())
    {
        std::string qs;
        for (auto it = queryParams.begin(); it != queryParams.end(); ++it)
        {
            if (!qs.empty()) qs += "&";
            qs += S3Auth::UriEncode(it->first, true) + "=" + S3Auth::UriEncode(it->second, true);
        }
        fullPath += "?" + qs;
    }

    DWORD flags = m_useSsl ? WINHTTP_FLAG_SECURE : 0;
    HINTERNET hRequest = WinHttpOpenRequest(
        hConnect,
        ToWide(method).c_str(),
        ToWide(fullPath).c_str(),
        NULL,
        WINHTTP_NO_REFERER,
        WINHTTP_DEFAULT_ACCEPT_TYPES,
        flags
    );

    if (!hRequest)
    {
        outError = "WinHttpOpenRequest failed: " + std::to_string(GetLastError());
        WinHttpCloseHandle(hConnect);
        return false;
    }

    // Set timeout (30 seconds)
    WinHttpSetTimeouts(hRequest, 10000, 15000, 30000, 30000);

    // Build headers string
    std::wstring headersW;
    for (auto it = sigReq.Headers.begin(); it != sigReq.Headers.end(); ++it)
    {
        if (it->first == "host") continue; // WinHTTP sets host automatically
        headersW += ToWide(it->first) + L": " + ToWide(it->second) + L"\r\n";
    }

    BOOL ok = WinHttpSendRequest(
        hRequest,
        headersW.c_str(),
        (DWORD)headersW.length(),
        (LPVOID)payloadData,
        (DWORD)payloadSize,
        (DWORD)payloadSize,
        0
    );

    if (!ok)
    {
        outError = "WinHttpSendRequest failed: " + std::to_string(GetLastError());
        WinHttpCloseHandle(hRequest);
        WinHttpCloseHandle(hConnect);
        return false;
    }

    ok = WinHttpReceiveResponse(hRequest, NULL);
    if (!ok)
    {
        outError = "WinHttpReceiveResponse failed: " + std::to_string(GetLastError());
        WinHttpCloseHandle(hRequest);
        WinHttpCloseHandle(hConnect);
        return false;
    }

    DWORD statusCode = 0;
    DWORD statusSize = sizeof(statusCode);
    WinHttpQueryHeaders(
        hRequest,
        WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
        WINHTTP_HEADER_NAME_BY_INDEX,
        &statusCode,
        &statusSize,
        WINHTTP_NO_HEADER_INDEX
    );
    outStatusCode = statusCode;

    // Read response body
    outResponseBody.clear();
    DWORD bytesAvailable = 0;
    while (WinHttpQueryDataAvailable(hRequest, &bytesAvailable) && bytesAvailable > 0)
    {
        std::vector<char> buf(bytesAvailable);
        DWORD bytesRead = 0;
        if (WinHttpReadData(hRequest, buf.data(), bytesAvailable, &bytesRead) && bytesRead > 0)
        {
            outResponseBody.append(buf.data(), bytesRead);
        }
    }

    WinHttpCloseHandle(hRequest);
    WinHttpCloseHandle(hConnect);

    if (statusCode >= 400)
    {
        size_t pos = 0;
        std::string errCode = ExtractTag(outResponseBody, "Code", pos);
        pos = 0;
        std::string errMsg = ExtractTag(outResponseBody, "Message", pos);
        if (!errCode.empty() || !errMsg.empty())
        {
            outError = "S3 Error (" + std::to_string(statusCode) + ") [" + errCode + "]: " + errMsg;
        }
        else
        {
            outError = "HTTP Error " + std::to_string(statusCode);
        }
        return false;
    }

    return true;
}

bool CS3Connection::ListBuckets(std::vector<S3BucketInfo>& outBuckets, std::string& outError)
{
    outBuckets.clear();
    std::string body;
    std::map<std::string, std::string> respHeaders;
    DWORD statusCode = 0;

    if (!SendS3Request("GET", "", "", {}, {}, nullptr, 0, body, respHeaders, statusCode, outError))
    {
        return false;
    }

    size_t searchPos = 0;
    while (true)
    {
        std::string bucketXml = ExtractXmlBlock(body, "Bucket", searchPos);
        if (bucketXml.empty()) break;

        size_t bPos = 0;
        std::string name = ExtractTag(bucketXml, "Name", bPos);
        bPos = 0;
        std::string creationDate = ExtractTag(bucketXml, "CreationDate", bPos);

        if (!name.empty())
        {
            S3BucketInfo info;
            info.Name = name;
            info.CreationDate = creationDate;
            info.CreationTime = Iso8601ToFileTime(creationDate);
            outBuckets.push_back(info);
        }
    }

    return true;
}

bool CS3Connection::ListDirectory(
    const std::string& bucket,
    const std::string& prefix,
    std::vector<S3ObjectInfo>& outItems,
    std::string& outError
)
{
    outItems.clear();
    std::string continuationToken;
    std::string cleanPrefix = prefix;
    while (!cleanPrefix.empty() && cleanPrefix.front() == '/')
    {
        cleanPrefix.erase(0, 1);
    }
    if (!cleanPrefix.empty() && cleanPrefix.back() != '/')
    {
        cleanPrefix.push_back('/');
    }

    do
    {
        std::map<std::string, std::string> qp;
        qp["list-type"] = "2";
        qp["delimiter"] = "/";
        if (!cleanPrefix.empty()) qp["prefix"] = cleanPrefix;
        if (!continuationToken.empty()) qp["continuation-token"] = continuationToken;

        std::string body;
        std::map<std::string, std::string> respHeaders;
        DWORD statusCode = 0;

        if (!SendS3Request("GET", bucket, "", qp, {}, nullptr, 0, body, respHeaders, statusCode, outError))
        {
            return false;
        }

        // 1. CommonPrefixes (Subdirectories)
        size_t searchPos = 0;
        while (true)
        {
            std::string cpXml = ExtractXmlBlock(body, "CommonPrefixes", searchPos);
            if (cpXml.empty()) break;

            size_t pPos = 0;
            std::string subPrefix = ExtractTag(cpXml, "Prefix", pPos);
            if (!subPrefix.empty())
            {
                S3ObjectInfo dirItem;
                dirItem.Key = subPrefix;
                dirItem.IsDirectory = true;

                // Extract folder display name
                std::string folderName = subPrefix;
                if (folderName.back() == '/') folderName.pop_back();
                size_t slash = folderName.rfind('/');
                if (slash != std::string::npos) folderName = folderName.substr(slash + 1);
                dirItem.DisplayName = folderName;

                outItems.push_back(dirItem);
            }
        }

        // 2. Contents (Files)
        searchPos = 0;
        while (true)
        {
            std::string contentsXml = ExtractXmlBlock(body, "Contents", searchPos);
            if (contentsXml.empty()) break;

            size_t cPos = 0;
            std::string key = ExtractTag(contentsXml, "Key", cPos);
            cPos = 0;
            std::string lastMod = ExtractTag(contentsXml, "LastModified", cPos);
            cPos = 0;
            std::string sizeStr = ExtractTag(contentsXml, "Size", cPos);
            cPos = 0;
            std::string etag = ExtractTag(contentsXml, "ETag", cPos);
            cPos = 0;
            std::string storageClass = ExtractTag(contentsXml, "StorageClass", cPos);

            // Skip folder placeholder matching prefix itself
            if (key == cleanPrefix) continue;

            if (!key.empty())
            {
                S3ObjectInfo fileItem;
                fileItem.Key = key;
                fileItem.LastModified = lastMod;
                fileItem.LastWriteTime = Iso8601ToFileTime(lastMod);
                fileItem.Size = _strtoui64(sizeStr.c_str(), NULL, 10);
                fileItem.ETag = etag;
                fileItem.StorageClass = storageClass.empty() ? "STANDARD" : storageClass;
                fileItem.IsDirectory = (!key.empty() && key.back() == '/');

                std::string dispName = key;
                if (fileItem.IsDirectory && dispName.back() == '/') dispName.pop_back();
                size_t slash = dispName.rfind('/');
                if (slash != std::string::npos) dispName = dispName.substr(slash + 1);
                fileItem.DisplayName = dispName;

                outItems.push_back(fileItem);
            }
        }

        // 3. Continuation token
        searchPos = 0;
        std::string isTruncated = ExtractTag(body, "IsTruncated", searchPos);
        if (isTruncated == "true")
        {
            searchPos = 0;
            continuationToken = ExtractTag(body, "NextContinuationToken", searchPos);
        }
        else
        {
            continuationToken.clear();
        }

    } while (!continuationToken.empty());

    return true;
}

bool CS3Connection::DownloadFile(
    const std::string& bucket,
    const std::string& key,
    const wchar_t* localFilePath,
    uint64_t resumeOffset,
    S3ProgressCallback progressCb,
    void* userData,
    std::string& outError
)
{
    if (resumeOffset > 0)
    {
        // Downloads go to a temp file that is swapped in at the end; resuming is not supported.
        outError = "Resuming a download is not supported.";
        return false;
    }

    std::string host = BuildHostHeader(bucket);
    std::string path = BuildRequestPath(bucket, key);

    S3Auth::SigV4Request sigReq;
    sigReq.HttpMethod = "GET";
    sigReq.Path = path;
    sigReq.AccessKeyId = m_profile.AccessKeyId;
    sigReq.SecretAccessKey = m_profile.SecretAccessKey;
    sigReq.SessionToken = m_profile.SessionToken;
    sigReq.Region = m_profile.Region;
    sigReq.Service = "s3";
    sigReq.Headers["host"] = host;

    if (!S3Auth::SignRequest(sigReq))
    {
        outError = "Failed to sign S3 download request.";
        return false;
    }

    HINTERNET hConnect = WinHttpConnect(m_hSession, ToWide(host).c_str(), (INTERNET_PORT)m_endpointPort, 0);
    if (!hConnect)
    {
        outError = "WinHttpConnect failed.";
        return false;
    }

    DWORD flags = m_useSsl ? WINHTTP_FLAG_SECURE : 0;
    HINTERNET hRequest = WinHttpOpenRequest(hConnect, L"GET", ToWide(path).c_str(), NULL, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, flags);
    if (!hRequest)
    {
        outError = "WinHttpOpenRequest failed.";
        WinHttpCloseHandle(hConnect);
        return false;
    }

    std::wstring headersW;
    for (auto it = sigReq.Headers.begin(); it != sigReq.Headers.end(); ++it)
    {
        if (it->first == "host") continue;
        headersW += ToWide(it->first) + L": " + ToWide(it->second) + L"\r\n";
    }

    if (!WinHttpSendRequest(hRequest, headersW.c_str(), (DWORD)headersW.length(), NULL, 0, 0, 0) ||
        !WinHttpReceiveResponse(hRequest, NULL))
    {
        outError = "Download request failed.";
        WinHttpCloseHandle(hRequest);
        WinHttpCloseHandle(hConnect);
        return false;
    }

    DWORD statusCode = 0;
    DWORD statusSize = sizeof(statusCode);
    WinHttpQueryHeaders(hRequest, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER, WINHTTP_HEADER_NAME_BY_INDEX, &statusCode, &statusSize, WINHTTP_NO_HEADER_INDEX);

    if (statusCode != 200 && statusCode != 206)
    {
        outError = "Server returned HTTP status " + std::to_string(statusCode);
        WinHttpCloseHandle(hRequest);
        WinHttpCloseHandle(hConnect);
        return false;
    }

    // Get Content-Length (used to verify the download is complete)
    wchar_t clBuf[64] = { 0 };
    DWORD clBufSize = sizeof(clBuf);
    uint64_t totalBytes = 0;
    bool hasExpected = false;
    if (WinHttpQueryHeaders(hRequest, WINHTTP_QUERY_CONTENT_LENGTH, WINHTTP_HEADER_NAME_BY_INDEX, clBuf, &clBufSize, WINHTTP_NO_HEADER_INDEX))
    {
        wchar_t* endp = NULL;
        unsigned long long v = _wcstoui64(clBuf, &endp, 10);
        if (endp != clBuf)
        {
            totalBytes = v;
            hasExpected = true;
        }
    }

    // Download into a temp file next to the target; the target is replaced only after
    // the whole body was received, written and flushed successfully.
    std::wstring targetW = localFilePath;
    std::wstring tempW;
    HANDLE hFile = INVALID_HANDLE_VALUE;
    for (unsigned attempt = 0; attempt < 100 && hFile == INVALID_HANDLE_VALUE; attempt++)
    {
        tempW = S3MakeTempPath(targetW, attempt);
        hFile = CreateFileW(tempW.c_str(), GENERIC_WRITE, 0, NULL, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, NULL);
        if (hFile == INVALID_HANDLE_VALUE && GetLastError() != ERROR_FILE_EXISTS && GetLastError() != ERROR_ALREADY_EXISTS)
            break;
    }

    if (hFile == INVALID_HANDLE_VALUE)
    {
        outError = "Cannot create local file: " + std::to_string(GetLastError());
        WinHttpCloseHandle(hRequest);
        WinHttpCloseHandle(hConnect);
        return false;
    }

    uint64_t currentBytes = 0;
    bool canceled = false;
    bool failed = false;
    std::vector<char> buffer(64 * 1024); // 64KB buffer

    for (;;)
    {
        DWORD bytesAvailable = 0;
        if (!WinHttpQueryDataAvailable(hRequest, &bytesAvailable))
        {
            outError = "Connection error while downloading (WinHTTP error " + std::to_string(GetLastError()) + ").";
            failed = true;
            break;
        }
        if (bytesAvailable == 0)
            break; // regular end of body

        DWORD toRead = (DWORD)std::min<size_t>(bytesAvailable, buffer.size());
        DWORD bytesRead = 0;
        if (!WinHttpReadData(hRequest, buffer.data(), toRead, &bytesRead))
        {
            outError = "Connection error while downloading (WinHTTP error " + std::to_string(GetLastError()) + ").";
            failed = true;
            break;
        }
        if (bytesRead == 0)
            break;

        if (!S3WriteAll(hFile, buffer.data(), bytesRead))
        {
            outError = "Cannot write local file (error " + std::to_string(GetLastError()) + "), disk full?";
            failed = true;
            break;
        }
        currentBytes += bytesRead;

        if (progressCb && !progressCb(currentBytes, totalBytes, userData))
        {
            canceled = true;
            break;
        }
    }

    WinHttpCloseHandle(hRequest);
    WinHttpCloseHandle(hConnect);

    if (!canceled && !failed && !S3ReceivedSizeOk(currentBytes, hasExpected, totalBytes))
    {
        outError = "Download incomplete: received " + std::to_string(currentBytes) + " of " +
                   std::to_string(totalBytes) + " bytes.";
        failed = true;
    }
    if (!canceled && !failed && !FlushFileBuffers(hFile))
    {
        outError = "Cannot flush local file (error " + std::to_string(GetLastError()) + ").";
        failed = true;
    }
    if (!CloseHandle(hFile) && !canceled && !failed)
    {
        outError = "Cannot close local file (error " + std::to_string(GetLastError()) + ").";
        failed = true;
    }

    if (canceled || failed)
    {
        DeleteFileW(tempW.c_str()); // only the temp file; an existing target stays untouched
        if (canceled)
            outError = "Download canceled by user.";
        return false;
    }

    DWORD replErr = 0;
    if (!S3ReplaceFile(tempW, targetW, &replErr))
    {
        DeleteFileW(tempW.c_str());
        outError = "Cannot replace local file (error " + std::to_string(replErr) + ").";
        return false;
    }

    return true;
}

bool CS3Connection::UploadFile(
    const std::string& bucket,
    const std::string& key,
    const wchar_t* localFilePath,
    S3ProgressCallback progressCb,
    void* userData,
    std::string& outError
)
{
    HANDLE hFile = CreateFileW(
        localFilePath,
        GENERIC_READ,
        FILE_SHARE_READ,
        NULL,
        OPEN_EXISTING,
        FILE_ATTRIBUTE_NORMAL,
        NULL
    );

    if (hFile == INVALID_HANDLE_VALUE)
    {
        outError = "Cannot open local file for reading.";
        return false;
    }

    LARGE_INTEGER liSize;
    GetFileSizeEx(hFile, &liSize);
    uint64_t fileSize = (uint64_t)liSize.QuadPart;

    std::vector<char> fileData((size_t)fileSize);
    DWORD bytesRead = 0;
    if (fileSize > 0)
    {
        ReadFile(hFile, fileData.data(), (DWORD)fileSize, &bytesRead, NULL);
    }
    CloseHandle(hFile);

    std::string host = BuildHostHeader(bucket);
    std::string path = BuildRequestPath(bucket, key);

    S3Auth::SigV4Request sigReq;
    sigReq.HttpMethod = "PUT";
    sigReq.Path = path;
    sigReq.AccessKeyId = m_profile.AccessKeyId;
    sigReq.SecretAccessKey = m_profile.SecretAccessKey;
    sigReq.SessionToken = m_profile.SessionToken;
    sigReq.Region = m_profile.Region;
    sigReq.Service = "s3";
    sigReq.PayloadHashHex = S3Auth::Sha256Hex(fileData.data(), fileData.size());
    sigReq.Headers["host"] = host;
    sigReq.Headers["content-length"] = std::to_string(fileSize);

    if (!S3Auth::SignRequest(sigReq))
    {
        outError = "Failed to sign S3 upload request.";
        return false;
    }

    HINTERNET hConnect = WinHttpConnect(m_hSession, ToWide(host).c_str(), (INTERNET_PORT)m_endpointPort, 0);
    if (!hConnect)
    {
        outError = "WinHttpConnect failed.";
        return false;
    }

    DWORD flags = m_useSsl ? WINHTTP_FLAG_SECURE : 0;
    HINTERNET hRequest = WinHttpOpenRequest(hConnect, L"PUT", ToWide(path).c_str(), NULL, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, flags);
    if (!hRequest)
    {
        outError = "WinHttpOpenRequest failed.";
        WinHttpCloseHandle(hConnect);
        return false;
    }

    std::wstring headersW;
    for (auto it = sigReq.Headers.begin(); it != sigReq.Headers.end(); ++it)
    {
        if (it->first == "host") continue;
        headersW += ToWide(it->first) + L": " + ToWide(it->second) + L"\r\n";
    }

    if (!WinHttpSendRequest(hRequest, headersW.c_str(), (DWORD)headersW.length(), NULL, 0, (DWORD)fileSize, 0))
    {
        outError = "WinHttpSendRequest failed.";
        WinHttpCloseHandle(hRequest);
        WinHttpCloseHandle(hConnect);
        return false;
    }

    uint64_t bytesSent = 0;
    const size_t chunkSize = 64 * 1024;
    bool canceled = false;

    if (fileSize == 0)
    {
        if (progressCb && !progressCb(0, 0, userData))
            canceled = true;
    }

    while (bytesSent < fileSize && !canceled)
    {
        DWORD toWrite = (DWORD)std::min<uint64_t>(chunkSize, fileSize - bytesSent);
        DWORD written = 0;
        if (!WinHttpWriteData(hRequest, fileData.data() + bytesSent, toWrite, &written) || written == 0)
        {
            outError = "WinHttpWriteData failed.";
            break;
        }
        bytesSent += written;
        if (progressCb && !progressCb(bytesSent, fileSize, userData))
        {
            canceled = true;
            break;
        }
    }

    if (canceled)
    {
        outError = "Upload canceled by user.";
        WinHttpCloseHandle(hRequest);
        WinHttpCloseHandle(hConnect);
        return false;
    }

    if (!WinHttpReceiveResponse(hRequest, NULL))
    {
        outError = "WinHttpReceiveResponse failed.";
        WinHttpCloseHandle(hRequest);
        WinHttpCloseHandle(hConnect);
        return false;
    }

    DWORD statusCode = 0;
    DWORD statusSize = sizeof(statusCode);
    WinHttpQueryHeaders(hRequest, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER, WINHTTP_HEADER_NAME_BY_INDEX, &statusCode, &statusSize, WINHTTP_NO_HEADER_INDEX);

    WinHttpCloseHandle(hRequest);
    WinHttpCloseHandle(hConnect);

    if (statusCode != 200 && statusCode != 204)
    {
        outError = "Server returned HTTP status " + std::to_string(statusCode);
        return false;
    }

    return true;
}

bool CS3Connection::DeleteObject(const std::string& bucket, const std::string& key, std::string& outError)
{
    std::string body;
    std::map<std::string, std::string> respHeaders;
    DWORD statusCode = 0;
    return SendS3Request("DELETE", bucket, key, {}, {}, nullptr, 0, body, respHeaders, statusCode, outError);
}

bool CS3Connection::DeleteBucket(const std::string& bucket, std::string& outError)
{
    std::string body;
    std::map<std::string, std::string> respHeaders;
    DWORD statusCode = 0;
    return SendS3Request("DELETE", bucket, "", {}, {}, nullptr, 0, body, respHeaders, statusCode, outError);
}

bool CS3Connection::CreateBucket(const std::string& bucket, const std::string& region, std::string& outError)
{
    std::string body;
    std::map<std::string, std::string> respHeaders;
    DWORD statusCode = 0;
    std::string payload;

    if (!region.empty() && region != "us-east-1")
    {
        payload = "<CreateBucketConfiguration xmlns=\"http://s3.amazonaws.com/doc/2006-03-01/\">"
                  "<LocationConstraint>" + region + "</LocationConstraint>"
                  "</CreateBucketConfiguration>";
    }

    return SendS3Request("PUT", bucket, "", {}, {}, payload.data(), payload.size(), body, respHeaders, statusCode, outError);
}

bool CS3Connection::CreateFolder(const std::string& bucket, const std::string& prefix, std::string& outError)
{
    std::string folderKey = prefix;
    while (!folderKey.empty() && folderKey.front() == '/') folderKey.erase(0, 1);
    if (!folderKey.empty() && folderKey.back() != '/') folderKey.push_back('/');

    std::string body;
    std::map<std::string, std::string> respHeaders;
    DWORD statusCode = 0;
    return SendS3Request("PUT", bucket, folderKey, {}, {}, nullptr, 0, body, respHeaders, statusCode, outError);
}

bool CS3Connection::CopyObject(const std::string& srcBucket, const std::string& srcKey, const std::string& dstBucket, const std::string& dstKey, std::string& outError)
{
    std::string body;
    std::map<std::string, std::string> respHeaders;
    DWORD statusCode = 0;

    std::map<std::string, std::string> extraHeaders;
    extraHeaders["x-amz-copy-source"] = S3Auth::UriEncode("/" + srcBucket + "/" + srcKey, false);

    return SendS3Request("PUT", dstBucket, dstKey, {}, extraHeaders, nullptr, 0, body, respHeaders, statusCode, outError);
}

bool CS3Connection::StatObject(const std::string& bucket, const std::string& key, S3ObjectInfo& outInfo, std::string& outError)
{
    std::string body;
    std::map<std::string, std::string> respHeaders;
    DWORD statusCode = 0;

    if (!SendS3Request("HEAD", bucket, key, {}, {}, nullptr, 0, body, respHeaders, statusCode, outError))
    {
        return false;
    }

    outInfo.Key = key;
    return true;
}

bool CS3Connection::CalculatePrefixSize(
    const std::string& bucket,
    const std::string& prefix,
    uint64_t& outTotalBytes,
    int& outFilesCount,
    std::string& outError,
    std::function<bool(uint64_t totalBytes, int filesCount)> progressCb
)
{
    outTotalBytes = 0;
    outFilesCount = 0;
    std::string continuationToken;
    std::string cleanPrefix = prefix;
    while (!cleanPrefix.empty() && cleanPrefix.front() == '/')
    {
        cleanPrefix.erase(0, 1);
    }

    do
    {
        std::map<std::string, std::string> qp;
        qp["list-type"] = "2";
        if (!cleanPrefix.empty()) qp["prefix"] = cleanPrefix;
        if (!continuationToken.empty()) qp["continuation-token"] = continuationToken;

        std::string body;
        std::map<std::string, std::string> respHeaders;
        DWORD statusCode = 0;

        if (!SendS3Request("GET", bucket, "", qp, {}, nullptr, 0, body, respHeaders, statusCode, outError))
        {
            return false;
        }

        size_t searchPos = 0;
        while (true)
        {
            std::string contentsXml = ExtractXmlBlock(body, "Contents", searchPos);
            if (contentsXml.empty()) break;

            size_t cPos = 0;
            std::string key = ExtractTag(contentsXml, "Key", cPos);
            cPos = 0;
            std::string sizeStr = ExtractTag(contentsXml, "Size", cPos);

            if (!key.empty())
            {
                uint64_t sz = _strtoui64(sizeStr.c_str(), NULL, 10);
                outTotalBytes += sz;
                outFilesCount++;
            }
        }

        if (progressCb)
        {
            if (!progressCb(outTotalBytes, outFilesCount))
            {
                outError = "Operation cancelled by user.";
                return false;
            }
        }

        searchPos = 0;
        std::string isTruncated = ExtractTag(body, "IsTruncated", searchPos);
        if (isTruncated == "true")
        {
            searchPos = 0;
            continuationToken = ExtractTag(body, "NextContinuationToken", searchPos);
        }
        else
        {
            continuationToken.clear();
        }
    } while (!continuationToken.empty());

    return true;
}

bool CS3Connection::ListRecursiveObjects(
    const std::string& bucket,
    const std::string& prefix,
    std::vector<S3ObjectInfo>& outItems,
    std::string& outError
)
{
    outItems.clear();
    std::string continuationToken;
    std::string cleanPrefix = prefix;
    while (!cleanPrefix.empty() && cleanPrefix.front() == '/')
    {
        cleanPrefix.erase(0, 1);
    }

    do
    {
        std::map<std::string, std::string> qp;
        qp["list-type"] = "2";
        if (!cleanPrefix.empty()) qp["prefix"] = cleanPrefix;
        if (!continuationToken.empty()) qp["continuation-token"] = continuationToken;

        std::string body;
        std::map<std::string, std::string> respHeaders;
        DWORD statusCode = 0;

        if (!SendS3Request("GET", bucket, "", qp, {}, nullptr, 0, body, respHeaders, statusCode, outError))
        {
            return false;
        }

        size_t searchPos = 0;
        while (true)
        {
            std::string contentsXml = ExtractXmlBlock(body, "Contents", searchPos);
            if (contentsXml.empty()) break;

            size_t cPos = 0;
            std::string key = ExtractTag(contentsXml, "Key", cPos);
            cPos = 0;
            std::string lastMod = ExtractTag(contentsXml, "LastModified", cPos);
            cPos = 0;
            std::string sizeStr = ExtractTag(contentsXml, "Size", cPos);
            cPos = 0;
            std::string etag = ExtractTag(contentsXml, "ETag", cPos);
            cPos = 0;
            std::string storageClass = ExtractTag(contentsXml, "StorageClass", cPos);

            if (!key.empty())
            {
                S3ObjectInfo fileItem;
                fileItem.Key = key;
                fileItem.LastModified = lastMod;
                fileItem.LastWriteTime = Iso8601ToFileTime(lastMod);
                fileItem.Size = _strtoui64(sizeStr.c_str(), NULL, 10);
                fileItem.ETag = etag;
                fileItem.StorageClass = storageClass.empty() ? "STANDARD" : storageClass;
                fileItem.IsDirectory = (!key.empty() && key.back() == '/');

                std::string dispName = key;
                if (fileItem.IsDirectory && dispName.back() == '/') dispName.pop_back();
                size_t slash = dispName.rfind('/');
                if (slash != std::string::npos) dispName = dispName.substr(slash + 1);
                fileItem.DisplayName = dispName;

                outItems.push_back(fileItem);
            }
        }

        searchPos = 0;
        std::string isTruncated = ExtractTag(body, "IsTruncated", searchPos);
        if (isTruncated == "true")
        {
            searchPos = 0;
            continuationToken = ExtractTag(body, "NextContinuationToken", searchPos);
        }
        else
        {
            continuationToken.clear();
        }
    } while (!continuationToken.empty());

    return true;
}

