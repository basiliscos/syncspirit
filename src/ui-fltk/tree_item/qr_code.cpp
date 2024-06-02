#include "qr_code.h"
#include "../qr_button.h"

using namespace syncspirit::fltk::tree_item;

qr_code_t::qr_code_t(app_supervisor_t &supervisor, Fl_Tree *tree) : parent_t(supervisor, tree) { label("QR code"); }

void qr_code_t::set_device(model::device_ptr_t device_) { device = std::move(device_); }

void qr_code_t::on_select() {
    if (device) {
        supervisor.replace_content([&](Fl_Widget *prev) -> Fl_Widget * {
            return new qr_button_t(device, supervisor, prev->x(), prev->y(), prev->w(), prev->h());
        });
    }
}
