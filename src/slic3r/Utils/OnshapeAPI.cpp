#include "OnshapeAPI.hpp"

#include <algorithm>
#include <ctime>
#include <random>
#include <sstream>

#include <boost/filesystem/fstream.hpp>
#include <boost/log/trivial.hpp>

#include <openssl/bio.h>
#include <openssl/buffer.h>
#include <openssl/evp.h>
#include <openssl/hmac.h>

#include "Http.hpp"
#include "nlohmann/json.hpp"

namespace Slic3r {

const std::string OnshapeClient::BASE_URL = "https://cad.onshape.com";

std::string OnshapeClient::make_nonce()
{
    static const char chars[] =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789";
    std::mt19937                    rng(std::random_device{}());
    std::uniform_int_distribution<int> dist(0, (int)(sizeof(chars) - 2));
    std::string nonce(25, ' ');
    for (char &c : nonce)
        c = chars[dist(rng)];
    return nonce;
}

std::string OnshapeClient::make_date()
{
    std::time_t now = std::time(nullptr);
    char        buf[64];
    std::strftime(buf, sizeof(buf), "%a, %d %b %Y %H:%M:%S GMT", std::gmtime(&now));
    return buf;
}

std::string OnshapeClient::hmac_sha256_base64(const std::string &key, const std::string &data)
{
    unsigned char digest[EVP_MAX_MD_SIZE];
    unsigned int  digest_len = 0;
    HMAC(EVP_sha256(), key.data(), (int) key.size(),
         reinterpret_cast<const unsigned char *>(data.data()), data.size(), digest, &digest_len);

    BIO *b64  = BIO_new(BIO_f_base64());
    BIO *bmem = BIO_new(BIO_s_mem());
    b64       = BIO_push(b64, bmem);
    BIO_set_flags(b64, BIO_FLAGS_BASE64_NO_NL);
    BIO_write(b64, digest, (int) digest_len);
    BIO_flush(b64);
    BUF_MEM *bptr;
    BIO_get_mem_ptr(b64, &bptr);
    std::string result(bptr->data, bptr->length);
    BIO_free_all(b64);
    return result;
}

std::string OnshapeClient::build_auth_header(const OnshapeCredentials &creds,
                                              const std::string &       method,
                                              const std::string &       path,
                                              const std::string &       query,
                                              const std::string &       content_type,
                                              const std::string &       nonce,
                                              const std::string &       date)
{
    // Onshape requires lowercase method, path, and query in the string to sign
    std::string method_lower = method;
    std::transform(method_lower.begin(), method_lower.end(), method_lower.begin(), ::tolower);

    std::string path_lower = path;
    std::transform(path_lower.begin(), path_lower.end(), path_lower.begin(), ::tolower);

    std::string query_lower = query;
    std::transform(query_lower.begin(), query_lower.end(), query_lower.begin(), ::tolower);

    std::string path_with_query = path_lower;
    if (!query_lower.empty())
        path_with_query += "?" + query_lower;

    // content_md5 is empty for GET requests
    std::string string_to_sign = method_lower + "\n" + /*content_md5=*/"" + "\n" +
                                  content_type + "\n" + date + "\n" + nonce + "\n" +
                                  path_with_query;

    std::string sig = hmac_sha256_base64(creds.secret_key, string_to_sign);
    return "On " + creds.access_key + ":HmacSHA256:" + sig;
}

void OnshapeClient::get_recent_documents(
    const OnshapeCredentials &                                    creds,
    std::function<void(std::vector<OnshapeDocument>, std::string)> callback)
{
    const std::string path         = "/api/v6/globaltreenodes/magic/recentlytouched";
    const std::string query        = "offset=0&limit=20&sortcolumn=modifiedAt&sortorder=desc";
    const std::string content_type = "";

    std::string nonce = make_nonce();
    std::string date  = make_date();
    std::string auth  = build_auth_header(creds, "GET", path, query, content_type, nonce, date);

    Http::get(BASE_URL + path + "?" + query)
        .header("Authorization", auth)
        .header("Date", date)
        .header("On-Nonce", nonce)
        .header("Accept", "application/json")
        .on_complete([callback](std::string body, unsigned) {
            try {
                auto                     j = nlohmann::json::parse(body);
                std::vector<OnshapeDocument> docs;
                for (auto &item : j["items"]) {
                    OnshapeDocument doc;
                    doc.id   = item.value("id", "");
                    doc.name = item.value("name", "");
                    if (item.contains("defaultWorkspace"))
                        doc.wid = item["defaultWorkspace"].value("id", "");
                    doc.modified_at = item.value("modifiedAt", "");
                    if (!doc.id.empty() && !doc.wid.empty())
                        docs.push_back(std::move(doc));
                }
                callback(std::move(docs), {});
            } catch (const std::exception &e) {
                callback({}, std::string("JSON parse error: ") + e.what());
            }
        })
        .on_error([callback](std::string, std::string error, unsigned status) {
            callback({}, error.empty() ? "HTTP " + std::to_string(status) : error);
        })
        .perform();
}

void OnshapeClient::get_part_studios(
    const OnshapeCredentials &                                  creds,
    const std::string &                                          did,
    const std::string &                                          wid,
    std::function<void(std::vector<OnshaperPart>, std::string)> callback)
{
    const std::string path         = "/api/v6/documents/d/" + did + "/w/" + wid + "/elements";
    const std::string query        = "elementType=PARTSTUDIO";
    const std::string content_type = "";

    std::string nonce = make_nonce();
    std::string date  = make_date();
    std::string auth  = build_auth_header(creds, "GET", path, query, content_type, nonce, date);

    Http::get(BASE_URL + path + "?" + query)
        .header("Authorization", auth)
        .header("Date", date)
        .header("On-Nonce", nonce)
        .header("Accept", "application/json")
        .on_complete([did, wid, callback](std::string body, unsigned) {
            try {
                auto                    j = nlohmann::json::parse(body);
                std::vector<OnshaperPart> parts;
                for (auto &item : j) {
                    OnshaperPart part;
                    part.did  = did;
                    part.wid  = wid;
                    part.eid  = item.value("id", "");
                    part.name = item.value("name", "");
                    if (!part.eid.empty())
                        parts.push_back(std::move(part));
                }
                callback(std::move(parts), {});
            } catch (const std::exception &e) {
                callback({}, std::string("JSON parse error: ") + e.what());
            }
        })
        .on_error([callback](std::string, std::string error, unsigned status) {
            callback({}, error.empty() ? "HTTP " + std::to_string(status) : error);
        })
        .perform();
}

void OnshapeClient::export_stl(const OnshapeCredentials &                            creds,
                                const OnshaperPart &                                   part,
                                std::function<void(std::string, std::string)> callback)
{
    const std::string path =
        "/api/v6/partstudios/d/" + part.did + "/w/" + part.wid + "/e/" + part.eid + "/stl";
    const std::string query        = "mode=binary&units=millimeter";
    const std::string content_type = "";

    std::string nonce = make_nonce();
    std::string date  = make_date();
    std::string auth  = build_auth_header(creds, "GET", path, query, content_type, nonce, date);

    Http::get(BASE_URL + path + "?" + query)
        .header("Authorization", auth)
        .header("Date", date)
        .header("On-Nonce", nonce)
        .size_limit(100 * 1024 * 1024)
        .on_complete([callback](std::string body, unsigned) { callback(std::move(body), {}); })
        .on_error([callback](std::string, std::string error, unsigned status) {
            callback({}, error.empty() ? "HTTP " + std::to_string(status) : error);
        })
        .perform();
}

void OnshapeClient::upload_blob(const OnshapeCredentials &                     creds,
                                 const std::string &                             did,
                                 const std::string &                             wid,
                                 const std::string &                             filename,
                                 const boost::filesystem::path &                 file_path,
                                 std::function<void(bool, std::string)> callback)
{
    const std::string path         = "/api/v6/blobelements/d/" + did + "/w/" + wid;
    const std::string query        = "";
    const std::string content_type = "multipart/form-data";

    std::string nonce = make_nonce();
    std::string date  = make_date();
    std::string auth =
        build_auth_header(creds, "POST", path, query, content_type, nonce, date);

    Http::post(BASE_URL + path)
        .header("Authorization", auth)
        .header("Date", date)
        .header("On-Nonce", nonce)
        .form_add_file("file", file_path, filename)
        .on_complete([callback](std::string, unsigned) { callback(true, {}); })
        .on_error([callback](std::string, std::string error, unsigned status) {
            callback(false, error.empty() ? "HTTP " + std::to_string(status) : error);
        })
        .perform();
}

} // namespace Slic3r
