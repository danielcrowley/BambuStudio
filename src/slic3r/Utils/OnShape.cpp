#include "OnShape.hpp"
#include "../GUI/GUI_App.hpp"
#include "Http.hpp"
#include "libslic3r/Format/bbs_3mf.hpp"
#include "libslic3r/Format/STL.hpp"
#include "libslic3r/Model.hpp"
#include "libslic3r/PrintConfig.hpp"
#include <nlohmann/json.hpp>
#include <boost/filesystem.hpp>
#include <openssl/hmac.h>
#include <openssl/evp.h>
#include <openssl/rand.h>
#include <fstream>
#include <algorithm>
#include <memory>
#include <mutex>
#include <atomic>
#include <ctime>
#include <sstream>
#include <iomanip>

namespace Slic3r {

const std::string OnShape::BASE_URL = "https://cad.onshape.com";

std::string OnShape::accessKey() {
    return GUI::wxGetApp().app_config->get("onshape_access_key");
}

std::string OnShape::secretKey() {
    return GUI::wxGetApp().app_config->get("onshape_secret_key");
}

bool OnShape::hasCredentials() {
    return !accessKey().empty() && !secretKey().empty();
}

// ---------------------------------------------------------------------------
// OnShape HMAC-SHA256 API key authentication
// See: https://onshape-public.github.io/docs/auth/apikeys/
// ---------------------------------------------------------------------------

static std::string base64_encode(const unsigned char* data, unsigned int len)
{
    static const char table[] =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string out;
    out.reserve(((len + 2) / 3) * 4);
    for (unsigned i = 0; i < len; i += 3) {
        unsigned val = (unsigned)data[i] << 16;
        if (i + 1 < len) val |= (unsigned)data[i + 1] << 8;
        if (i + 2 < len) val |= (unsigned)data[i + 2];
        out.push_back(table[(val >> 18) & 0x3F]);
        out.push_back(table[(val >> 12) & 0x3F]);
        out.push_back((i + 1 < len) ? table[(val >> 6) & 0x3F] : '=');
        out.push_back((i + 2 < len) ? table[val & 0x3F] : '=');
    }
    return out;
}

static std::string generate_nonce()
{
    unsigned char buf[20];
    RAND_bytes(buf, sizeof(buf));
    std::ostringstream ss;
    for (auto b : buf)
        ss << std::hex << std::setfill('0') << std::setw(2) << (int)b;
    return ss.str(); // 40 hex chars
}

static std::string http_date_now()
{
    // RFC 2616 date: "Mon, 29 Mar 2026 12:00:00 GMT" (must use English locale)
    static const char* days[] = {"Sun","Mon","Tue","Wed","Thu","Fri","Sat"};
    static const char* mons[] = {"Jan","Feb","Mar","Apr","May","Jun",
                                  "Jul","Aug","Sep","Oct","Nov","Dec"};
    std::time_t now = std::time(nullptr);
    std::tm     gmt{};
#ifdef _WIN32
    gmtime_s(&gmt, &now);
#else
    gmtime_r(&now, &gmt);
#endif
    char buf[64];
    std::snprintf(buf, sizeof(buf), "%s, %02d %s %04d %02d:%02d:%02d GMT",
                  days[gmt.tm_wday], gmt.tm_mday, mons[gmt.tm_mon],
                  gmt.tm_year + 1900, gmt.tm_hour, gmt.tm_min, gmt.tm_sec);
    return std::string(buf);
}

static std::string to_lower(const std::string& s)
{
    std::string out = s;
    std::transform(out.begin(), out.end(), out.begin(),
                   [](unsigned char c) { return std::tolower(c); });
    return out;
}

// Parse a full URL into path and query components.
static void split_url(const std::string& url, std::string& path, std::string& query)
{
    // Find the path start (after "https://host")
    auto scheme_end = url.find("://");
    size_t path_start = (scheme_end != std::string::npos)
        ? url.find('/', scheme_end + 3)
        : 0;
    if (path_start == std::string::npos) {
        path  = "/";
        query = "";
        return;
    }
    auto q = url.find('?', path_start);
    if (q == std::string::npos) {
        path  = url.substr(path_start);
        query = "";
    } else {
        path  = url.substr(path_start, q - path_start);
        query = url.substr(q + 1);
    }
}

// Sign a request and apply the three required OnShape auth headers.
static Http& onshape_sign(Http& req, const std::string& method,
                           const std::string& url,
                           const std::string& content_type,
                           const std::string& ak, const std::string& sk)
{
    std::string nonce = generate_nonce();
    std::string date  = http_date_now();
    std::string path, query;
    split_url(url, path, query);

    // Signature input: all fields joined by \n, then lowercased
    std::string sig_input = to_lower(
        method + "\n" + nonce + "\n" + date + "\n" +
        content_type + "\n" + path + "\n" + query + "\n");

    // HMAC-SHA256
    unsigned char digest[EVP_MAX_MD_SIZE];
    unsigned int  digest_len = 0;
    HMAC(EVP_sha256(),
         sk.data(), (int)sk.size(),
         (const unsigned char*)sig_input.data(), sig_input.size(),
         digest, &digest_len);

    std::string signature = base64_encode(digest, digest_len);
    std::string auth = "On " + ak + ":HmacSHA256:" + signature;

    req.header("Date", date);
    req.header("On-Nonce", nonce);
    req.header("Authorization", auth);
    return req;
}

void OnShape::fetchRecentParts(
    std::function<void(std::vector<OnShapePart>)> on_success,
    std::function<void(std::string)>              on_error)
{
    if (!hasCredentials()) {
        on_error("No OnShape credentials configured");
        return;
    }

    std::string docs_url = BASE_URL + "/api/documents?filter=6&limit=5";
    auto req = Http::get(docs_url);
    onshape_sign(req, "GET", docs_url, "", accessKey(), secretKey());
    req.timeout_max(15)
        .on_complete([on_success, on_error](std::string body, unsigned status) {
            if (status != 200) {
                on_error("OnShape document fetch failed: HTTP " + std::to_string(status));
                return;
            }
            try {
                auto docs_json = nlohmann::json::parse(body);
                if (!docs_json.contains("items")) {
                    on_error("Unexpected API response from OnShape");
                    return;
                }
                auto& items = docs_json["items"];

                if (items.empty()) {
                    on_success({});
                    return;
                }

                auto all_parts  = std::make_shared<std::vector<OnShapePart>>();
                auto mtx        = std::make_shared<std::mutex>();
                auto remaining  = std::make_shared<std::atomic<int>>((int)items.size());

                auto finish = [all_parts, on_success]() {
                    std::sort(all_parts->begin(), all_parts->end(),
                        [](const OnShapePart& a, const OnShapePart& b) {
                            return a.modified_at > b.modified_at;
                        });
                    if (all_parts->size() > 5)
                        all_parts->resize(5);
                    on_success(*all_parts);
                };

                // Cache credentials before entering callbacks (called from worker thread)
                std::string ak = accessKey();
                std::string sk = secretKey();

                for (auto& doc : items) {
                    std::string did = doc["id"].get<std::string>();
                    std::string wid = doc.value("defaultWorkspace", nlohmann::json::object()).value("id", "");
                    if (wid.empty()) {
                        // Versioned document — skip silently
                        if (--(*remaining) == 0) finish();
                        continue;
                    }

                    std::string url = BASE_URL + "/api/parts/d/" + did + "/w/" + wid;
                    auto inner_req = Http::get(url);
                    onshape_sign(inner_req, "GET", url, "", ak, sk);
                    inner_req.timeout_max(10)
                        .on_complete([did, wid, all_parts, mtx, remaining, finish]
                                     (std::string pbody, unsigned ps) {
                            if (ps == 200) {
                                try {
                                    auto parts_json = nlohmann::json::parse(pbody);
                                    std::lock_guard<std::mutex> lock(*mtx);
                                    for (auto& p : parts_json) {
                                        if (p.value("bodyType", "") != "solid") continue;
                                        OnShapePart part;
                                        part.doc_id       = did;
                                        part.workspace_id = wid;
                                        part.element_id   = p.value("elementId", "");
                                        part.part_id      = p.value("partId", "");
                                        part.part_name    = p.value("name", "");
                                        part.modified_at  = p.value("modifiedAt", "");
                                        all_parts->push_back(part);
                                    }
                                } catch (...) {}
                            }
                            if (--(*remaining) == 0) finish();
                        })
                        .on_error([remaining, finish](std::string, std::string, unsigned) {
                            if (--(*remaining) == 0) finish();
                        })
                        .perform();
                }
            } catch (const std::exception& e) {
                on_error(std::string("OnShape JSON parse error: ") + e.what());
            }
        })
        .on_error([on_error](std::string, std::string err, unsigned) {
            on_error("OnShape network error: " + err);
        })
        .perform();
}

void OnShape::exportPart(
    const OnShapePart& part,
    const std::string& out_path,
    std::function<void(std::string)> on_success,
    std::function<void(std::string)> on_error)
{
    if (!hasCredentials()) {
        on_error("No OnShape credentials configured");
        return;
    }

    std::string url = BASE_URL + "/api/partstudios/d/" + part.doc_id
                    + "/w/" + part.workspace_id
                    + "/e/" + part.element_id + "/partexport";

    std::string body_json = "{\"partIds\":[\"" + part.part_id
                          + "\"],\"format\":\"STL\",\"units\":\"millimeter\"}";

    // Cache credentials before entering callback (called from worker thread)
    std::string ak = accessKey();
    std::string sk = secretKey();

    auto export_req = Http::post(url);
    onshape_sign(export_req, "POST", url, "application/json", ak, sk);
    export_req.set_post_body(body_json)
        .header("Content-Type", "application/json")
        .timeout_max(30)
        .on_complete([out_path, on_success, on_error](std::string stl_body, unsigned status) {
            if (status != 200) {
                on_error("OnShape export failed: HTTP " + std::to_string(status));
                return;
            }
            // Write STL bytes to temp file
            std::string stl_path = out_path + ".stl";
            {
                std::ofstream f(stl_path, std::ios::binary);
                f.write(stl_body.data(), (std::streamsize)stl_body.size());
            }
            // Convert STL to 3MF
            Model model;
            if (!load_stl(stl_path.c_str(), &model)) {
                boost::filesystem::remove(stl_path);
                on_error("Failed to parse STL data from OnShape");
                return;
            }
            model.add_default_instances();

            StoreParams params;
            params.path   = out_path.c_str();
            params.model  = &model;
            params.config = nullptr;
            // strategy defaults to SaveStrategy::Zip64 per StoreParams default constructor
            if (!store_bbs_3mf(params)) {
                boost::filesystem::remove(stl_path);
                boost::filesystem::remove(out_path);
                on_error("Failed to convert STL to 3MF");
                return;
            }
            boost::filesystem::remove(stl_path);
            on_success(out_path);
        })
        .on_error([on_error](std::string, std::string err, unsigned) {
            on_error("OnShape export network error: " + err);
        })
        .perform();
}

void OnShape::uploadAttachment(
    const std::string& doc_id,
    const std::string& workspace_id,
    const std::string& file_path,
    std::function<void(bool, std::string)> on_done)
{
    if (!hasCredentials()) {
        on_done(false, "No OnShape credentials configured");
        return;
    }

    std::string create_url = BASE_URL + "/api/blobelements/d/" + doc_id + "/w/" + workspace_id;
    std::string filename   = boost::filesystem::path(file_path).filename().string();
    std::string body_json  = "{\"name\":\"" + filename + "\",\"contentType\":\"application/octet-stream\"}";

    std::string ak = accessKey();
    std::string sk = secretKey();

    auto upload_req = Http::post(create_url);
    onshape_sign(upload_req, "POST", create_url, "application/json", ak, sk);
    upload_req.header("Content-Type", "application/json")
        .set_post_body(body_json)
        .timeout_max(30)
        .on_complete([file_path, on_done, ak, sk](std::string body, unsigned status) {
            if (status != 200 && status != 201) {
                on_done(false, "Failed to create blob element: HTTP " + std::to_string(status));
                return;
            }
            try {
                auto j = nlohmann::json::parse(body);
                std::string upload_url = j.value("uploadUrl", "");
                if (upload_url.empty()) {
                    on_done(false, "No uploadUrl in OnShape blob response");
                    return;
                }
                // Use Http::put2() with set_post_body to avoid CURL mode conflict
                // Http::put2() uses CURLOPT_CUSTOMREQUEST instead of CURLOPT_UPLOAD
                Http::put2(upload_url)
                    .header("Content-Type", "application/octet-stream")
                    .set_post_body(boost::filesystem::path(file_path))
                    .timeout_max(30)
                    .on_complete([on_done](std::string, unsigned s) {
                        bool ok = (s == 200 || s == 204);
                        on_done(ok, ok ? "" : "Upload failed: HTTP " + std::to_string(s));
                    })
                    .on_error([on_done](std::string, std::string err, unsigned) {
                        on_done(false, "Upload network error: " + err);
                    })
                    .perform();
            } catch (const std::exception& e) {
                on_done(false, std::string("JSON parse error: ") + e.what());
            }
        })
        .on_error([on_done](std::string, std::string err, unsigned) {
            on_done(false, "Network error creating blob element: " + err);
        })
        .perform();
}

} // namespace Slic3r
