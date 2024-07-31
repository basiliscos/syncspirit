#include "../static_table.h"

#include <FL/Fl_Input.H>
#include <string>

namespace syncspirit::fltk::table_widget {

struct path_t : widgetable_t {
    using parent_t = widgetable_t;

    path_t(Fl_Widget &container, std::string title);

    Fl_Widget *create_widget(int x, int y, int w, int h) override;
    void on_click();

    Fl_Input *input = nullptr;
    std::string title;
};

} // namespace syncspirit::fltk::table_widget
