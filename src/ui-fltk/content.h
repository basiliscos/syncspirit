#pragma once

#include <FL/Fl_Widget.H>

#include <type_traits>

namespace syncspirit::fltk {

struct content_t {
    virtual Fl_Widget *get_widget() = 0;

    virtual void refresh();
    virtual void reset();
    virtual bool store(void *);

    virtual ~content_t() = default;
};

template <typename T, typename E = void> struct contentable_t : T, content_t {
    using T::T;

    Fl_Widget *get_widget() override { return this; }
};

template <typename T> struct contentable_t<T, std::is_base_of<Fl_Group, T>> : T, content_t {
    using T::T;

    Fl_Widget *get_widget() override { return this; }

    void refresh() override {
        for (int i = 0; i < this->children(); ++i) {
            auto child = this->child(i);
            if (auto content = dynamic_cast<content_t *>(child)) {
                content->refresh();
            }
        }
    }
};

} // namespace syncspirit::fltk
