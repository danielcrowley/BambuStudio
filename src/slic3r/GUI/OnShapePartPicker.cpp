#include "OnShapePartPicker.hpp"
#include "GUI_App.hpp"
#include "I18N.hpp"
#include <wx/stattext.h>
#include <wx/button.h>
#include <wx/sizer.h>

namespace Slic3r { namespace GUI {

OnShapePartPicker::OnShapePartPicker(wxWindow* parent,
                                     std::function<void(OnShapePart)> on_select)
    : wxPopupTransientWindow(parent, wxBORDER_SIMPLE)
    , m_on_select(std::move(on_select))
{
    m_sizer = new wxBoxSizer(wxVERTICAL);
    SetSizer(m_sizer);
    SetMinSize(wxSize(280, 40));
}

void OnShapePartPicker::ShowAt(wxPoint screen_pos)
{
    // Clear previous content
    m_sizer->Clear(true);
    m_parts.clear();

    if (!OnShape::hasCredentials()) {
        show_no_credentials();
    } else {
        show_loading();
        // Fetch parts — callback runs on HTTP worker thread, post to UI thread
        OnShape::fetchRecentParts(
            [this](std::vector<OnShapePart> parts) {
                wxGetApp().CallAfter([this, parts = std::move(parts)]() {
                    if (parts.empty())
                        show_empty();
                    else
                        show_parts(parts);
                });
            },
            [this](std::string err) {
                wxGetApp().CallAfter([this, err = std::move(err)]() {
                    show_error(err);
                });
            }
        );
    }

    Layout();
    Fit();
    Position(screen_pos, wxSize(0, 0));
    Popup();
}

void OnShapePartPicker::show_loading()
{
    m_sizer->Clear(true);
    m_sizer->Add(new wxStaticText(this, wxID_ANY, _L("Loading OnShape parts\u2026")),
                 0, wxALL, 8);
    Layout();
    Fit();
}

void OnShapePartPicker::show_no_credentials()
{
    m_sizer->Clear(true);
    m_sizer->Add(new wxStaticText(this, wxID_ANY,
        _L("Add your OnShape API key in\nPreferences \u2192 OnShape")),
        0, wxALL, 8);
    Layout();
    Fit();
}

void OnShapePartPicker::show_empty()
{
    m_sizer->Clear(true);
    m_sizer->Add(new wxStaticText(this, wxID_ANY,
        _L("No parts found in your recently\nmodified OnShape documents.")),
        0, wxALL, 8);
    Layout();
    Fit();
}

void OnShapePartPicker::show_error(const std::string& /*msg*/)
{
    m_sizer->Clear(true);
    m_sizer->Add(new wxStaticText(this, wxID_ANY,
        _L("Couldn't reach OnShape.\nCheck your connection.")),
        0, wxALL, 8);

    auto* retry = new wxButton(this, wxID_ANY, _L("Retry"));
    retry->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) {
        ShowAt(GetScreenPosition());
    });
    m_sizer->Add(retry, 0, wxALL | wxALIGN_CENTER, 4);
    Layout();
    Fit();
}

void OnShapePartPicker::show_parts(const std::vector<OnShapePart>& parts)
{
    m_parts = parts;
    m_sizer->Clear(true);

    m_sizer->Add(new wxStaticText(this, wxID_ANY, _L("Recent OnShape Parts")),
                 0, wxLEFT | wxTOP | wxRIGHT, 8);

    for (size_t i = 0; i < parts.size(); ++i) {
        auto* btn = new wxButton(this, wxID_ANY,
            wxString::FromUTF8(parts[i].part_name),
            wxDefaultPosition, wxDefaultSize, wxBU_LEFT);
        btn->Bind(wxEVT_BUTTON, [this, i](wxCommandEvent&) {
            Dismiss();
            m_on_select(m_parts[i]);
        });
        m_sizer->Add(btn, 0, wxEXPAND | wxLEFT | wxRIGHT | wxTOP, 4);
    }

    m_sizer->AddSpacer(4);
    Layout();
    Fit();
}

}} // namespace Slic3r::GUI
