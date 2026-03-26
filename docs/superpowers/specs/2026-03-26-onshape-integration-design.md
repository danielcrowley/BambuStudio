# OnShape Integration Design

**Date:** 2026-03-26
**Status:** Approved

## Summary

Add OnShape CAD integration to BambuStudio's Prepare screen. Users can import recently modified OnShape parts directly onto the build plate, reload updated geometry in-place, and automatically upload the printed 3MF back to the originating OnShape document.

---

## Features

### 1. Import from OnShape (split button)
The existing "Add Part" button becomes a split button:
- **Left side** — executes the last-used action (default: "From disk", switches to "From OnShape" after first OnShape import)
- **Right side** (dropdown arrow) — reveals "From disk…" and "From OnShape…"

"From OnShape…" opens the `OnShapePartPicker` popover showing the 5 most recently modified parts across the 5 most recently modified OnShape documents. Selecting a part exports it as 3MF via the OnShape API and loads it onto the build plate.

### 2. Reload from OnShape (right-click context menu)
A new "Reload from OnShape" item appears below "Reload from disk" in the part right-click menu. It is only enabled when the selected object has stored OnShape metadata. Activating it re-fetches the same part's geometry from OnShape and swaps it in-place, preserving position, rotation, scale, and all other object settings.

### 3. Post-print 3MF upload
When the user clicks Print, after the project 3MF is saved, BambuStudio scans the objects for any with OnShape metadata. For each unique OnShape document, it uploads the saved 3MF as a blob attachment on that document. This runs asynchronously — a toast notification reports success or failure without blocking the print.

---

## Architecture

### New Files

| File | Purpose |
|------|---------|
| `src/slic3r/Utils/OnShape.hpp/.cpp` | OnShape API client — HMAC auth, fetch recent parts, export 3MF, upload attachment |
| `src/slic3r/GUI/OnShapePartPicker.hpp/.cpp` | wxWidgets popover listing the 5 recent parts |

### Modified Files

| File | Changes |
|------|---------|
| `src/slic3r/GUI/MainFrame.cpp` | Convert Add Part button to split button; Ctrl+I triggers default (left-side) action |
| `src/slic3r/GUI/GUI_Factories.cpp` | Add `append_menu_item_reload_from_onshape()` below reload-from-disk |
| `src/slic3r/GUI/Plater.cpp/.hpp` | Add `reload_from_onshape()`, `can_reload_from_onshape()`, post-print upload hook |
| `src/libslic3r/Format/bbs_3mf.cpp/.hpp` | Read/write OnShape metadata per object in custom XML namespace |
| Preferences dialog | New "OnShape" section with Access Key and Secret Key fields |

---

## Authentication

OnShape API keys consist of an **Access Key** and a **Secret Key**. Every HTTP request is signed with HMAC-SHA256 over the method, path, query string, content type, and a nonce/date header. The `Authorization` header format is:

```
On <accessKey>:<nonce>:<base64-hmac-signature>
```

Both keys are stored in BambuStudio's existing credential store. The Secret Key is displayed masked in Preferences. If no key is configured, the part picker shows an inline prompt to add credentials in Preferences.

---

## Data Flow

### Import
1. User clicks dropdown → `OnShapePartPicker` opens
2. `OnShape::fetchRecentParts()` — fetches 5 most recently modified documents, collects parts from each, returns top 5 by last-modified date
3. User selects a part → `OnShape::exportPartAs3MF(docId, workspaceId, elementId, partId)` downloads to a temp file
4. Temp file loaded via existing `Plater::load_files()`
5. OnShape metadata (`docId`, `workspaceId`, `elementId`, `partId`, `partName`) stored on the `ModelObject` and written to 3MF custom XML namespace (`onshape:source`)

### Reload from OnShape
1. Read OnShape metadata from selected `ModelObject`
2. `OnShape::exportPartAs3MF()` for the same IDs
3. Replace mesh volumes in-place — position, rotation, scale, and all other settings unchanged

### Post-print Upload
1. Print clicked → existing 3MF save runs
2. Objects scanned for OnShape metadata
3. For each unique `docId`: `OnShape::uploadAttachment(docId, savedFilePath)` uploads 3MF as blob element
4. Runs async; toast on completion/failure; never blocks print

---

## OnShape API Endpoints Used

| Endpoint | Purpose |
|----------|---------|
| `GET /api/documents?filter=6&limit=5` | 5 most recently modified documents |
| `GET /api/parts/d/{did}/w/{wid}` | List parts in a document workspace |
| `POST /api/partstudios/d/{did}/w/{wid}/e/{eid}/export` | Export part as 3MF |
| Blob element upload API | Upload 3MF as file attachment to document |

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
| Network failure on fetch | Picker shows error + retry button |
| Export in progress | Spinner shown; button disabled to prevent double-import |
| Reload on non-OnShape part | "Reload from OnShape" menu item greyed out |
| Post-print upload failure | Toast notification; print not blocked; no auto-retry |
| Part deleted/moved in OnShape | Error dialog: "This part no longer exists in OnShape at its original location" |
