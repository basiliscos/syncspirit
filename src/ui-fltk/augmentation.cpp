#include "augmentation.h"
#include "tree_item.h"

namespace syncspirit::fltk {

augmentation_t::augmentation_t(tree_item_t *owner_) : owner{owner_} {}

void augmentation_t::on_update() noexcept {
    if (owner) {
        owner->on_update();
    }
}

void augmentation_t::on_delete() noexcept {
    if (owner) {
        owner->on_delete();
    }
}

void augmentation_t::release_onwer() noexcept { owner = nullptr; }

tree_item_t *augmentation_t::get_owner() noexcept { return owner; }

augmentation_proxy_t::augmentation_proxy_t(augmentation_ptr_t backend_) : backend{backend_} {}

void augmentation_proxy_t::on_update() noexcept { backend->on_update(); }
void augmentation_proxy_t::on_delete() noexcept {
    // no-op, owner should receive own on-delete event
}

void augmentation_proxy_t::release_onwer() noexcept {
    // no-op, owner should receive own on-delete event
}

tree_item_t *augmentation_proxy_t::get_owner() noexcept { return backend->get_owner(); }

using nc_t = augmentation_entry_base_t::name_comparator_t;

bool nc_t::operator()(const ptr_t& lhs, const ptr_t& rhs) const {
    auto ld = lhs->file.is_dir();
    auto rd = rhs->file.is_dir();
    if (ld && !rd) {
        return true;
    } else if (rd && !ld) {
        return false;
    }
    return lhs->get_own_name() < rhs->get_own_name();
}

bool nc_t::operator()(const ptr_t& lhs, const std::string_view rhs) const {
    auto ld = lhs->file.is_dir();
    if (!ld) {
        return false;
    }
    return lhs->get_own_name() < rhs;
}

bool nc_t::operator()(const std::string_view lhs, const ptr_t& rhs) const {
    auto rd = rhs->file.is_dir();
    if (!rd) {
        return true;
    }
    return lhs < rhs->get_own_name();
}

augmentation_entry_base_t::augmentation_entry_base_t(self_t* parent_, dynamic_item_t *owner_, std::string own_name_):
parent_t(owner_), parent{parent_}, own_name{std::move(own_name_)} {}

augmentation_entry_base_t::~augmentation_entry_base_t() {
    if (parent) {
        auto self = static_cast<augmentation_entry_t*>(this);
        auto ref = augmentation_entry_ptr_t(self, false);
        parent->children.erase(ref);
        ref.detach();
    }
    for (auto& it : children) {
        it->parent = nullptr;
    }
    children.clear();
}

auto augmentation_entry_base_t::get_children() noexcept -> children_t& {
    return children;
}

void augmentation_entry_base_t::display() noexcept {}

std::string_view augmentation_entry_base_t::get_own_name() { return own_name; }

augmentation_entry_root_t::augmentation_entry_root_t(model::folder_info_t& folder_, dynamic_item_t* owner_):
    parent_t(nullptr, owner_, {}), folder{folder_} {
    struct file_comparator_t {
        using file_t = model::file_info_t;
        bool operator()(const file_t* lhs, const file_t* rhs ) const {
            auto ld = lhs->is_dir();
            auto rd = rhs->is_dir();
            if (ld && !rd) {
                return true;
            } else if (rd && !ld) {
                return false;
            }
            return lhs->get_name() < rhs->get_name();
        }
    };
    using files_t = std::set<model::file_info_t*, file_comparator_t>;

    auto files = files_t();
    for (auto& it: folder.get_file_infos()) {
        auto file = it.item.get();
        files.emplace(file);
    }
    for (auto file: files) {
        auto path = bfs::path(file->get_name());
        auto parent = (self_t*)(this);
        auto p_it = path.begin();
        auto count = std::distance(p_it, path.end());
        for (decltype(count) i = 0; i < count - 1; ++i, ++p_it) {
            auto name = p_it->string();
            auto name_view = std::string_view(name);
            auto c_it = parent->children.find(name_view);
            parent = c_it->get();
        }
        auto own_name = p_it->string();
        auto child = augmentation_entry_ptr_t(new augmentation_entry_t(parent, *file, own_name));
        parent->children.emplace(std::move(child));
    }
}

model::folder_info_t& augmentation_entry_root_t::get_folder() {
    return folder;
}

auto augmentation_entry_root_t::get_file() -> model::file_info_t* {
    return nullptr;
}

int augmentation_entry_root_t::get_position(bool)  {
    return 0;
}

augmentation_entry_t::augmentation_entry_t(self_t* parent, model::file_info_t& file_, std::string own_name_):
    parent_t(parent, nullptr, std::move(own_name_)), file{file_} {
}


void augmentation_entry_t::display() noexcept {
    if (!owner) {
        if (!parent->owner) {
            parent->display();
        }
        owner = static_cast<dynamic_item_t*>(parent->owner)->create(*this);
    }
    return;
}

int augmentation_entry_t::get_position(bool include_deleted) {
    auto& container = parent->children;
    int position = 0;
    for (auto& it: container) {
        if (it.get() == this) {
            break;
        }
        if (include_deleted || !it->file.is_deleted()) {
            ++position;
        }
    }
    return position;
}

auto augmentation_entry_t::get_file() -> model::file_info_t* {
    return &file;
}


} // namespace syncspirit::fltk
