#pragma once
#include <string>
#include <vector>
#include <functional>

namespace Slic3r {

struct OnShapePart {
    std::string doc_id;
    std::string workspace_id;
    std::string element_id;
    std::string part_id;
    std::string part_name;
    std::string modified_at; // ISO-8601 string for display
};

class OnShape {
public:
    // Credentials are read from AppConfig on each call.
    static void fetchRecentParts(
        std::function<void(std::vector<OnShapePart>)> on_success,
        std::function<void(std::string)>              on_error
    );

    // Downloads the part as STL, converts to 3MF, writes to out_path.
    static void exportPart(
        const OnShapePart&                        part,
        const std::string&                        out_path,
        std::function<void(std::string)>          on_success,
        std::function<void(std::string)>          on_error
    );

    // Uploads file_path as a blob attachment on the given document.
    // Fire-and-forget with 30-second timeout.
    static void uploadAttachment(
        const std::string&                                      doc_id,
        const std::string&                                      workspace_id,
        const std::string&                                      file_path,
        std::function<void(bool /*ok*/, std::string /*error*/)> on_done
    );

    // Returns true if both AppConfig keys are non-empty.
    static bool hasCredentials();

private:
    static std::string accessKey();
    static std::string secretKey();
    static const std::string BASE_URL;
};

} // namespace Slic3r
