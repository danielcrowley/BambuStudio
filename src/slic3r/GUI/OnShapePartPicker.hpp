#pragma once
#include <wx/wx.h>
#include <wx/popupwin.h>
#include <vector>
#include <functional>
#include "slic3r/Utils/OnShape.hpp"

namespace Slic3r { namespace GUI {

// A popup window shown when the user clicks "From OnShape…".
// Shows a list of up to 5 recently modified OnShape parts.
// Calls on_select when the user picks a part.
class OnShapePartPicker : public wxPopupTransientWindow {
public:
    // on_select is called on the main thread with the chosen part.
    OnShapePartPicker(wxWindow* parent,
                      std::function<void(OnShapePart)> on_select);

    // Call this to show the picker at the given screen position and kick off fetching.
    void ShowAt(wxPoint screen_pos);

private:
    void show_loading();
    void show_error(const std::string& msg);
    void show_parts(const std::vector<OnShapePart>& parts);
    void show_no_credentials();
    void show_empty();

    std::function<void(OnShapePart)> m_on_select;
    wxBoxSizer*  m_sizer    {nullptr};
    std::vector<OnShapePart> m_parts;
};

}} // namespace Slic3r::GUI
