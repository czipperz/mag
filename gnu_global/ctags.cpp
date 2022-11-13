#include "ctags.hpp"

#include <cz/dedup.hpp>
#include <cz/defer.hpp>
#include <cz/file.hpp>
#include <cz/find_file.hpp>
#include <cz/format.hpp>
#include <cz/parse.hpp>
#include <cz/sort.hpp>
#include "file.hpp"

namespace mag {
namespace ctags {

///////////////////////////////////////////////////////////////////////////////
// Utility functions
///////////////////////////////////////////////////////////////////////////////

static bool eat(cz::Str str, size_t* it, char ch) {
    if (str.len >= *it && str[*it] == ch) {
        ++*it;
        return true;
    } else {
        return false;
    }
}

static cz::Str eat_line(cz::Str str, size_t* it) {
    size_t start = *it;
    *it += str.slice_start(*it).find_index('\n');
    if (*it < str.len) {
        ++*it;
        return str.slice(start, *it - 1);
    } else {
        return str.slice_start(start);
    }
}

static bool find(cz::Str str, size_t* it, char ch) {
    *it += str.slice_start(*it).find_index(ch);
    if (*it < str.len) {
        ++*it;
        return true;
    } else {
        return false;
    }
}

static bool get_symbol(cz::Str line, cz::Str* symbol, size_t* symbol_end) {
    size_t symbol_start = 0;
    if (!find(line, &symbol_start, (char)127))
        return false;

    *symbol_end = symbol_start;
    if (!find(line, symbol_end, (char)1))
        return false;

    *symbol = line.slice(symbol_start, *symbol_end - 1);
    return true;
}

///////////////////////////////////////////////////////////////////////////////
// list_symbols
///////////////////////////////////////////////////////////////////////////////

const char* list_symbols(cz::Str directory, cz::Allocator allocator, cz::Vector<cz::Str>* symbols) {
    cz::String contents = {};
    CZ_DEFER(contents.drop(cz::heap_allocator()));
    {
        cz::String path = directory.clone(cz::heap_allocator());
        CZ_DEFER(path.drop(cz::heap_allocator()));
        if (!cz::find_file_up(cz::heap_allocator(), &path, "TAGS"))
            return "Couldn't find TAGS file";

        cz::Input_File file;
        CZ_DEFER(file.close());
        if (!file.open(path.buffer))
            return "Couldn't open TAGS file";
        if (!cz::read_to_string(file, cz::heap_allocator(), &contents))
            return "Couldn't read TAGS file";
    }

    size_t i = 0;

    // Go to after the first \[12; line.
    while (1) {
        cz::Str line = eat_line(contents, &i);
        if (line.len == 1 && line[0] == (char)12)
            break;
    }

    while (i < contents.len) {
        // Ignore file name.
        eat_line(contents, &i);

        bool first = true;

        while (i < contents.len) {
            cz::Str line = eat_line(contents, &i);
            // \[12; means end of this file.
            if (line.len == 1 && line[0] == (char)12)
                break;

            // Get the symbol part.
            cz::Str symbol;
            size_t symbol_end;
            if (!get_symbol(line, &symbol, &symbol_end))
                continue;

            // ctags finds anonymous functions and throws them in there lol so ignore that.
            if (symbol.starts_with("__anon"))
                continue;

            symbols->reserve(cz::heap_allocator(), 1);
            symbols->push(symbol.clone(allocator));
        }
    }

    cz::sort(*symbols);
    cz::dedup(symbols);

    return nullptr;
}

///////////////////////////////////////////////////////////////////////////////
// completion engine
///////////////////////////////////////////////////////////////////////////////

void init_completion_engine_context(Completion_Engine_Context* engine_context, char* directory) {
    engine_context->data = directory;
    engine_context->cleanup = [](void* data) { cz::heap_allocator().dealloc({data, 0}); };
}

bool completion_engine(Editor* editor, Completion_Engine_Context* context, bool is_initial_frame) {
    if (!is_initial_frame)
        return false;

    char* directory = (char*)context->data;
    (void)list_symbols(directory, context->results_buffer_array.allocator(), &context->results);
    return true;
}

///////////////////////////////////////////////////////////////////////////////
// lookup
///////////////////////////////////////////////////////////////////////////////

const char* lookup_symbol(cz::Str directory,
                          cz::Str query,
                          cz::Allocator allocator,
                          cz::Vector<tags::Tag>* tags) {
    cz::String contents = {};
    CZ_DEFER(contents.drop(cz::heap_allocator()));

    cz::String path = directory.clone(cz::heap_allocator());
    CZ_DEFER(path.drop(cz::heap_allocator()));
    if (!cz::find_file_up(cz::heap_allocator(), &path, "TAGS"))
        return "Couldn't find TAGS file";

    {
        cz::Input_File file;
        CZ_DEFER(file.close());
        if (!file.open(path.buffer))
            return "Couldn't open TAGS file";
        if (!cz::read_to_string(file, cz::heap_allocator(), &contents))
            return "Couldn't read TAGS file";
    }

    // Snip out "/TAGS".
    path.len -= 5;

    size_t i = 0;

    // Go to after the first \[12; line.
    while (1) {
        cz::Str line = eat_line(contents, &i);
        if (line.len == 1 && line[0] == (char)12)
            break;
    }

    while (i < contents.len) {
        // Parse file name.
        cz::Str file_name = eat_line(contents, &i);
        const char* comma = file_name.rfind(',');
        if (!comma)
            return "Error parsing TAGS file";
        file_name = file_name.slice_end(comma);

        bool first = true;

        while (i < contents.len) {
            cz::Str line = eat_line(contents, &i);
            // \[12; means end of this file.
            if (line.len == 1 && line[0] == (char)12)
                break;

            // Get the symbol part.
            cz::Str symbol;
            size_t symbol_end;
            if (!get_symbol(line, &symbol, &symbol_end))
                continue;

            // Only continue if the symbol matches.
            if (symbol != query)
                continue;

            // Parse line number.
            size_t line_num_end = symbol_end;
            if (!find(line, &line_num_end, ','))
                return "Error parsing TAGS file";
            cz::Str line_num_str = line.slice(symbol_end, line_num_end - 1);
            uint64_t line_num;
            if (cz::parse(line_num_str, &line_num) <= 0)
                return "Error parsing TAGS file";

            // Push tag.
            tags::Tag tag;
            tag.line = line_num;
            if (first) {
                cz::String temp = cz::format(path, '/', file_name);
                CZ_DEFER(temp.drop(cz::heap_allocator()));
                tag.file_name = standardize_path(allocator, temp);
                first = false;
            } else {
                // Reuse the previous allocation.
                tag.file_name = tags->last().file_name;
            }
            tags->reserve(cz::heap_allocator(), 1);
            tags->push(tag);
        }
    }

    return nullptr;
}

}
}
