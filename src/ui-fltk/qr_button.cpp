// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2024-2025 Ivan Baidakou

#include "qr_button.h"

#include <memory>
#include <system_error>
#include <algorithm>
#include <vector>

#include <qrencode.h>
#include <boost/dynamic_bitset.hpp>

#include <FL/Fl.H>
#include <FL/Fl_Group.H>
#include <FL/Fl_Button.H>
#include <FL/Fl_Bitmap.H>

namespace syncspirit::fltk {

namespace {

static constexpr int PADDING = 10;

template <typename T> using guard_t = std::unique_ptr<T, std::function<void(T *)>>;

template <typename T, typename G> guard_t<T> make_guard(T *ptr, G &&fn) {
    return guard_t<T>{ptr, [fn = std::move(fn)](T *it) { fn(it); }};
}

using code_t = guard_t<QRcode>;

struct button_t : Fl_Button {
    using image_t = std::unique_ptr<Fl_Image>;
    using bits_t = std::vector<unsigned char>;
    using bit_set_t = boost::dynamic_bitset<unsigned char>;
    using parent_t = Fl_Button;

    button_t(app_supervisor_t &sup_, code_t code_, int x, int y, int w, int h, const char *label_,
             std::string_view short_id_)
        : parent_t(x, y, w, h), sup{sup_}, code{std::move(code_)}, scale{0}, data{label_}, short_id(short_id_) {
        regen_image(w, h);
        tooltip("copy device id to clipboard");

        int xx, yy, ww, hh;
        recalc_dimensions(x, y, w, h, xx, yy, ww, hh);
        this->x(xx);
        this->y(yy);
        this->w(ww);
        this->h(hh);

        callback(
            [](Fl_Widget *widget, void *data) {
                auto button = static_cast<button_t *>(widget);
                auto device_id = button->data;
                Fl::copy(device_id, strlen(device_id), 1);
                auto sup = reinterpret_cast<app_supervisor_t *>(data);
                sup->get_logger()->info("device id {} has been copied to clipboard", button->short_id);
            },
            &sup);
    }

    void regen_image(int w, int h) {
        auto min_w = std::min(w - PADDING * 2, h - PADDING * 2);
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

    void recalc_dimensions(int x, int y, int w, int h, int &xx, int &yy, int &ww, int &hh) {
        auto img = image();
        ww = img->w() + PADDING * 2;
        hh = img->h() + PADDING * 2;
        xx = x + w / 2 - ww / 2;
        yy = y + h / 2 - hh / 2;
    }

    void resize(int, int, int, int) override {
        int x = parent()->x();
        int y = parent()->y();
        int w = parent()->w();
        int h = parent()->h();
        regen_image(w, h);
        auto img = image();
        int xx, yy, ww, hh;
        recalc_dimensions(x, y, w, h, xx, yy, ww, hh);
        parent_t::resize(xx, yy, ww, hh);
    }

    app_supervisor_t &sup;
    code_t code;
    bits_t bits;
    image_t qr_image;
    int scale = 0;
    const char *data;
    std::string short_id;
};

} // namespace

qr_button_t::qr_button_t(const model::device_id_t &device_, app_supervisor_t &supervisor_, int x, int y, int w, int h)
    : parent_t(x, y, w, h), device_id{device_}, supervisor{supervisor_} {

    box(FL_FLAT_BOX);

    auto device_id_raw = device_id.get_value().c_str();
    auto device_id_short = device_id.get_short();
    auto code_raw = QRcode_encodeString(device_id_raw, 0, QR_ECLEVEL_H, QR_MODE_8, 1);
    auto &logger = supervisor.get_logger();
    if (!code_raw) {
        auto ec = std::error_code{errno, std::generic_category()};
        logger->error("cannot generate qr code ({}) : {}", ec.value(), ec.message());
        return;
    }
    auto code = make_guard(code_raw, [](auto ptr) { QRcode_free(ptr); });
    auto button = new button_t(supervisor, std::move(code), x, y, w, h, device_id_raw, device_id_short);
    resizable(button);
}

} // namespace syncspirit::fltk
