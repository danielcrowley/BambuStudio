#include "OnshapeDialog.hpp"

#include <thread>

#include <wx/button.h>
#include <wx/msgdlg.h>
#include <wx/sizer.h>
#include <wx/statline.h>

#include <boost/filesystem.hpp>
#include <boost/filesystem/fstream.hpp>
#include <boost/log/trivial.hpp>

#include "GUI_App.hpp"
#include "I18N.hpp"
#include "Plater.hpp"

namespace Slic3r {
namespace GUI {

// ---------------------------------------------------------------------------
// OnshapeSettingsDialog
// ---------------------------------------------------------------------------

OnshapeSettingsDialog::OnshapeSettingsDialog(wxWindow *parent)
    : DPIDialog(parent, wxID_ANY, _L("Onshape API Credentials"), wxDefaultPosition,
                wxDefaultSize, wxCAPTION | wxCLOSE_BOX)
{
    SetBackgroundColour(*wxWHITE);

    auto *sizer = new wxBoxSizer(wxVERTICAL);

    auto *grid = new wxFlexGridSizer(2, 2, FromDIP(8), FromDIP(12));
    grid->AddGrowableCol(1, 1);

    grid->Add(new wxStaticText(this, wxID_ANY, _L("Access Key:")), 0,
              wxALIGN_CENTER_VERTICAL);
    m_access_key_ctrl = new wxTextCtrl(this, wxID_ANY, wxEmptyString, wxDefaultPosition,
                                       wxSize(FromDIP(320), -1));
    grid->Add(m_access_key_ctrl, 1, wxEXPAND);

    grid->Add(new wxStaticText(this, wxID_ANY, _L("Secret Key:")), 0,
              wxALIGN_CENTER_VERTICAL);
    m_secret_key_ctrl = new wxTextCtrl(this, wxID_ANY, wxEmptyString, wxDefaultPosition,
                                       wxSize(FromDIP(320), -1), wxTE_PASSWORD);
    grid->Add(m_secret_key_ctrl, 1, wxEXPAND);

    sizer->Add(new wxStaticText(this, wxID_ANY,
                                _L("Enter your Onshape API keys from https://dev-portal.onshape.com/keys")),
               0, wxALL, FromDIP(12));
    sizer->Add(grid, 0, wxLEFT | wxRIGHT | wxBOTTOM, FromDIP(12));
    sizer->Add(new wxStaticLine(this), 0, wxEXPAND | wxLEFT | wxRIGHT, FromDIP(12));

    auto *btn_sizer = new wxBoxSizer(wxHORIZONTAL);
    auto *save_btn  = new wxButton(this, wxID_OK, _L("Save"));
    auto *cancel_btn = new wxButton(this, wxID_CANCEL, _L("Cancel"));
    btn_sizer->AddStretchSpacer();
    btn_sizer->Add(save_btn, 0, wxALL, FromDIP(6));
    btn_sizer->Add(cancel_btn, 0, wxALL, FromDIP(6));
    sizer->Add(btn_sizer, 0, wxEXPAND | wxALL, FromDIP(4));

    SetSizer(sizer);
    sizer->Fit(this);
    CentreOnParent();

    // Pre-fill from config
    auto *cfg = wxGetApp().app_config;
    m_access_key_ctrl->SetValue(wxString::FromUTF8(cfg->get("onshape_access_key")));
    m_secret_key_ctrl->SetValue(wxString::FromUTF8(cfg->get("onshape_secret_key")));

    save_btn->Bind(wxEVT_BUTTON, &OnshapeSettingsDialog::on_save, this);
}

void OnshapeSettingsDialog::on_save(wxCommandEvent &)
{
    auto *cfg = wxGetApp().app_config;
    cfg->set("onshape_access_key", m_access_key_ctrl->GetValue().ToUTF8().data());
    cfg->set("onshape_secret_key", m_secret_key_ctrl->GetValue().ToUTF8().data());
    EndModal(wxID_OK);
}

// ---------------------------------------------------------------------------
// OnshapeDialog
// ---------------------------------------------------------------------------

OnshapeDialog::OnshapeDialog(wxWindow *parent)
    : DPIDialog(parent, wxID_ANY, _L("Add from Onshape"), wxDefaultPosition,
                wxSize(FromDIP(560), FromDIP(400)), wxCAPTION | wxCLOSE_BOX | wxRESIZE_BORDER)
{
    SetBackgroundColour(*wxWHITE);

    auto *sizer = new wxBoxSizer(wxVERTICAL);

    // Toolbar row: title + settings button
    auto *top_row = new wxBoxSizer(wxHORIZONTAL);
    top_row->Add(new wxStaticText(this, wxID_ANY, _L("Recent Onshape Documents")), 1,
                 wxALIGN_CENTER_VERTICAL | wxLEFT, FromDIP(8));
    auto *refresh_btn  = new wxButton(this, wxID_ANY, _L("Refresh"), wxDefaultPosition,
                                     wxDefaultSize, wxBU_EXACTFIT);
    auto *settings_btn = new wxButton(this, wxID_ANY, wxString::FromUTF8("\xE2\x9A\x99"), // ⚙
                                      wxDefaultPosition, wxDefaultSize, wxBU_EXACTFIT);
    top_row->Add(refresh_btn, 0, wxALL, FromDIP(4));
    top_row->Add(settings_btn, 0, wxALL, FromDIP(4));
    sizer->Add(top_row, 0, wxEXPAND | wxALL, FromDIP(4));

    // List
    m_list = new wxListCtrl(this, wxID_ANY, wxDefaultPosition, wxDefaultSize,
                            wxLC_REPORT | wxLC_SINGLE_SEL | wxBORDER_SIMPLE);
    m_list->AppendColumn(_L("Name"), wxLIST_FORMAT_LEFT, FromDIP(360));
    m_list->AppendColumn(_L("Modified"), wxLIST_FORMAT_LEFT, FromDIP(160));
    sizer->Add(m_list, 1, wxEXPAND | wxLEFT | wxRIGHT, FromDIP(8));

    // Status + gauge
    m_status = new wxStaticText(this, wxID_ANY, wxEmptyString);
    m_gauge  = new wxGauge(this, wxID_ANY, 100, wxDefaultPosition, wxSize(-1, FromDIP(4)),
                          wxGA_HORIZONTAL | wxGA_SMOOTH);
    m_gauge->Hide();
    sizer->Add(m_status, 0, wxLEFT | wxTOP, FromDIP(8));
    sizer->Add(m_gauge, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, FromDIP(8));

    // Bottom buttons
    sizer->Add(new wxStaticLine(this), 0, wxEXPAND | wxLEFT | wxRIGHT, FromDIP(8));
    auto *btn_sizer = new wxBoxSizer(wxHORIZONTAL);
    btn_sizer->AddStretchSpacer();
    m_import_btn = new wxButton(this, wxID_OK, _L("Import"));
    m_import_btn->Disable();
    auto *cancel_btn = new wxButton(this, wxID_CANCEL, _L("Cancel"));
    btn_sizer->Add(m_import_btn, 0, wxALL, FromDIP(6));
    btn_sizer->Add(cancel_btn, 0, wxALL, FromDIP(6));
    sizer->Add(btn_sizer, 0, wxEXPAND | wxALL, FromDIP(4));

    SetSizer(sizer);
    Layout();
    CentreOnParent();

    // Bindings
    m_import_btn->Bind(wxEVT_BUTTON, &OnshapeDialog::on_import, this);
    settings_btn->Bind(wxEVT_BUTTON, &OnshapeDialog::on_settings, this);
    refresh_btn->Bind(wxEVT_BUTTON,
                      [this](wxCommandEvent &) { refresh_documents(); });
    m_list->Bind(wxEVT_LIST_ITEM_ACTIVATED, &OnshapeDialog::on_item_activated, this);
    m_list->Bind(wxEVT_LIST_ITEM_SELECTED, [this](wxListEvent &) {
        long idx = m_list->GetNextItem(-1, wxLIST_NEXT_ALL, wxLIST_STATE_SELECTED);
        if (idx >= 0 && idx < (long) m_entries.size())
            m_import_btn->Enable(!m_entries[idx].is_document);
    });
    m_list->Bind(wxEVT_LIST_ITEM_DESELECTED,
                 [this](wxListEvent &) { m_import_btn->Disable(); });

    // Load credentials — if missing, show settings first
    OnshapeCredentials creds = load_credentials();
    if (creds.access_key.empty() || creds.secret_key.empty()) {
        OnshapeSettingsDialog dlg(this);
        if (dlg.ShowModal() != wxID_OK) {
            // User cancelled — close this dialog too
            CallAfter([this]() { EndModal(wxID_CANCEL); });
            return;
        }
    }

    refresh_documents();
}

OnshapeCredentials OnshapeDialog::load_credentials() const
{
    OnshapeCredentials c;
    auto *             cfg = wxGetApp().app_config;
    c.access_key           = cfg->get("onshape_access_key");
    c.secret_key           = cfg->get("onshape_secret_key");
    return c;
}

void OnshapeDialog::set_status(const wxString &msg, bool busy)
{
    m_status->SetLabel(msg);
    if (busy) {
        m_gauge->Pulse();
        m_gauge->Show();
    } else {
        m_gauge->Hide();
    }
    Layout();
}

void OnshapeDialog::refresh_documents()
{
    m_list->DeleteAllItems();
    m_entries.clear();
    m_import_btn->Disable();
    set_status(_L("Loading recent documents\xe2\x80\xa6"), true); // "Loading recent documents…"

    OnshapeCredentials creds = load_credentials();

    // Run on background thread; post result back to UI thread
    std::thread([this, creds]() {
        OnshapeClient::get_recent_documents(
            creds, [this](std::vector<OnshapeDocument> docs, std::string error) {
                wxTheApp->CallAfter([this, docs = std::move(docs), error = std::move(error)]() {
                    if (!error.empty()) {
                        set_status(wxString::FromUTF8(error));
                        wxMessageBox(wxString::FromUTF8(error), _L("Onshape Error"),
                                     wxOK | wxICON_ERROR, this);
                        return;
                    }
                    m_list->DeleteAllItems();
                    m_entries.clear();
                    for (auto &doc : docs) {
                        ListEntry entry;
                        entry.is_document = true;
                        entry.doc         = doc;
                        long idx          = m_list->InsertItem(m_list->GetItemCount(),
                                                      wxString::FromUTF8(doc.name));
                        m_list->SetItem(idx, 1, wxString::FromUTF8(doc.modified_at));
                        m_entries.push_back(std::move(entry));
                    }
                    set_status(docs.empty() ? _L("No recent documents found.") : wxString());
                });
            });
    }).detach();
}

void OnshapeDialog::on_item_activated(wxListEvent &evt)
{
    long idx = evt.GetIndex();
    if (idx < 0 || idx >= (long) m_entries.size())
        return;

    if (!m_entries[idx].is_document) {
        // Double-click on a part — trigger import
        wxCommandEvent dummy;
        on_import(dummy);
        return;
    }

    // Expand the document: load its part studios
    load_parts_for(idx);
}

void OnshapeDialog::load_parts_for(long doc_index)
{
    const OnshapeDocument &doc   = m_entries[doc_index].doc;
    OnshapeCredentials     creds = load_credentials();

    set_status(wxString::Format(_L("Loading parts for \"%s\"\xe2\x80\xa6"),
                                wxString::FromUTF8(doc.name)),
               true);

    std::thread([this, doc, creds]() {
        OnshapeClient::get_part_studios(
            creds, doc.id, doc.wid,
            [this, doc](std::vector<OnshaperPart> parts, std::string error) {
                wxTheApp->CallAfter(
                    [this, doc, parts = std::move(parts), error = std::move(error)]() {
                        set_status({});
                        if (!error.empty()) {
                            wxMessageBox(wxString::FromUTF8(error), _L("Onshape Error"),
                                         wxOK | wxICON_ERROR, this);
                            return;
                        }

                        // Find insertion point: just after the document row
                        long insert_at = -1;
                        for (long i = 0; i < (long) m_entries.size(); ++i) {
                            if (m_entries[i].is_document && m_entries[i].doc.id == doc.id) {
                                insert_at = i + 1;
                                break;
                            }
                        }
                        if (insert_at < 0)
                            return;

                        for (size_t pi = 0; pi < parts.size(); ++pi) {
                            ListEntry entry;
                            entry.is_document = false;
                            entry.part        = parts[pi];

                            long row = m_list->InsertItem((long)(insert_at + pi),
                                                          "    " + wxString::FromUTF8(parts[pi].name));
                            m_list->SetItem(row, 1, {});
                            m_entries.insert(m_entries.begin() + insert_at + (long) pi,
                                             std::move(entry));
                        }

                        if (parts.empty())
                            wxMessageBox(_L("No part studios found in this document."),
                                         _L("Onshape"), wxOK | wxICON_INFORMATION, this);
                    });
            });
    }).detach();
}

void OnshapeDialog::on_import(wxCommandEvent &)
{
    long idx = m_list->GetNextItem(-1, wxLIST_NEXT_ALL, wxLIST_STATE_SELECTED);
    if (idx < 0 || idx >= (long) m_entries.size() || m_entries[idx].is_document)
        return;

    const OnshaperPart &part  = m_entries[idx].part;
    OnshapeCredentials  creds = load_credentials();

    m_import_btn->Disable();
    set_status(wxString::Format(_L("Downloading \"%s\"\xe2\x80\xa6"),
                                wxString::FromUTF8(part.name)),
               true);

    std::thread([this, part, creds]() {
        OnshapeClient::export_stl(
            creds, part, [this, part](std::string stl_bytes, std::string error) {
                wxTheApp->CallAfter([this, part, stl_bytes = std::move(stl_bytes),
                                     error = std::move(error)]() {
                    set_status({});
                    if (!error.empty()) {
                        m_import_btn->Enable();
                        wxMessageBox(wxString::FromUTF8(error), _L("Onshape Error"),
                                     wxOK | wxICON_ERROR, this);
                        return;
                    }

                    // Write STL to temp file
                    boost::filesystem::path tmp =
                        boost::filesystem::temp_directory_path() /
                        ("onshape_" + part.name + ".stl");
                    try {
                        boost::filesystem::ofstream ofs(tmp, std::ios::binary);
                        ofs.write(stl_bytes.data(), (std::streamsize) stl_bytes.size());
                    } catch (const std::exception &e) {
                        m_import_btn->Enable();
                        wxMessageBox(wxString::FromUTF8(e.what()), _L("Onshape Error"),
                                     wxOK | wxICON_ERROR, this);
                        return;
                    }

                    // Load into the plater
                    Plater *plater = wxGetApp().plater();
                    if (plater) {
                        plater->load_files({tmp});
                        Plater::OnshapeSource src;
                        src.did  = part.did;
                        src.wid  = part.wid;
                        src.eid  = part.eid;
                        src.name = part.name;
                        plater->set_onshape_source(src);
                    }

                    EndModal(wxID_OK);
                });
            });
    }).detach();
}

void OnshapeDialog::on_settings(wxCommandEvent &)
{
    OnshapeSettingsDialog dlg(this);
    dlg.ShowModal();
}

} // namespace GUI
} // namespace Slic3r
