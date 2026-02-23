#pragma once

#include <string>
#include <vector>
#include <functional>
#include <boost/filesystem/path.hpp>

namespace Slic3r {

struct OnshapeCredentials {
    std::string access_key;
    std::string secret_key;
};

struct OnshapeDocument {
    std::string id;   // document ID (did)
    std::string wid;  // default workspace ID
    std::string name;
    std::string modified_at; // ISO8601 string
};

struct OnshaperPart {
    std::string did;
    std::string wid;
    std::string eid;  // element (part studio) ID
    std::string name;
};

class OnshapeClient {
public:
    // Build the Authorization header value for a signed Onshape API request.
    // Returns the full header value: "On <access_key>:HmacSHA256:<base64-sig>"
    static std::string build_auth_header(
        const OnshapeCredentials& creds,
        const std::string& method,
        const std::string& path,
        const std::string& query,
        const std::string& content_type,
        const std::string& nonce,
        const std::string& date);

    // List recently touched documents from Onshape.
    // Callback is invoked on a background thread.
    static void get_recent_documents(
        const OnshapeCredentials& creds,
        std::function<void(std::vector<OnshapeDocument>, std::string /*error*/)> callback);

    // List part studios within a document/workspace.
    // Callback is invoked on a background thread.
    static void get_part_studios(
        const OnshapeCredentials& creds,
        const std::string& did,
        const std::string& wid,
        std::function<void(std::vector<OnshaperPart>, std::string /*error*/)> callback);

    // Export a part studio element as STL (binary).
    // Callback receives raw STL bytes on success or a non-empty error string on failure.
    // Callback is invoked on a background thread.
    static void export_stl(
        const OnshapeCredentials& creds,
        const OnshaperPart& part,
        std::function<void(std::string /*stl_bytes*/, std::string /*error*/)> callback);

    // Upload a file to an Onshape document as a new blob element.
    // Callback is invoked on a background thread.
    static void upload_blob(
        const OnshapeCredentials& creds,
        const std::string& did,
        const std::string& wid,
        const std::string& filename,
        const boost::filesystem::path& file_path,
        std::function<void(bool /*ok*/, std::string /*error*/)> callback);

private:
    static const std::string BASE_URL;

    // Returns a generated nonce string.
    static std::string make_nonce();

    // Returns current UTC date in RFC 2822 format as required by Onshape.
    static std::string make_date();

    // Performs HMAC-SHA256 and returns base64-encoded result.
    static std::string hmac_sha256_base64(const std::string& key, const std::string& data);
};

} // namespace Slic3r
