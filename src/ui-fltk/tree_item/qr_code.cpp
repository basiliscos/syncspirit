#include "qr_code.h"

#include <memory>
#include <system_error>
#include <algorithm>
#include <qrencode.h>
#include <boost/dynamic_bitset.hpp>

#include <FL/Fl.H>
#include <FL/Fl_Box.H>
#include <FL/Fl_Bitmap.H>

using namespace syncspirit::fltk::tree_item;

static constexpr int PADDING = 30;

template <typename T> using guard_t = std::unique_ptr<T, std::function<void(T *)>>;

template <typename T, typename G> guard_t<T> make_guard(T *ptr, G &&fn) {
    return guard_t<T>{ptr, [fn = std::move(fn)](T *it) { fn(it); }};
}

qr_code_t::qr_code_t(app_supervisor_t &supervisor, Fl_Tree *tree) : parent_t(supervisor, tree) { label("QR code"); }

void qr_code_t::on_select() {
    using bit_set_t = boost::dynamic_bitset<unsigned char>;
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
            auto min_w = std::min(prev->w() - PADDING, prev->h() - PADDING);
            min_w = std::max(code->width, min_w);
            auto scale = min_w / code->width;
            auto w = code->width * scale;
            auto extra_w = w % 8;
            auto line_w = w + (extra_w ? (8 - extra_w) : 0);
            bit_set_t bit_set(std::size_t(w * line_w));
            logger->debug("qr code v{}, width = {}, scale = {}", code->version, code->width, scale);
            for (int i = 0; i < code->width; ++i) {
                for (int j = 0; j < code->width; ++j) {
                    auto bit = code->data[i * code->width + j] & 1;
                    for (int y = i * scale; y < (i + 1) * scale; ++y) {
                        for (int x = j * scale; x < (j + 1) * scale; ++x) {
                            bit_set.set(y * line_w + x, bit);
                        }
                    }
                }
            }
            bits.clear();
            bits.reserve(bit_set.num_blocks());
            boost::to_block_range(bit_set, std::back_insert_iterator(bits));
            auto box = new Fl_Box(prev->x(), prev->y(), prev->w(), prev->h(), device_id);
            image.reset(new Fl_Bitmap(bits.data(), w, w));
            box->image(image.get());
            box->box(FL_ENGRAVED_BOX);
            return box;
        });
    }
}
