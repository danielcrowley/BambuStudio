#include <catch2/catch.hpp>
#include <nlohmann/json.hpp>
#include <vector>
#include <string>

// Fixture: what the OnShape /api/documents API returns
static const std::string DOCS_RESPONSE = R"({
  "items": [
    {"id":"doc1","defaultWorkspace":{"id":"ws1"},"name":"My Project","modifiedAt":"2024-01-10T12:00:00Z"},
    {"id":"doc2","defaultWorkspace":{"id":"ws2"},"name":"Another Doc","modifiedAt":"2024-01-09T08:00:00Z"}
  ]
})";

// Fixture: what /api/parts/d/{did}/w/{wid} returns
static const std::string PARTS_RESPONSE = R"([
  {"partId":"JHD","name":"Bracket v3","bodyType":"solid","elementId":"el1","modifiedAt":"2024-01-10T11:00:00Z"},
  {"partId":"KSQ","name":"Lid","bodyType":"solid","elementId":"el1","modifiedAt":"2024-01-09T07:00:00Z"},
  {"partId":"MEH","name":"Surface Part","bodyType":"surface","elementId":"el1","modifiedAt":"2024-01-10T10:00:00Z"}
])";

TEST_CASE("parse OnShape document list", "[onshape][json]") {
    auto j = nlohmann::json::parse(DOCS_RESPONSE);
    REQUIRE(j["items"].size() == 2);
    REQUIRE(j["items"][0]["id"] == "doc1");
    REQUIRE(j["items"][0]["defaultWorkspace"]["id"] == "ws1");
    REQUIRE(j["items"][1]["id"] == "doc2");
}

TEST_CASE("parse OnShape parts list — filters non-solid bodies", "[onshape][json]") {
    auto j = nlohmann::json::parse(PARTS_RESPONSE);
    REQUIRE(j.size() == 3);

    // Simulate the bodyType=="solid" filter used by fetchRecentParts
    std::vector<std::string> solid_part_ids;
    for (auto& p : j) {
        if (p.value("bodyType", "") == "solid") {
            solid_part_ids.push_back(p["partId"].get<std::string>());
        }
    }
    REQUIRE(solid_part_ids.size() == 2);
    REQUIRE(solid_part_ids[0] == "JHD");
    REQUIRE(solid_part_ids[1] == "KSQ");
}

TEST_CASE("parse OnShape part fields", "[onshape][json]") {
    auto j = nlohmann::json::parse(PARTS_RESPONSE);
    REQUIRE(j[0]["partId"]    == "JHD");
    REQUIRE(j[0]["name"]      == "Bracket v3");
    REQUIRE(j[0]["elementId"] == "el1");
    REQUIRE(j[0]["modifiedAt"] == "2024-01-10T11:00:00Z");
}

TEST_CASE("top-5 sorting by modifiedAt", "[onshape][json]") {
    // Simulate the sort used in fetchRecentParts
    struct Part { std::string modified_at; std::string name; };
    std::vector<Part> parts = {
        {"2024-01-09T07:00:00Z", "Old"},
        {"2024-01-10T11:00:00Z", "Newer"},
        {"2024-01-10T12:00:00Z", "Newest"},
    };
    std::sort(parts.begin(), parts.end(), [](const Part& a, const Part& b){
        return a.modified_at > b.modified_at;
    });
    REQUIRE(parts[0].name == "Newest");
    REQUIRE(parts[1].name == "Newer");
    REQUIRE(parts[2].name == "Old");
}
