# OnShape Integration Design

**Date:** 2026-03-26
**Status:** Approved

## Summary

Add OnShape CAD integration to BambuStudio's Prepare screen. Users can import recently modified OnShape parts directly onto the build plate, reload updated geometry in-place, and automatically upload the printed 3MF back to the originating OnShape document.

---

## Features

### 1. Import from OnShape (split button)
The existing "Add Part" button becomes a composite split button (two adjacent `wxBitmapButton` controls — a wide action button and a narrow dropdown arrow button — since `wxSplitButton` is Windows-only and not used elsewhere in the codebase):
- **Left side (action button)** — executes the last-used action. Default: "From disk" (opens file dialog, same as current behaviour). After first successful OnShape import the default switches to "From OnShape". The last-used choice is persisted in `AppConfig` under key `onshape_add_part_last_source` (values: `"disk"` | `"onshape"`, default `"disk"`). `Ctrl+I` continues to trigger the left-side default action — no key-binding change.
- **Right side (dropdown arrow)** — reveals a small menu: "From disk…" and "From OnShape…"

"From OnShape…" opens the `OnShapePartPicker` popover showing up to 5 recently modified parts. Selecting a part exports it as 3MF via the OnShape API and loads it onto the build plate.

### 2. Reload from OnShape (right-click context menu)
A new "Reload from OnShape" item appears below "Reload from disk" in the part right-click menu. `can_reload_from_onshape()` returns true only when **exactly one** object is selected and that object has OnShape metadata stored on it. Activating it calls `TakeSnapshot` (matching the behaviour of `reload_from_disk()`), then re-fetches the same part's geometry from OnShape and swaps it in-place, preserving position, rotation, scale, and all other object settings.

### 3. Post-print 3MF upload
When the user clicks Print, after the **whole-project** 3MF is saved (not a per-plate file), BambuStudio scans all objects across all plates for OnShape metadata. For each unique `docId` found, it uploads the saved project 3MF as a blob element on that OnShape document. This runs async with a 30-second timeout; a toast reports success or failure without blocking the print. If objects from multiple OnShape documents exist in the scene, the same 3MF file is uploaded to each of those documents.

---

## Architecture

### New Files

| File | Purpose |
|------|---------|
| `src/slic3r/Utils/OnShape.hpp/.cpp` | OnShape API client — HTTP Basic auth, fetch recent parts, export part 3MF, upload blob attachment |
| `src/slic3r/GUI/OnShapePartPicker.hpp/.cpp` | Composite wxWidgets popover listing up to 5 recent parts |

### Modified Files

| File | Changes |
|------|---------|
| `src/slic3r/GUI/MainFrame.cpp` | Convert Add Part button to composite split button; `Ctrl+I` triggers left-side default action (unchanged semantically) |
| `src/slic3r/GUI/GUI_Factories.cpp` | Add `append_menu_item_reload_from_onshape()` below reload-from-disk |
| `src/slic3r/GUI/Plater.cpp/.hpp` | Add `reload_from_onshape()`, `can_reload_from_onshape()`, post-print upload hook |
| `src/libslic3r/Format/bbs_3mf.cpp/.hpp` | Read/write OnShape metadata per object in custom XML namespace |
| Preferences dialog | New "OnShape" section with Access Key and Secret Key fields (both stored as plaintext in `AppConfig`) |

---

## Authentication

OnShape API key authentication uses **HTTP Basic auth**: the Access Key is the username and the Secret Key is the password, sent as:

```
Authorization: Basic base64(<accessKey>:<secretKey>)
```

Both values are stored as plaintext in `AppConfig` under keys `onshape_access_key` and `onshape_secret_key`. The Secret Key field in Preferences is displayed masked (`wxTextCtrl` with `wxTE_PASSWORD` style). BambuStudio has no OS keychain integration; plaintext `AppConfig` storage matches how other credentials are handled in the codebase.

If either key is empty, the part picker shows an inline message: "Add your OnShape API key in Preferences → OnShape to use this feature."

---

## Data Flow

### Import
1. User clicks dropdown arrow → `OnShapePartPicker` opens
2. `OnShape::fetchRecentParts()` — fetches up to 5 most recently modified documents (using `/api/documents?filter=6&limit=5`). For each document, fetches parts via `/api/parts/d/{did}/w/{wid}`. Parts from documents that have no parts are skipped (the document still counts against the 5-document limit). Collects all parts found, returns up to 5 by last-modified date as a flat list. The picker displays however many parts were found (0–5) with no pagination. If zero parts are found across all fetched documents, the picker shows the empty-state message: "No parts found in your recently modified OnShape documents."
3. Only documents accessible via a **main workspace** (`/w/{wid}`) are supported. Versioned or microversioned documents are skipped silently in the fetch step.
4. User selects a part → `OnShape::exportPart(docId, workspaceId, elementId, partId)` calls `POST /api/partstudios/d/{did}/w/{wid}/e/{eid}/partexport` with `{"partIds": [partId], "format": "STL"}`. The STL response is written to a temp file and converted to 3MF in-process using `Slic3r::load_stl()` followed by `Slic3r::store_3mf()` (the same path used elsewhere in BambuStudio for STL→3MF conversion). The resulting 3MF temp file is then passed to step 5.
5. Temp file loaded via existing `Plater::load_files()`
6. OnShape metadata (`docId`, `workspaceId`, `elementId`, `partId`, `partName`) stored on the `ModelObject` and written to 3MF custom XML namespace (`onshape:source`)

### Reload from OnShape
1. Read OnShape metadata from selected `ModelObject`
2. Call `Plater::TakeSnapshot` (matching `reload_from_disk` behaviour)
3. `OnShape::exportPart()` for the same IDs
4. Replace mesh volumes in-place — position, rotation, scale, and all other settings unchanged

### Post-print Upload
1. Print clicked → existing whole-project 3MF save runs
2. All objects scanned for OnShape metadata; unique `(docId, workspaceId)` pairs collected (both values read from each object's stored metadata)
3. For each unique `(docId, workspaceId)` pair: `OnShape::uploadAttachment(docId, workspaceId, savedFilePath)` — two-step blob upload:
   - `POST /api/blobelements/d/{did}/w/{wid}` (using the stored `workspaceId`) to create the blob element and receive a signed upload URL
   - `PUT <signed-url>` with the 3MF file bytes
4. Runs async with 30-second timeout per document; toast on completion/failure; never blocks print

---

## OnShape API Endpoints Used

| Endpoint | Purpose |
|----------|---------|
| `GET /api/documents?filter=6&limit=5` | 5 most recently modified documents |
| `GET /api/parts/d/{did}/w/{wid}` | List parts in a document's main workspace |
| `POST /api/partstudios/d/{did}/w/{wid}/e/{eid}/partexport` | Export a single part by `partId` (body: `{"partIds": [...], "format": "STL"}`) |
| `POST /api/blobelements/d/{did}/w/{wid}` | Create blob element, receive signed upload URL |
| `PUT <signed-url>` | Upload 3MF file bytes to the blob element |

---

## 3MF Metadata Schema

Per-object metadata stored in the 3MF's custom XML namespace:

```xml
<onshape:source
  docId="..."
  workspaceId="..."
  elementId="..."
  partId="..."
  partName="..." />
```

---

## Error Handling

| Scenario | Behaviour |
|----------|-----------|
| No API key configured | Picker shows inline message with link to Preferences |
| Network failure on fetch | Picker shows "Couldn't reach OnShape — check your connection" + retry button |
| Export in progress | Spinner shown; import button disabled to prevent double-import |
| Reload on non-OnShape part or multi-selection | `can_reload_from_onshape()` returns false → menu item greyed out |
| Post-print upload failure or 30s timeout | Toast notification; print not blocked; no auto-retry |
| Part deleted/moved in OnShape | Error dialog: "This part no longer exists in OnShape at its original location" |
| Document is versioned (no workspace ID) | Skipped silently during fetch; not shown in picker |

---

## Scope Limitations

- Only main-workspace OnShape documents are supported (not versions or branches)
- `wxSplitButton` is not used — composite widget (two `wxBitmapButton` controls) used instead to maintain cross-platform compatibility
- Credentials stored as plaintext in `AppConfig` (no OS keychain integration)
- Post-print upload sends the whole-project 3MF regardless of which plate is being printed
