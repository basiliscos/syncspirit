#pragma once

// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2025 Ivan Baidakou

#pragma once

#include "syncspirit-export.h"

namespace syncspirit{

namespace proto {
// clang-format off

enum class MessageType {
    CLUSTER_CONFIG    = 0,
    INDEX             = 1,
    INDEX_UPDATE      = 2,
    REQUEST           = 3,
    RESPONSE          = 4,
    DOWNLOAD_PROGRESS = 5,
    PING              = 6,
    CLOSE             = 7,
};

enum class MessageCompression {
    NONE = 0,
    LZ4  = 1,
};

enum class Compression {
    METADATA = 0,
    NEVER    = 1,
    ALWAYS   = 2,
};

enum class FileInfoType {
    FILE              = 0,
    DIRECTORY         = 1,
    SYMLINK_FILE      = 2,
    SYMLINK_DIRECTORY = 3,
    SYMLINK           = 4,
};

enum class ErrorCode {
    NO_BEP_ERROR = 0,
    GENERIC      = 1,
    NO_SUCH_FILE = 2,
    INVALID_FILE = 3,
};

enum class FileDownloadProgressUpdateType {
    APPEND = 0,
    FORGET = 1,
};

// clang-format on

struct SYNCSPIRIT_API Announce;
struct SYNCSPIRIT_API Hello;
struct SYNCSPIRIT_API Header;
struct SYNCSPIRIT_API Device;
struct SYNCSPIRIT_API Folder;
struct SYNCSPIRIT_API ClusterConfig;
struct SYNCSPIRIT_API Counter;
struct SYNCSPIRIT_API Vector;
struct SYNCSPIRIT_API BlockInfo;
struct SYNCSPIRIT_API FileInfo;
struct SYNCSPIRIT_API Index;
struct SYNCSPIRIT_API IndexUpdate;
struct SYNCSPIRIT_API Request;
struct SYNCSPIRIT_API Response;
struct SYNCSPIRIT_API FileDownloadProgressUpdate;
struct SYNCSPIRIT_API DownloadProgress;
struct SYNCSPIRIT_API Ping;
struct SYNCSPIRIT_API Close;

}

namespace db {

// clang-format off

enum class FolderType {
    send             = 0,
    receive          = 1,
    send_and_receive = 2,
};

enum class PullOrder {
    random      = 0,
    alphabetic  = 1,
    smallest    = 2,
    largest     = 3,
    oldest      = 4,
    newest      = 5,
};

// clang-format on

struct SYNCSPIRIT_API IgnoredFolder;
struct SYNCSPIRIT_API Device;
struct SYNCSPIRIT_API Folder;
struct SYNCSPIRIT_API FolderInfo;
struct SYNCSPIRIT_API PendingFolder;
struct SYNCSPIRIT_API FileInfo;
struct SYNCSPIRIT_API IngoredFolder;
struct SYNCSPIRIT_API BlockInfo;
struct SYNCSPIRIT_API SomeDevice;

namespace view {
struct SYNCSPIRIT_API Folder;
}

namespace changeable {
struct SYNCSPIRIT_API Folder;
}

}

}
