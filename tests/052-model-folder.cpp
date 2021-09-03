#include "catch.hpp"
#include "test-utils.h"
#include "model/file_info.h"
#include "model/cluster.h"

using namespace syncspirit;
using namespace syncspirit::test;
using namespace syncspirit::model;

namespace bfs = boost::filesystem;

TEST_CASE("folder, update local files", "[model]") {
    db::Device db_d1;
    db_d1.set_id(test::device_id2sha256("KHQNO2S-5QSILRK-YX4JZZ4-7L77APM-QNVGZJT-EKU7IFI-PNEPBMY-4MXFMQD"));
    auto d1 = model::device_ptr_t(new model::device_t(db_d1, 2));

    db::Folder db_f;
    db_f.set_id("2");
    db_f.set_label("f2-label");
    db_f.set_path("/some/path");
    auto folder = model::folder_ptr_t(new model::folder_t(db_f, 1));

    cluster_ptr_t cluster = new cluster_t(d1);
    auto folders = model::folders_map_t();
    folders.put(folder);
    cluster->assign_folders(std::move(folders));
    folder->assign_cluster(cluster.get());
    folder->assign_device(d1);

    db::FolderInfo db_folderinfo;
    auto folder_info = folder_info_ptr_t(new folder_info_t(db_folderinfo, d1.get(), folder.get(), 3));
    folder->add(folder_info);

    SECTION("deleted file (in cluster) is not present (on disk)") {
        auto db_file = db::FileInfo();
        db_file.set_name("my-file.txt");
        db_file.set_sequence(5);
        db_file.set_type(proto::FileInfoType::FILE);
        db_file.set_deleted(true);
        auto file = model::file_info_ptr_t(new file_info_t(db_file, folder_info.get()));
        file->unmark_dirty();
        folder_info->add(file);

        auto local_file_map = model::local_file_map_t(bfs::path("/some/path"));
        folder->update(local_file_map);
        CHECK(file->is_dirty() == false);
    }
}
