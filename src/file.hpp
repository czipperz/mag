#pragma once

namespace cz {
struct Str;
struct Input_File;
struct Output_File;
struct Result;
}

namespace mag {

struct Buffer;
struct Editor;
struct Client;
struct Contents;
struct Buffer_Id;

bool is_directory(const char* path);

void* get_file_time(const char* path);
bool is_out_of_date(const char* path, void* file_time);
bool is_file_time_before(const void* file_time, const void* other_file_time);

struct Date {
    /// Printable year (2020 = year 2020).
    int year;
    /// Printable month (1 = January).
    int month;
    /// Printable day of month (1 = 1st).
    int day_of_month;
    /// Index day of week (0 = Sunday).
    int day_of_week;
    /// Index hour (0 = 01:00:00 AM).
    int hour;
    /// Index minute (0 = 00:01:00).
    int minute;
    /// Index second (0 = 00:00:01).
    int second;
};

/// Convert a `file_time` to a `Date` in UTC.
bool file_time_to_date_utc(const void* file_time, Date* date);
/// Convert a `file_time` to a `Date` in the local time zone.
bool file_time_to_date_local(const void* file_time, Date* date);

cz::Result reload_directory_buffer(Buffer* buffer);

void open_file(Editor* editor, Client* client, cz::Str user_path);

bool save_buffer(Buffer* buffer);

bool save_contents_cr(const Contents* contents, cz::Output_File file);
bool save_contents_cr(const Contents* contents, const char* path);
bool save_contents_to_temp_file_cr(const Contents* contents, cz::Input_File* fd);

bool save_contents_no_cr(const Contents* contents, cz::Output_File file);
bool save_contents_no_cr(const Contents* contents, const char* path);
bool save_contents_to_temp_file_no_cr(const Contents* contents, cz::Input_File* fd);

bool save_contents(const Contents* contents, cz::Output_File file, bool use_carriage_returns);
bool save_contents(const Contents* contents, const char* path, bool use_carriage_returns);
bool save_contents_to_temp_file(const Contents* contents,
                                cz::Input_File* fd,
                                bool use_carriage_returns);

bool find_buffer_by_path(Editor* editor, Client* client, cz::Str path, Buffer_Id* buffer_id);

/// Find temporary buffer by parsing the path.
///
/// `path` should be of the style `NAME` or `NAME (DIRECTORY)`.
bool find_temp_buffer(Editor* editor, Client* client, cz::Str path, Buffer_Id* buffer_id);

}
