#include "qr_code.h"

#include <memory>
#include <system_error>
#include <algorithm>
#include <vector>

#include <qrencode.h>
#include <boost/dynamic_bitset.hpp>

#include <FL/Fl.H>
#include <FL/Fl_Group.H>
#include <FL/Fl_Box.H>
#include <FL/Fl_Button.H>
#include <FL/Fl_Bitmap.H>

using namespace syncspirit::fltk::tree_item;

static constexpr int PADDING = 30;

template <typename T> using guard_t = std::unique_ptr<T, std::function<void(T *)>>;

template <typename T, typename G> guard_t<T> make_guard(T *ptr, G &&fn) {
    return guard_t<T>{ptr, [fn = std::move(fn)](T *it) { fn(it); }};
}

using code_t = guard_t<QRcode>;

namespace {

struct box_t : Fl_Box {
    using image_t = std::unique_ptr<Fl_Image>;
    using bits_t = std::vector<unsigned char>;
    using bit_set_t = boost::dynamic_bitset<unsigned char>;
    using parent_t = Fl_Box;

    box_t(code_t code_, int x, int y, int w, int h, const char *label)
        : parent_t(x, y, w, h, label), code{std::move(code_)}, scale{0} {
        box(FL_ENGRAVED_BOX);
        regen_image(w, h);
    }

    void regen_image(int w, int h) {
        auto min_w = std::min(w - PADDING, h - PADDING);
        min_w = std::max(code->width, min_w);
        auto new_scale = min_w / code->width;
        if (new_scale == scale) {
            return;
        }

        auto img_w = code->width * new_scale;
        auto extra_w = img_w % 8;
        auto line_w = img_w + (extra_w ? (8 - extra_w) : 0);
        bit_set_t bit_set(std::size_t(img_w * line_w));
        // logger->debug("qr code v{}, width = {}, scale = {}", code->version, code->width, scale);
        for (int i = 0; i < code->width; ++i) {
            for (int j = 0; j < code->width; ++j) {
                auto bit = code->data[i * code->width + j] & 1;
                for (int y = i * new_scale; y < (i + 1) * new_scale; ++y) {
                    for (int x = j * new_scale; x < (j + 1) * new_scale; ++x) {
                        bit_set.set(y * line_w + x, bit);
                    }
                }
            }
        }

        bits.clear();
        bits.reserve(bit_set.num_blocks());
        boost::to_block_range(bit_set, std::back_insert_iterator(bits));
        qr_image.reset(new Fl_Bitmap(bits.data(), img_w, img_w));
        image(qr_image.get());
        scale = new_scale;
    }

    void resize(int x, int y, int w, int h) override {
        regen_image(w, h);
        parent_t::resize(x, y, w, h);
    }

    code_t code;
    bits_t bits;
    image_t qr_image;
    int scale = 0;
};

struct my_container_t : Fl_Group {
    using parent_t = Fl_Group;
    using supervisor_t = syncspirit::fltk::app_supervisor_t;
    my_container_t(code_t code_, supervisor_t *sup_, int x, int y, int w, int h, const char *label)
        : parent_t(x, y, w, h), sup{sup_} {
        auto box = new box_t(std::move(code_), x, y, w, h - 30, nullptr);
        auto button = new Fl_Button(x, y + box->h(), w, 30, label);
        button->tooltip("copy to buffer");
        resizable(box);
        button->callback(
            [](Fl_Widget *widget, void *data) {
                auto button = static_cast<Fl_Button *>(widget);
                auto device_id = button->label();
                Fl::copy(device_id, strlen(device_id), 1);
                auto sup = reinterpret_cast<supervisor_t *>(data);
                auto device_id_short = sup->get_cluster()->get_device()->device_id().get_short();
                sup->get_logger()->info("device id {} has been copied to clipboard", device_id_short);
            },
            sup);
    }

    supervisor_t *sup;
};

} // namespace

qr_code_t::qr_code_t(app_supervisor_t &supervisor, Fl_Tree *tree) : parent_t(supervisor, tree) { label("QR code"); }

void qr_code_t::on_select() {
    auto &cluster = supervisor.get_cluster();
    if (cluster) {
        auto device_id = cluster->get_device()->device_id().get_value().c_str();
        auto code_raw = QRcode_encodeString(device_id, 0, QR_ECLEVEL_H, QR_MODE_8, 1);
        auto &logger = supervisor.get_logger();
        if (!code_raw) {
            auto ec = std::error_code{errno, std::generic_category()};
            logger->error("cannot generate qr code ({}) : {}", ec.value(), ec.message());
            return;
        }
        auto code = make_guard(code_raw, [](auto ptr) { QRcode_free(ptr); });

        supervisor.replace_content([&](Fl_Widget *prev) -> Fl_Widget * {
            return new my_container_t(std::move(code), &supervisor, prev->x(), prev->y(), prev->w(), prev->h(),
                                      device_id);
        });
    }
}
