#include "syncspirit-export.h"
#include "proto-fwd.hpp"

namespace syncspirit::proto {

namespace encode {

utils::bytes_t encode(const Announce&);
utils::bytes_t encode(const BlockInfo&);
utils::bytes_t encode(const Close&);
utils::bytes_t encode(const ClusterConfig&);
utils::bytes_t encode(const Device&);
utils::bytes_t encode(const DownloadProgress&);
utils::bytes_t encode(const FileDownloadProgressUpdate&);
utils::bytes_t encode(const FileInfo&);
utils::bytes_t encode(const Folder&);
utils::bytes_t encode(const Header&);
utils::bytes_t encode(const Hello&);
std::size_t    encode(const Hello&, fmt::memory_buffer&);
utils::bytes_t encode(const IndexBase&);
utils::bytes_t encode(const Ping&);
utils::bytes_t encode(const Request&);
utils::bytes_t encode(const Response&);

}

namespace decode {

bool decode(utils::bytes_view_t, Announce&);
bool decode(utils::bytes_view_t, BlockInfo&);
bool decode(utils::bytes_view_t, Close&);
bool decode(utils::bytes_view_t, ClusterConfig&);
bool decode(utils::bytes_view_t, Device&);
bool decode(utils::bytes_view_t, DownloadProgress&);
bool decode(utils::bytes_view_t, FileDownloadProgressUpdate&);
bool decode(utils::bytes_view_t, FileInfo&);
bool decode(utils::bytes_view_t, Folder&);
bool decode(utils::bytes_view_t, Header&);
bool decode(utils::bytes_view_t, Hello&);
bool decode(utils::bytes_view_t, IndexBase&);
bool decode(utils::bytes_view_t, Ping&);
bool decode(utils::bytes_view_t, Request&);
bool decode(utils::bytes_view_t, Response&);

}

// Announce
utils::bytes_view_t get_id(const Announce&);
void                set_id(Announce&, utils::bytes_view_t value);
std::size_t         get_addresses_size(const Announce&);
std::string_view    get_addresses(const Announce&, std::size_t i);
void                set_addresses(Announce&, std::size_t i, std::string_view);
void                add_addresses(Announce&, std::string_view);
std::uint64_t       get_instance_id(Announce&);
void                set_instance_id(Announce&, std::uint64_t value);

// BlockInfo

std::int64_t        get_offset(BlockInfo&);
void                set_offset(BlockInfo&, std::int64_t value);
std::int32_t        get_size(const BlockInfo&);
void                set_size(BlockInfo&, std::int32_t value);
utils::bytes_view_t get_hash(const BlockInfo&);
void                set_hash(BlockInfo&, utils::bytes_view_t value);
std::uint32_t       get_weak_hash(const BlockInfo&);
void                set_weak_hash(BlockInfo&, std::uint32_t value);

// Close
std::string_view    get_reason(const Close&);
void                set_reason(Close&, std::string_view value);
void                set_reason(Close&, std::string value);

// ClusterConfig
std::size_t         get_folders_size(const ClusterConfig&);
Folder&             get_folders(const ClusterConfig&, std::size_t i);
void                set_folders(ClusterConfig&, std::size_t i, Folder);
void                add_folders(ClusterConfig&, Folder);

// Counter
std::uint64_t       get_id(const Counter&);
void                set_id(Counter&, std::uint64_t value);
std::uint64_t       get_value(const Counter&);
void                set_value(Counter&, std::uint64_t value);

// Device
utils::bytes_view_t get_id(const Device&);
void                set_id(Device&, utils::bytes_view_t value);
std::string_view    get_name(const Device&);
void                set_name(Device&, std::string_view value);
std::size_t         get_addresses_size(const Device&);
std::string_view    get_addresses(const Device&, std::size_t i);
void                set_addresses(Device&, std::size_t i, std::string_view);
Compression         get_compression(const Device&);
void                set_compression(Device&, Compression value);
std::string_view    get_cert_name(const Device&);
void                set_cert_name(Device&, std::string_view value);
std::int64_t        get_max_sequence(const Device&);
void                set_max_sequence(Device&, std::int64_t value);
bool                get_introducer(const Device&);
void                set_introducer(Device&, bool value);
std::uint64_t       get_index_id(const Device&);
void                set_index_id(Device&, std::uint64_t value);
bool                get_skip_introduction_removals(const Device&);
void                set_skip_introduction_removals(Device&, bool value);

// FileInfo
std::string_view    get_name(const FileInfo&);
void                set_name(FileInfo&, std::string_view value);
FileInfoType        get_type(const FileInfo&);
void                set_type(FileInfo&, FileInfoType value);
std::int64_t        get_size(const FileInfo&);
void                set_size(FileInfo&, std::int64_t value);
std::uint32_t       get_permissions(const FileInfo&);
void                set_permissions(FileInfo&, std::uint32_t value);
std::int64_t        get_modified_s(const FileInfo&);
void                set_modified_s(FileInfo&, std::int64_t value);
std::int32_t        get_modified_ns(const FileInfo&);
void                set_modified_ns(FileInfo&, std::int32_t value);
std::uint64_t       get_modified_by(const FileInfo&);
void                set_modified_by(FileInfo&, std::uint64_t value);
bool                get_deleted(const FileInfo&);
void                set_deleted(FileInfo&, bool value);
bool                get_invalid(const FileInfo&);
void                set_invalid(FileInfo&, bool value);
bool                get_no_permissions(const FileInfo&);
void                set_no_permissions(FileInfo&, bool value);
Vector&             get_version(const FileInfo&);
void                set_version(FileInfo&, Vector value);
std::int64_t        get_sequence(const FileInfo&);
void                set_sequence(FileInfo&, std::int64_t value);
std::int32_t        get_block_size(const FileInfo&);
void                set_block_size(FileInfo&, std::int32_t value);
std::size_t         get_blocks_size(const FileInfo&);
BlockInfo&          get_blocks(const FileInfo&, std::size_t i);
void                set_blocks(FileInfo&, std::size_t i, BlockInfo);
void                add_blocks(FileInfo&, BlockInfo);
std::string_view    get_symlink_target(const FileInfo&);
void                set_symlink_target(FileInfo&, std::string_view value);
void                set_symlink_target(FileInfo&, std::string value);

// Folder
std::string_view    get_id(const Folder&);
void                set_id(Folder&, std::string_view value);
std::string_view    get_label(const Folder&);
void                set_label(Folder&, std::string_view value);
bool                get_read_only(const Folder&);
void                set_read_only(Folder&, bool value);
bool                get_ignore_permissions(const Folder&);
void                set_ignore_permissions(Folder&, bool value);
bool                get_ignore_delete(const Folder&);
void                set_ignore_delete(Folder&, bool value);
bool                get_disable_temp_indexes(const Folder&);
void                set_disable_temp_indexes(Folder&, bool value);
bool                get_paused(const Folder&);
void                set_paused(Folder&, bool value);
std::size_t         get_devices_size(const Folder&);
Device&                            get_devices(const Folder&, std::size_t i);
void                set_devices(Folder&, std::size_t i, Device);
void                add_devices(Folder&, Device);


// Header
MessageType         get_type(const Header&);
void                set_type(Announce&, MessageType value);
MessageCompression  get_compression(const Header&);
void                set_compression(Announce&, MessageCompression value);

// Hello
std::string_view    get_device_name(const Hello&);
void                set_device_name(Hello&, std::string_view value);
std::string_view    get_client_name(const Hello&);
void                set_client_name(Hello&, std::string_view value);
std::string_view    get_client_version(const Hello&);
void                set_client_version(Hello&, std::string_view value);

// IndexBase
std::string_view    get_folder(const IndexBase&);
void                set_folder(IndexBase&, std::string_view value);
std::size_t         get_files_size(const IndexBase&);
FileInfo&           get_files(const IndexBase&, std::size_t i);
void                set_files(IndexBase&, std::size_t i, FileInfo);
void                add_files(IndexBase&, FileInfo);

// Request
std::int32_t        get_id(const Request&);
void                set_id(Request&, std::int32_t value);
std::string_view    get_folder(const Request&);
void                set_folder(Request&, std::string_view value);
std::string_view    get_name(const Request&);
void                set_name(Request&, std::string_view value);
std::int64_t        get_offset(const Request&);
void                set_offset(Request&, std::int64_t value);
std::int32_t        get_size(const Request&);
void                set_size(Request&, std::int32_t value);
utils::bytes_view_t get_hash(const Request&);
void                set_hash(Request&, utils::bytes_view_t value);
bool                get_from_temporary(const Request&);
void                set_from_temporary(Request&, bool value);
std::int32_t        get_weak_hash(const Request&);
void                set_weak_hash(Request&, std::int32_t value);

// Response
std::int32_t        get_id(const Response&);
void                set_id(Response&, std::int32_t value);
utils::bytes_view_t get_data(const Response&);
void                set_data(Response&, utils::bytes_view_t value);
void                set_data(Response&, utils::bytes_t value);
ErrorCode           get_code(const Response&);
void                set_code(Response&, ErrorCode value);

// Vector
std::size_t         get_counters_size(const Vector&);
Counter&            get_counters(const Vector&, std::size_t i);
void                clear_counters(Vector&);
void                set_counters(Vector&, std::size_t i, Counter);
void                add_counters(Vector&, Counter);

}


