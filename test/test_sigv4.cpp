// Test SigV4 and AWS Credential parsing

#include <iostream>
#include <cassert>
#include "../src/s3sigv4.h"
#include "../src/awscred.h"

int main()
{
    std::cout << "Testing S3Auth..." << std::endl;

    // Test SHA256 of empty string
    std::string emptyHash = S3Auth::Sha256Hex("");
    std::cout << "Empty SHA256: " << emptyHash << std::endl;
    assert(emptyHash == "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");

    // Test SHA256 of "Hello World"
    std::string helloHash = S3Auth::Sha256Hex("Hello World");
    std::cout << "Hello World SHA256: " << helloHash << std::endl;
    assert(helloHash == "a591a6d40bf420404a011733cfb7b190d62c65bf0bcda32b57b277d9ad9f146e");

    // Test HMAC-SHA256
    std::string hmacTest = S3Auth::HmacSha256Hex("key", 3, "The quick brown fox jumps over the lazy dog", 43);
    std::cout << "HMAC-SHA256: " << hmacTest << std::endl;
    assert(hmacTest == "f7bc83f430538424b13298e6aa6fb143ef4d59a14946175997479dbc2d1a3cd8");

    // Test UriEncode
    std::string enc = S3Auth::UriEncode("my bucket/photo & file.jpg", false);
    std::cout << "URI encoded (slash preserved): " << enc << std::endl;
    assert(enc == "my%20bucket/photo%20%26%20file.jpg");

    // Test SigV4 Signing
    S3Auth::SigV4Request req;
    req.HttpMethod = "GET";
    req.Path = "/test-bucket/test.txt";
    req.AccessKeyId = "AKIAIOSFODNN7EXAMPLE";
    req.SecretAccessKey = "wJalrXUtnFEMI/K7MDENG/bPxRfiCYEXAMPLEKEY";
    req.Region = "us-east-1";
    req.Service = "s3";
    req.Iso8601Date = "20130524T000000Z";
    req.DateStamp = "20130524";
    req.Headers["host"] = "test-bucket.s3.amazonaws.com";
    req.Headers["x-amz-content-sha256"] = emptyHash;

    bool signedOk = S3Auth::SignRequest(req);
    assert(signedOk);
    std::cout << "Authorization Header:\n" << req.Headers["authorization"] << std::endl;

    // Test AWS Credentials file path detection
    std::string credPath = CAwsCredManager::GetCredentialsFilePath();
    std::cout << "Detected AWS Cred Path: " << credPath << std::endl;
    std::string cfgPath = CAwsCredManager::GetConfigFilePath();
    std::cout << "Detected AWS Config Path: " << cfgPath << std::endl;

    std::vector<AwsProfile> profiles = CAwsCredManager::LoadAllProfiles();
    std::cout << "Loaded AWS Profiles count: " << profiles.size() << std::endl;
    for (const auto& p : profiles)
    {
        std::cout << "  Profile: [" << p.Name << "] KeyID: " 
                  << (p.AccessKeyId.empty() ? "(none)" : p.AccessKeyId.substr(0, std::min<size_t>(4, p.AccessKeyId.size())) + "...")
                  << " Region: " << (p.Region.empty() ? "(default)" : p.Region)
                  << std::endl;
    }

    std::cout << "All S3Auth tests PASSED successfully!" << std::endl;
    return 0;
}
