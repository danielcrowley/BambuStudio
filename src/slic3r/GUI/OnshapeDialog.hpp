#pragma once

#include <string>
#include <vector>

#include <wx/dialog.h>
#include <wx/gauge.h>
#include <wx/listctrl.h>
#include <wx/stattext.h>
#include <wx/textctrl.h>

#include "GUI_Utils.hpp"
#include "slic3r/Utils/OnshapeAPI.hpp"

namespace Slic3r {
namespace GUI {

// Simple credentials dialog shown on first use or when the user clicks the settings button.
class OnshapeSettingsDialog : public DPIDialog
{
public:
    OnshapeSettingsDialog(wxWindow *parent);

    void on_dpi_changed(const wxRect &suggested_rect) override {}

private:
    wxTextCtrl *m_access_key_ctrl{nullptr};
    wxTextCtrl *m_secret_key_ctrl{nullptr};

    void on_save(wxCommandEvent &);
};

// Main browse dialog: lists recent Onshape documents, lets user pick a part studio, and imports it.
class OnshapeDialog : public DPIDialog
{
public:
    OnshapeDialog(wxWindow *parent);

    void on_dpi_changed(const wxRect &suggested_rect) override {}

private:
    wxListCtrl * m_list{nullptr};
    wxGauge *    m_gauge{nullptr};
    wxStaticText *m_status{nullptr};
    wxButton *   m_import_btn{nullptr};

    // Flat list of items shown: first level = documents, second level = part studios
    struct ListEntry {
        bool          is_document{false};
        OnshapeDocument doc;
        OnshaperPart    part;
    };
    std::vector<ListEntry> m_entries;

    OnshapeCredentials load_credentials() const;
    void               refresh_documents();
    void               load_parts_for(long doc_index);
    void               on_import(wxCommandEvent &);
    void               on_item_activated(wxListEvent &);
    void               on_settings(wxCommandEvent &);
    void               set_status(const wxString &msg, bool busy = false);
};

} // namespace GUI
} // namespace Slic3r
