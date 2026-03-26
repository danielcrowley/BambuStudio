#include "OnShape.hpp"
#include "../GUI/GUI_App.hpp"
#include "Http.hpp"
#include "libslic3r/Format/bbs_3mf.hpp"
#include "libslic3r/Format/STL.hpp"
#include "libslic3r/Model.hpp"
#include "libslic3r/PrintConfig.hpp"
#include <nlohmann/json.hpp>
#include <boost/filesystem.hpp>
#include <fstream>
#include <algorithm>
#include <memory>
#include <mutex>
#include <atomic>

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

void OnShape::fetchRecentParts(
    std::function<void(std::vector<OnShapePart>)> on_success,
    std::function<void(std::string)>              on_error)
{
    if (!hasCredentials()) {
        on_error("No OnShape credentials configured");
        return;
    }

    Http::get(BASE_URL + "/api/documents?filter=6&limit=5")
        .auth_basic(accessKey(), secretKey())
        .timeout_max(15)
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
                    Http::get(url)
                        .auth_basic(ak, sk)
                        .timeout_max(10)
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

    Http::post(url)
        .auth_basic(ak, sk)
        .set_post_body(body_json)
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

    Http::post(create_url)
        .auth_basic(ak, sk)
        .header("Content-Type", "application/json")
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
