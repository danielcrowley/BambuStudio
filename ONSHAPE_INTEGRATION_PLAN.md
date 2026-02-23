# Plan: Onshape Parts Integration for BambuStudio

## Context
Users who design parts in Onshape (a cloud-based CAD tool) currently need to manually export their models and import them into BambuStudio. This integration adds a first-class "Add from Onshape" workflow that:
1. Lets users browse recent Onshape documents and select a part to import
2. Automatically exports it as STL and loads it into the prepare view
3. Uploads the BambuStudio project 3MF back to the same Onshape document when the print job is sent to the printer

Authentication uses Onshape API key pairs (Access Key + Secret Key) generated at https://dev-portal.onshape.com/keys — no OAuth app registration required.

---

## New Files

### `src/slic3r/Utils/OnshapeAPI.hpp` / `.cpp`
Onshape REST API client with HMAC-SHA256 request signing.

**Structs:**
```cpp
struct OnshapeCredentials { std::string access_key, secret_key; };
struct OnshapeDocument { std::string id, wid, name; };
struct OnshaperPart   { std::string did, wid, eid, name; };
```

**Class `OnshapeClient`:**
- `static std::string sign_request(const OnshapeCredentials&, const std::string& method, const std::string& path, const std::string& query, const std::string& content_type, const std::string& nonce, const std::string& date)` — builds the `Authorization: On {key}:HmacSHA256:{base64(HMAC)}` header using OpenSSL `HMAC()` (already linked via `OpenSSL::Crypto`)
- `static void get_recent_documents(const OnshapeCredentials&, std::function<void(std::vector<OnshapeDocument>, std::string error)> callback)` — `GET /api/v6/globaltreenodes/magic/recentlytouched?offset=0&limit=20`
- `static void get_part_studios(const OnshapeCredentials&, const std::string& did, const std::string& wid, std::function<void(std::vector<OnshaperPart>, std::string)> callback)` — `GET /api/v6/documents/d/{did}/w/{wid}/elements?elementType=PARTSTUDIO`
- `static void export_stl(const OnshapeCredentials&, const OnshaperPart&, std::function<void(std::string stl_bytes, std::string error)> callback)` — `GET /api/v6/partstudios/d/{did}/w/{wid}/e/{eid}/stl?mode=binary`
- `static void upload_blob(const OnshapeCredentials&, const std::string& did, const std::string& wid, const std::string& filename, const boost::filesystem::path& file_path, std::function<void(bool ok, std::string error)> callback)` — `POST /api/v6/blobelements/d/{did}/w/{wid}` with multipart form data

All network calls run on background threads via `Http` (already in `src/slic3r/Utils/Http.hpp`). Base URL: `https://cad.onshape.com`.

---

### `src/slic3r/GUI/OnshapeDialog.hpp` / `.cpp`
Two dialog classes.

#### `class OnshapeSettingsDialog : public DPIDialog`
Simple settings form (matches pattern of other dialogs, e.g. `DownloadProgressDialog`):
- `wxTextCtrl* m_access_key_ctrl`
- `wxTextCtrl* m_secret_key_ctrl` (password style)
- "Save" / "Cancel" buttons
- On Save: writes to `wxGetApp().app_config->set("onshape_access_key", ...)` and `"onshape_secret_key"`
- On construction: pre-fills from app_config

#### `class OnshapeDialog : public DPIDialog`
Main browse dialog:
- Title bar with gear ⚙ settings button (top-right)
- `wxListCtrl` (report mode, 2 columns: Name / Last Modified) showing recent documents
- "Refresh" reloads the list via `OnshapeClient::get_recent_documents()`
- Expanding a document row (double-click or expand) loads its part studios via `get_part_studios()`
- Alternatively, keep it simpler: flat list of `"{document} — {part studio name}"` entries
- "Import" button: calls `OnshapeClient::export_stl()`, writes bytes to a temp file `%TEMP%/onshape_{name}.stl`, then calls `wxGetApp().plater()->load_files({tmp_path})` and stores `OnshaperPart` in `Plater::set_onshape_source()`
- "Cancel" closes without importing
- Shows a `wxGauge` progress indicator while loading/downloading
- Credentials read from app_config; if empty, shows settings dialog first

---

## Modified Files

### 1. `src/slic3r/GUI/GLToolbar.hpp`
Add after existing `EVT_GLTOOLBAR_ADD` declaration (line ~39):
```cpp
wxDECLARE_EVENT(EVT_GLTOOLBAR_ADD_FROM_ONSHAPE, SimpleEvent);
```

### 2. `src/slic3r/GUI/GLToolbar.cpp`
Add after `wxDEFINE_EVENT(EVT_GLTOOLBAR_ADD, SimpleEvent);` (line ~38):
```cpp
wxDEFINE_EVENT(EVT_GLTOOLBAR_ADD_FROM_ONSHAPE, SimpleEvent);
```

### 3. `src/slic3r/GUI/GLCanvas3D.cpp` — `_init_main_toolbar()` (line ~6999)
After the existing "add" item block (lines 6992–7002), insert a new toolbar item:
```cpp
item.name = "add_from_onshape";
item.icon_filename_callback = [](bool is_dark_mode) -> std::string {
    return is_dark_mode ? "toolbar_onshape_dark.svg" : "toolbar_onshape.svg";
};
item.tooltip = _utf8(L("Add from Onshape"));
item.sprite_id = sprite_id++;
item.left.action_callback = [this]() {
    if (m_canvas != nullptr)
        wxPostEvent(m_canvas, SimpleEvent(EVT_GLTOOLBAR_ADD_FROM_ONSHAPE));
};
item.enabling_callback = []() -> bool { return wxGetApp().plater()->can_add_model(); };
if (!p_main_toolbar->add_item(item))
    return false;
```
Also create `resources/images/toolbar_onshape.svg` and `toolbar_onshape_dark.svg` — simple SVG using the Onshape "O" mark or a generic cloud+import icon (can reuse `toolbar_open.svg` shape with different color initially).

### 4. `src/slic3r/GUI/Plater.hpp`
Add public declarations after `add_file()` (line ~339):
```cpp
void add_from_onshape();

struct OnshapeSource { std::string did, wid, eid, name; };
void set_onshape_source(const OnshapeSource& src);
std::optional<OnshapeSource> get_onshape_source() const;
void clear_onshape_source();
```

### 5. `src/slic3r/GUI/Plater.cpp`
**In `Plater::priv` struct** — add field:
```cpp
std::optional<Plater::OnshapeSource> m_onshape_source;
```

**Add handler in `priv`:**
```cpp
void on_action_add_from_onshape(SimpleEvent&) {
    if (q != nullptr) q->add_from_onshape();
}
```

**Event binding** (near existing `EVT_GLTOOLBAR_ADD` binding):
```cpp
q->Bind(EVT_GLTOOLBAR_ADD_FROM_ONSHAPE, &priv::on_action_add_from_onshape, this);
```

**New public methods on `Plater`:**
```cpp
void Plater::add_from_onshape() {
    OnshapeDialog dlg(this);
    dlg.ShowModal();
    // OnshapeDialog internally calls load_files() and set_onshape_source()
}

void Plater::set_onshape_source(const OnshapeSource& src) { p->m_onshape_source = src; }
std::optional<Plater::OnshapeSource> Plater::get_onshape_source() const { return p->m_onshape_source; }
void Plater::clear_onshape_source() { p->m_onshape_source.reset(); }
```

**Clear source on new project / open project** — call `clear_onshape_source()` in `Plater::new_project()` and `Plater::load_project()`.

### 6. `src/slic3r/GUI/SelectMachine.cpp` — `on_send_print()` (line ~2470)
After the successful `send_gcode()` + `export_config_3mf()` path (after line 2483), add:
```cpp
// Onshape back-upload: if this project was imported from Onshape, upload the 3mf
if (auto src = m_plater->get_onshape_source()) {
    // Read credentials
    OnshapeCredentials creds;
    creds.access_key = wxGetApp().app_config->get("onshape_access_key");
    creds.secret_key = wxGetApp().app_config->get("onshape_secret_key");
    if (!creds.access_key.empty() && !creds.secret_key.empty()) {
        // Get the exported 3mf path from the plater
        auto project_path = m_plater->get_export_gcode_filename(m_print_plate_idx);
        std::string upload_name = src->name + "_print.3mf";
        // Run async so it doesn't block the send flow
        std::thread([creds, src = *src, project_path, upload_name]() {
            OnshapeClient::upload_blob(creds, src.did, src.wid, upload_name, project_path,
                [](bool ok, std::string err) {
                    if (!ok)
                        BOOST_LOG_TRIVIAL(warning) << "Onshape back-upload failed: " << err;
                });
        }).detach();
    }
}
```
Need to add `#include "OnshapeDialog.hpp"` and `#include "../Utils/OnshapeAPI.hpp"` at top of SelectMachine.cpp.

Also need to expose the exported gcode path — check `m_plater->get_gcode_path(m_print_plate_idx)` or use the temp path set during `send_gcode()`. If not directly available, read the temp file that `send_gcode()` already wrote (it's cached in `Plater::priv::m_gcode_result`). Alternatively look at `export_config_3mf` return path or use the `.gcode.3mf` that was already exported.

### 7. `src/slic3r/CMakeLists.txt`
In the existing `set(SLIC3R_GUI_SOURCES ...)` block (around line 386), add:
```cmake
GUI/OnshapeDialog.hpp
GUI/OnshapeDialog.cpp
```

In the `set(SLIC3R_UTILS_SOURCES ...)` block (or wherever `Http.cpp` is listed), add:
```cmake
../Utils/OnshapeAPI.hpp
../Utils/OnshapeAPI.cpp
```

### 8. Icon SVGs
Create `resources/images/toolbar_onshape.svg` and `toolbar_onshape_dark.svg`:
- Simple cloud with an upward arrow or the letter "O" in Onshape blue (#5E6AD2)
- Same dimensions as other toolbar SVGs (e.g. `toolbar_open.svg` — 22×22 px viewBox)

---

## Settings Flow
1. User opens OnshapeDialog for the first time
2. If `app_config` keys `onshape_access_key`/`onshape_secret_key` are empty → automatically open OnshapeSettingsDialog
3. User enters keys from https://dev-portal.onshape.com/keys, clicks Save
4. Dialog loads recent documents list
5. Settings gear button in OnshapeDialog allows reconfiguring credentials any time

---

## Data Flow Summary
```
User clicks "Add from Onshape" in toolbar
  → EVT_GLTOOLBAR_ADD_FROM_ONSHAPE
  → Plater::add_from_onshape()
  → OnshapeDialog::ShowModal()
  → OnshapeClient::get_recent_documents() [background thread, Http GET]
  → User selects part → clicks Import
  → OnshapeClient::export_stl() [background thread, Http GET]
  → write to /tmp/onshape_{name}.stl
  → Plater::load_files({tmp_stl})   ← loads model into 3D view
  → Plater::set_onshape_source({did, wid, eid, name})
  → Dialog closes

User slices and clicks Send to Printer
  → SelectMachineDialog::on_send_print()
  → m_plater->send_gcode() + export_config_3mf()  [existing flow]
  → OnshapeClient::upload_blob() [detached background thread]
  → Uploads .3mf to Onshape document as new blob element
```

---

## Build Notes
The BambuStudio build requires a multi-step process. There is no pre-configured build in this environment:
1. Build vendored deps: `cmake -S deps -B deps/build -G Ninja && cmake --build deps/build`
2. Configure main project: `cmake -S . -B build -DCMAKE_PREFIX_PATH=deps/build/destdir/usr/local -DSLIC3R_STATIC=1`
3. Build: `cmake --build build -- -j$(nproc)`

All new code follows exact patterns from existing codebase files, verifiable by inspection.

## Verification / Testing

1. **Build**: Follow steps above — should compile without errors
2. **Credentials**: Open BambuStudio → click "Add from Onshape" → settings dialog appears → enter API key from https://dev-portal.onshape.com/keys → Save
3. **Import**: Recent documents list populates → select a document with a part studio → click Import → STL loads into prepare view
4. **Back-upload**: Slice the model → Send to Printer → after send succeeds, check Onshape document for a new blob element named `{part}_print.3mf`
5. **No credentials**: With empty keys, dialog should show settings dialog automatically rather than failing silently
6. **Error handling**: If Onshape API returns 401 (bad credentials) or 404, dialog should show readable error message in a `wxMessageBox`
