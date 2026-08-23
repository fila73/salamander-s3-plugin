// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (c) 2026 Open Salamander Authors & Dupl3xx
// AWS Signature Version 4 (SigV4) calculation for S3 requests.

#include "s3sigv4.h"
#include <bcrypt.h>
#include <algorithm>
#include <sstream>
#include <iomanip>
#include <cctype>

#pragma comment(lib, "bcrypt.lib")

namespace S3Auth
{
    static const char HexChars[] = "0123456789abcdef";

    static std::string ToHex(const unsigned char* data, size_t length)
    {
        std::string hex;
        hex.reserve(length * 2);
        for (size_t i = 0; i < length; ++i)
        {
            hex.push_back(HexChars[(data[i] >> 4) & 0x0F]);
            hex.push_back(HexChars[data[i] & 0x0F]);
        }
        return hex;
    }

    bool Sha256Raw(const void* data, size_t length, unsigned char outHash[32])
    {
        BCRYPT_ALG_HANDLE hAlg = NULL;
        BCRYPT_HASH_HANDLE hHash = NULL;
        NTSTATUS status = BCryptOpenAlgorithmProvider(&hAlg, BCRYPT_SHA256_ALGORITHM, NULL, 0);
        if (status != 0) return false;

        status = BCryptCreateHash(hAlg, &hHash, NULL, 0, NULL, 0, 0);
        if (status != 0)
        {
            BCryptCloseAlgorithmProvider(hAlg, 0);
            return false;
        }

        status = BCryptHashData(hHash, (PUCHAR)data, (ULONG)length, 0);
        if (status == 0)
        {
            status = BCryptFinishHash(hHash, outHash, 32, 0);
        }

        BCryptDestroyHash(hHash);
        BCryptCloseAlgorithmProvider(hAlg, 0);
        return (status == 0);
    }

    std::string Sha256Hex(const void* data, size_t length)
    {
        unsigned char hash[32];
        if (Sha256Raw(data, length, hash))
        {
            return ToHex(hash, 32);
        }
        return "";
    }

    std::string Sha256Hex(const std::string& str)
    {
        return Sha256Hex(str.data(), str.size());
    }

    bool HmacSha256(const void* key, size_t keyLen, const void* data, size_t dataLen, unsigned char outMac[32])
    {
        BCRYPT_ALG_HANDLE hAlg = NULL;
        BCRYPT_HASH_HANDLE hHash = NULL;
        NTSTATUS status = BCryptOpenAlgorithmProvider(&hAlg, BCRYPT_SHA256_ALGORITHM, NULL, BCRYPT_ALG_HANDLE_HMAC_FLAG);
        if (status != 0) return false;

        status = BCryptCreateHash(hAlg, &hHash, NULL, 0, (PUCHAR)key, (ULONG)keyLen, 0);
        if (status != 0)
        {
            BCryptCloseAlgorithmProvider(hAlg, 0);
            return false;
        }

        status = BCryptHashData(hHash, (PUCHAR)data, (ULONG)dataLen, 0);
        if (status == 0)
        {
            status = BCryptFinishHash(hHash, outMac, 32, 0);
        }

        BCryptDestroyHash(hHash);
        BCryptCloseAlgorithmProvider(hAlg, 0);
        return (status == 0);
    }

    std::string HmacSha256Hex(const void* key, size_t keyLen, const void* data, size_t dataLen)
    {
        unsigned char mac[32];
        if (HmacSha256(key, keyLen, data, dataLen, mac))
        {
            return ToHex(mac, 32);
        }
        return "";
    }

    std::string UriEncode(const std::string& input, bool encodeSlash)
    {
        std::string result;
        result.reserve(input.size() * 3);

        for (size_t i = 0; i < input.size(); ++i)
        {
            unsigned char c = (unsigned char)input[i];
            if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
                (c >= '0' && c <= '9') || c == '_' || c == '-' || c == '~' || c == '.')
            {
                result.push_back((char)c);
            }
            else if (c == '/' && !encodeSlash)
            {
                result.push_back('/');
            }
            else
            {
                result.push_back('%');
                result.push_back(HexChars[(c >> 4) & 0x0F]);
                result.push_back(HexChars[c & 0x0F]);
                // AWS requires uppercase hex digits for URI encoding
                if (result[result.size() - 2] >= 'a' && result[result.size() - 2] <= 'f')
                    result[result.size() - 2] -= 32;
                if (result[result.size() - 1] >= 'a' && result[result.size() - 1] <= 'f')
                    result[result.size() - 1] -= 32;
            }
        }
        return result;
    }

    void GetTimestamps(SYSTEMTIME* st, char* outIso8601, char* outDateStamp)
    {
        if (outIso8601)
        {
            sprintf_s(outIso8601, 17, "%04u%02u%02uT%02u%02u%02uZ",
                st->wYear, st->wMonth, st->wDay,
                st->wHour, st->wMinute, st->wSecond);
        }
        if (outDateStamp)
        {
            sprintf_s(outDateStamp, 9, "%04u%02u%02u",
                st->wYear, st->wMonth, st->wDay);
        }
    }

    void GetCurrentTimestamps(char* outIso8601, char* outDateStamp)
    {
        SYSTEMTIME st;
        GetSystemTime(&st);
        GetTimestamps(&st, outIso8601, outDateStamp);
    }

    static std::string Trim(const std::string& str)
    {
        size_t first = str.find_first_not_of(" \t\r\n");
        if (first == std::string::npos) return "";
        size_t last = str.find_last_not_of(" \t\r\n");
        return str.substr(first, (last - first + 1));
    }

    static std::string ToLower(std::string s)
    {
        std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return (char)std::tolower(c); });
        return s;
    }

    bool SignRequest(SigV4Request& req)
    {
        if (req.AccessKeyId.empty() || req.SecretAccessKey.empty())
            return false;

        if (req.Iso8601Date.empty() || req.DateStamp.empty())
        {
            char iso[17], date[9];
            GetCurrentTimestamps(iso, date);
            req.Iso8601Date = iso;
            req.DateStamp = date;
        }

        if (req.Service.empty()) req.Service = "s3";
        if (req.Region.empty()) req.Region = "us-east-1";

        if (req.PayloadHashHex.empty())
        {
            req.PayloadHashHex = Sha256Hex("");
        }

        // Set standard headers
        req.Headers["x-amz-date"] = req.Iso8601Date;
        req.Headers["x-amz-content-sha256"] = req.PayloadHashHex;
        if (!req.SessionToken.empty())
        {
            req.Headers["x-amz-security-token"] = req.SessionToken;
        }

        // 1. Canonical URI
        std::string canonicalUri = req.Path;
        if (canonicalUri.empty() || canonicalUri[0] != '/')
        {
            canonicalUri = "/" + canonicalUri;
        }
        canonicalUri = UriEncode(canonicalUri, false);

        // 2. Canonical Query String
        std::string canonicalQuery;
        for (auto it = req.QueryParams.begin(); it != req.QueryParams.end(); ++it)
        {
            if (!canonicalQuery.empty()) canonicalQuery.push_back('&');
            canonicalQuery += UriEncode(it->first, true);
            canonicalQuery.push_back('=');
            canonicalQuery += UriEncode(it->second, true);
        }

        // 3. Canonical Headers & Signed Headers
        std::map<std::string, std::string> normalizedHeaders;
        for (auto it = req.Headers.begin(); it != req.Headers.end(); ++it)
        {
            std::string key = ToLower(Trim(it->first));
            std::string val = Trim(it->second);
            normalizedHeaders[key] = val;
        }

        std::string canonicalHeaders;
        std::string signedHeaders;
        for (auto it = normalizedHeaders.begin(); it != normalizedHeaders.end(); ++it)
        {
            canonicalHeaders += it->first + ":" + it->second + "\n";
            if (!signedHeaders.empty()) signedHeaders.push_back(';');
            signedHeaders += it->first;
        }

        // 4. Canonical Request
        std::string canonicalRequest =
            req.HttpMethod + "\n" +
            canonicalUri + "\n" +
            canonicalQuery + "\n" +
            canonicalHeaders + "\n" +
            signedHeaders + "\n" +
            req.PayloadHashHex;

        std::string canonicalRequestHash = Sha256Hex(canonicalRequest);

        // 5. String to Sign
        std::string credentialScope = req.DateStamp + "/" + req.Region + "/" + req.Service + "/aws4_request";
        std::string stringToSign =
            "AWS4-HMAC-SHA256\n" +
            req.Iso8601Date + "\n" +
            credentialScope + "\n" +
            canonicalRequestHash;

        // 6. Calculate Signature Key
        std::string kSecret = "AWS4" + req.SecretAccessKey;
        unsigned char kDate[32];
        if (!HmacSha256(kSecret.data(), kSecret.size(), req.DateStamp.data(), req.DateStamp.size(), kDate))
            return false;

        unsigned char kRegion[32];
        if (!HmacSha256(kDate, 32, req.Region.data(), req.Region.size(), kRegion))
            return false;

        unsigned char kService[32];
        if (!HmacSha256(kRegion, 32, req.Service.data(), req.Service.size(), kService))
            return false;

        unsigned char kSigning[32];
        const char kAws4Request[] = "aws4_request";
        if (!HmacSha256(kService, 32, kAws4Request, sizeof(kAws4Request) - 1, kSigning))
            return false;

        // 7. Calculate Signature
        std::string signature = HmacSha256Hex(kSigning, 32, stringToSign.data(), stringToSign.size());

        // 8. Authorization Header
        std::string authHeader =
            "AWS4-HMAC-SHA256 Credential=" + req.AccessKeyId + "/" + credentialScope +
            ", SignedHeaders=" + signedHeaders +
            ", Signature=" + signature;

        req.Headers["authorization"] = authHeader;
        return true;
    }
}
