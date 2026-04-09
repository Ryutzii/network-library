
/// @copyright Copyright (c) 2026 Ángel, All rights reserved.
/// angel.rodriguez@udit.es

#include <map>
#include "MimeType.hpp"
#include <string_view>

namespace argb
{

    MimeType MimeType::from (const std::string_view & path_or_file_extension)
    {
        static const std::map<std::string_view, std::string_view> mime_types_by_file_extension
        {
            { ".7z"     , "application/x-7z-compressed" },
            { ".aac"    , "audio/aac" },
            { ".avif"   , "image/avif" },
            { ".avi"    , "video/x-msvideo" },
            { ".azw"    , "application/vnd.amazon.ebook" },
            { ".bin"    , "application/octet-stream" },
            { ".bmp"    , "image/bmp" },
            { ".bz"     , "application/x-bzip" },
            { ".bz2"    , "application/x-bzip2" },
            { ".css"    , "text/css" },
            { ".csv"    , "text/csv" },
            { ".doc"    , "application/msword" },
            { ".docx"   , "application/vnd.openxmlformats-officedocument.wordprocessingml.document" },
            { ".eot"    , "application/vnd.ms-fontobject" },
            { ".epub"   , "application/epub+zip" },
            { ".gz"     , "application/gzip" },
            { ".gif"    , "image/gif" },
            { ".htm"    , "text/html" },
            { ".html"   , "text/html" },
            { ".ico"    , "image/vnd.microsoft.icon" },
            { ".ics"    , "text/calendar" },
            { ".jar"    , "application/java-archive" },
            { ".jpeg"   , "image/jpeg" },
            { ".jpg"    , "image/jpeg" },
            { ".js"     , "text/javascript" },
            { ".json"   , "application/json" },
            { ".mid"    , "audio/midiaudio/x-midi" },
            { ".midi"   , "audio/midiaudio/x-midi" },
            { ".mp3"    , "audio/mpeg" },
            { ".mp4"    , "video/mp4" },
            { ".mpeg"   , "video/mpeg" },
            { ".odp"    , "application/vnd.oasis.opendocument.presentation" },
            { ".ods"    , "application/vnd.oasis.opendocument.spreadsheet" },
            { ".odt"    , "application/vnd.oasis.opendocument.text" },
            { ".oga"    , "audio/ogg" },
            { ".ogv"    , "video/ogg" },
            { ".ogx"    , "application/ogg" },
            { ".opus"   , "audio/opus" },
            { ".otf"    , "font/otf" },
            { ".png"    , "image/png" },
            { ".pdf"    , "application/pdf" },
            { ".php"    , "application/x-httpd-php" },
            { ".ppt"    , "application/vnd.ms-powerpoint" },
            { ".pptx"   , "application/vnd.openxmlformats-officedocument.presentationml.presentation" },
            { ".rar"    , "application/vnd.rar" },
            { ".rtf"    , "application/rtf" },
            { ".sh"     , "application/x-sh" },
            { ".svg"    , "image/svg+xml" },
            { ".swf"    , "application/x-shockwave-flash" },
            { ".tar"    , "application/x-tar" },
            { ".tif"    , "image/tiff" },
            { ".tiff"   , "image/tiff" },
            { ".ts"     , "text/javascript" },
            { ".ttf"    , "font/ttf" },
            { ".txt"    , "text/plain" },
            { ".wav"    , "audio/wav" },
            { ".weba"   , "audio/webm" },
            { ".webm"   , "video/webm" },
            { ".webp"   , "image/webp" },
            { ".woff"   , "font/woff" },
            { ".woff2"  , "font/woff2" },
            { ".xhtml"  , "application/xhtml+xml" },
            { ".xls"    , "application/vnd.ms-excel" },
            { ".xlsx"   , "application/vnd.openxmlformats-officedocument.spreadsheetml.sheet" },
            { ".xml"    , "application/xml" },
            { ".zip"    , "application/zip" },
        };

        size_t last_dot_offset = path_or_file_extension.find_last_of ('.');

        auto item = mime_types_by_file_extension.find
        (
            last_dot_offset != std::string_view::npos
                ? path_or_file_extension.substr (last_dot_offset)
                : path_or_file_extension
        );

        return item != mime_types_by_file_extension.end ()
            ? MimeType{  item->second.data ()      }
            : MimeType{ "application/octet-stream" };
    }

}
