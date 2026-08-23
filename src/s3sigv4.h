// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (c) 2026 Open Salamander Authors & Dupl3xx
// AWS Signature Version 4 (SigV4) calculation for S3 requests.

#pragma once

#include <windows.h>
#include <string>
#include <map>
#include <vector>

namespace S3Auth
{
    // Computes SHA-256 hash of data and returns 64-char lowercase hex string
    std::string Sha256Hex(const void* data, size_t length);
    std::string Sha256Hex(const std::string& str);

    // Computes SHA-256 hash and returns 32-byte raw binary hash
    bool Sha256Raw(const void* data, size_t length, unsigned char outHash[32]);

    // Computes HMAC-SHA256
    bool HmacSha256(const void* key, size_t keyLen, const void* data, size_t dataLen, unsigned char outMac[32]);
    std::string HmacSha256Hex(const void* key, size_t keyLen, const void* data, size_t dataLen);

    // URI encoding conforming to RFC 3986 (AWS S3 rules)
    std::string UriEncode(const std::string& input, bool encodeSlash = true);

    // Formats timestamps
    // outIso8601: "20260820T120000Z" (length >= 17)
    // outDateStamp: "20260820" (length >= 9)
    void GetTimestamps(SYSTEMTIME* st, char* outIso8601, char* outDateStamp);
    void GetCurrentTimestamps(char* outIso8601, char* outDateStamp);

    // Request signing context
    struct SigV4Request
    {
        std::string HttpMethod;          // "GET", "PUT", "DELETE", "HEAD", "POST"
        std::string Path;                // e.g. "/my-bucket/photo.jpg" or "/"
        std::map<std::string, std::string> QueryParams; // Sorted automatically
        std::map<std::string, std::string> Headers;     // Key should be lowercased
        std::string PayloadHashHex;      // e.g. Sha256Hex("") or Sha256Hex(payload) or "UNSIGNED-PAYLOAD"
        
        std::string AccessKeyId;
        std::string SecretAccessKey;
        std::string SessionToken;        // Optional (STS)
        std::string Region;              // e.g. "us-east-1"
        std::string Service;             // "s3"
        std::string Iso8601Date;         // e.g. "20260820T120000Z"
        std::string DateStamp;           // e.g. "20260820"
    };

    // Signs the request by computing the Authorization header value and required x-amz-* headers
    // Updates req.Headers with:
    // - "x-amz-date"
    // - "x-amz-content-sha256"
    // - "x-amz-security-token" (if SessionToken non-empty)
    // - "authorization"
    bool SignRequest(SigV4Request& req);
}
