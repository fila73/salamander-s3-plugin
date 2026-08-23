// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (c) 2026 Open Salamander Authors & Dupl3xx
// AWS Credentials and Config parser (~/.aws/credentials & ~/.aws/config)

#pragma once

#include <string>
#include <vector>
#include <map>

struct AwsProfile
{
    std::string Name;                // e.g. "default", "test", "stage"
    std::string AccessKeyId;         // aws_access_key_id
    std::string SecretAccessKey;     // aws_secret_access_key
    std::string SessionToken;        // aws_session_token (optional)
    std::string Region;              // e.g. "us-east-1", "eu-central-1"
    std::string EndpointUrl;         // Custom S3 endpoint URL (optional)
    bool PathStyle;                  // S3 path-style addressing toggle
};

class CAwsCredManager
{
public:
    static std::string GetCredentialsFilePath();
    static std::string GetConfigFilePath();

    // Loads all profiles found in credentials and config files
    static std::vector<AwsProfile> LoadAllProfiles();

    // Finds a specific profile by name (case-insensitive)
    static bool FindProfile(const std::string& name, AwsProfile& outProfile);
};
