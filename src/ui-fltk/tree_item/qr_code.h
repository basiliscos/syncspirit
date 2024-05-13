#pragma once

#include "../tree_item.h"
#include "FL/Fl_Widget.H"
#include <vector>

namespace syncspirit::fltk::tree_item {

struct qr_code_t : tree_item_t {
    using parent_t = tree_item_t;
    using image_t = std::unique_ptr<Fl_Image>;
    qr_code_t(app_supervisor_t &supervisor, Fl_Tree *tree);

    void on_select() override;

    using bits_t = std::vector<unsigned char>;
    bits_t bits;
    image_t image;
};

} // namespace syncspirit::fltk::tree_item
