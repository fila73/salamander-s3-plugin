// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (c) 2026 Open Salamander Authors & Dupl3xx
// AWS Credentials and Config parser (~/.aws/credentials & ~/.aws/config)

#include "awscred.h"
#include <windows.h>
#include <fstream>
#include <sstream>
#include <algorithm>
#include <cctype>

static std::string Trim(const std::string& str)
{
    size_t first = str.find_first_not_of(" \t\r\n\"'");
    if (first == std::string::npos) return "";
    size_t last = str.find_last_not_of(" \t\r\n\"'");
    return str.substr(first, (last - first + 1));
}

static std::string ToLower(std::string s)
{
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return (char)std::tolower(c); });
    return s;
}

std::string CAwsCredManager::GetCredentialsFilePath()
{
    char envBuf[1024];
    DWORD len = GetEnvironmentVariableA("AWS_SHARED_CREDENTIALS_FILE", envBuf, sizeof(envBuf));
    if (len > 0 && len < sizeof(envBuf))
    {
        return std::string(envBuf);
    }

    len = GetEnvironmentVariableA("USERPROFILE", envBuf, sizeof(envBuf));
    if (len > 0 && len < sizeof(envBuf))
    {
        std::string p = envBuf;
        p += "\\.aws\\credentials";
        return p;
    }
    return "";
}

std::string CAwsCredManager::GetConfigFilePath()
{
    char envBuf[1024];
    DWORD len = GetEnvironmentVariableA("AWS_CONFIG_FILE", envBuf, sizeof(envBuf));
    if (len > 0 && len < sizeof(envBuf))
    {
        return std::string(envBuf);
    }

    len = GetEnvironmentVariableA("USERPROFILE", envBuf, sizeof(envBuf));
    if (len > 0 && len < sizeof(envBuf))
    {
        std::string p = envBuf;
        p += "\\.aws\\config";
        return p;
    }
    return "";
}

static void ParseIniFile(const std::string& filePath, bool isConfigFile, std::map<std::string, AwsProfile>& profiles)
{
    std::ifstream file(filePath);
    if (!file.is_open()) return;

    std::string line;
    std::string currentSection;

    while (std::getline(file, line))
    {
        std::string trimmed = Trim(line);
        if (trimmed.empty() || trimmed[0] == '#' || trimmed[0] == ';')
            continue;

        if (trimmed.front() == '[' && trimmed.back() == ']')
        {
            std::string sec = Trim(trimmed.substr(1, trimmed.size() - 2));
            if (isConfigFile)
            {
                // Config file has "[profile name]" or "[default]"
                if (sec.find("profile ") == 0)
                {
                    sec = Trim(sec.substr(8));
                }
            }
            currentSection = sec;
            if (!currentSection.empty())
            {
                if (profiles.find(currentSection) == profiles.end())
                {
                    AwsProfile p;
                    p.Name = currentSection;
                    p.PathStyle = false;
                    profiles[currentSection] = p;
                }
            }
            continue;
        }

        if (currentSection.empty()) continue;

        size_t eqPos = trimmed.find('=');
        if (eqPos == std::string::npos) continue;

        std::string key = ToLower(Trim(trimmed.substr(0, eqPos)));
        std::string val = Trim(trimmed.substr(eqPos + 1));

        AwsProfile& prof = profiles[currentSection];
        if (key == "aws_access_key_id" || key == "access_key_id")
        {
            prof.AccessKeyId = val;
        }
        else if (key == "aws_secret_access_key" || key == "secret_access_key")
        {
            prof.SecretAccessKey = val;
        }
        else if (key == "aws_session_token" || key == "session_token" || key == "aws_security_token")
        {
            prof.SessionToken = val;
        }
        else if (key == "region")
        {
            prof.Region = val;
        }
        else if (key == "endpoint_url" || key == "s3_endpoint_url")
        {
            prof.EndpointUrl = val;
        }
    }
}

std::vector<AwsProfile> CAwsCredManager::LoadAllProfiles()
{
    std::map<std::string, AwsProfile> profileMap;

    std::string credFile = GetCredentialsFilePath();
    if (!credFile.empty())
    {
        ParseIniFile(credFile, false, profileMap);
    }

    std::string configFile = GetConfigFilePath();
    if (!configFile.empty())
    {
        ParseIniFile(configFile, true, profileMap);
    }

    std::vector<AwsProfile> result;
    // Put "default" first if exists
    auto itDef = profileMap.find("default");
    if (itDef != profileMap.end())
    {
        result.push_back(itDef->second);
        profileMap.erase(itDef);
    }

    for (auto it = profileMap.begin(); it != profileMap.end(); ++it)
    {
        result.push_back(it->second);
    }

    return result;
}

bool CAwsCredManager::FindProfile(const std::string& name, AwsProfile& outProfile)
{
    std::vector<AwsProfile> profiles = LoadAllProfiles();
    std::string lowerName = ToLower(name);
    for (const auto& p : profiles)
    {
        if (ToLower(p.Name) == lowerName)
        {
            outProfile = p;
            return true;
        }
    }
    return false;
}
