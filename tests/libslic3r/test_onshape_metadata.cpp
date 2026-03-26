#include <catch2/catch.hpp>
#include "libslic3r/Model.hpp"
#include "libslic3r/Format/bbs_3mf.hpp"
#include "libslic3r/Format/STL.hpp"
#include <boost/filesystem.hpp>

using namespace Slic3r;
namespace fs = boost::filesystem;

SCENARIO("OnShape metadata survives 3MF roundtrip", "[onshape][3mf]") {
    GIVEN("a model with OnShape metadata on one object") {
        Model src_model;
        std::string stl_path = std::string(TEST_DATA_DIR) + "/test_3mf/Prusa.stl";
        load_stl(stl_path.c_str(), &src_model);
        src_model.add_default_instances();

        OnShapeMetadata meta;
        meta.doc_id       = "doc123";
        meta.workspace_id = "ws456";
        meta.element_id   = "el789";
        meta.part_id      = "part001";
        meta.part_name    = "Bracket v3";
        src_model.objects.front()->onshape_source = meta;

        WHEN("the model is saved and reloaded as 3MF") {
            fs::path tmp = fs::temp_directory_path() / "test_onshape.3mf";

            StoreParams store_params;
            store_params.path = tmp.string().c_str();
            store_params.model = &src_model;
            DynamicPrintConfig config;
            store_params.config = &config;
            store_params.strategy = SaveStrategy::Zip64;
            bool save_ok = store_bbs_3mf(store_params);
            REQUIRE(save_ok);

            Model loaded_model;
            DynamicPrintConfig loaded_config;
            ConfigSubstitutionContext ctxt{ForwardCompatibilitySubstitutionRule::Disable};
            bool is_bbl_3mf = false;
            Semver file_version;
            bool load_ok = load_bbs_3mf(tmp.string().c_str(), &loaded_config, &ctxt,
                                        &loaded_model, nullptr, nullptr,
                                        &is_bbl_3mf, &file_version);
            REQUIRE(load_ok);
            REQUIRE(!loaded_model.objects.empty());

            THEN("OnShape metadata is preserved") {
                auto& obj = loaded_model.objects.front();
                REQUIRE(obj->onshape_source.has_value());
                REQUIRE(obj->onshape_source->doc_id       == "doc123");
                REQUIRE(obj->onshape_source->workspace_id == "ws456");
                REQUIRE(obj->onshape_source->element_id   == "el789");
                REQUIRE(obj->onshape_source->part_id      == "part001");
                REQUIRE(obj->onshape_source->part_name    == "Bracket v3");
            }

            fs::remove(tmp);
        }
    }

    GIVEN("a model without OnShape metadata") {
        Model src_model;
        std::string stl_path = std::string(TEST_DATA_DIR) + "/test_3mf/Prusa.stl";
        load_stl(stl_path.c_str(), &src_model);
        src_model.add_default_instances();

        WHEN("saved and reloaded") {
            fs::path tmp = fs::temp_directory_path() / "test_no_onshape.3mf";

            StoreParams store_params;
            store_params.path = tmp.string().c_str();
            store_params.model = &src_model;
            DynamicPrintConfig config;
            store_params.config = &config;
            store_params.strategy = SaveStrategy::Zip64;
            bool save_ok2 = store_bbs_3mf(store_params);
            REQUIRE(save_ok2);

            Model loaded_model;
            DynamicPrintConfig loaded_config;
            ConfigSubstitutionContext ctxt{ForwardCompatibilitySubstitutionRule::Disable};
            bool is_bbl_3mf = false;
            Semver file_version;
            bool load_ok2 = load_bbs_3mf(tmp.string().c_str(), &loaded_config, &ctxt,
                                         &loaded_model, nullptr, nullptr,
                                         &is_bbl_3mf, &file_version);
            REQUIRE(load_ok2);
            REQUIRE(!loaded_model.objects.empty());

            THEN("onshape_source is empty") {
                REQUIRE(!loaded_model.objects.front()->onshape_source.has_value());
            }

            fs::remove(tmp);
        }
    }
}
