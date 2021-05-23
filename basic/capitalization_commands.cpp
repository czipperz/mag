#include "capitalization_commands.hpp"

#include <cz/char_type.hpp>
#include <cz/format.hpp>
#include <cz/heap_vector.hpp>
#include "buffer_commands.hpp"
#include "client.hpp"
#include "command_macros.hpp"
#include "movement.hpp"
#include "transaction.hpp"
#include "window.hpp"

namespace mag {
namespace basic {

void command_uppercase_letter(Editor* editor, Command_Source source) {
    WITH_SELECTED_BUFFER(source.client);

    cz::Slice<Cursor> cursors = window->cursors;
    Transaction transaction;
    transaction.init(buffer);
    CZ_DEFER(transaction.drop());

    for (size_t c = 0; c < cursors.len; ++c) {
        uint64_t point = cursors[c].point;
        char ch = buffer->contents.get_once(point);

        Edit remove;
        remove.value = SSOStr::from_char(ch);
        remove.position = point;
        remove.flags = Edit::REMOVE;
        transaction.push(remove);

        Edit insert;
        insert.value = SSOStr::from_char(cz::to_upper(ch));
        insert.position = point;
        insert.flags = Edit::INSERT;
        transaction.push(insert);
    }

    transaction.commit();
}

void command_lowercase_letter(Editor* editor, Command_Source source) {
    WITH_SELECTED_BUFFER(source.client);

    cz::Slice<Cursor> cursors = window->cursors;
    Transaction transaction;
    transaction.init(buffer);
    CZ_DEFER(transaction.drop());

    for (size_t c = 0; c < cursors.len; ++c) {
        uint64_t point = cursors[c].point;
        char ch = buffer->contents.get_once(point);

        Edit remove;
        remove.value = SSOStr::from_char(ch);
        remove.position = point;
        remove.flags = Edit::REMOVE;
        transaction.push(remove);

        Edit insert;
        insert.value = SSOStr::from_char(cz::to_lower(ch));
        insert.position = point;
        insert.flags = Edit::INSERT;
        transaction.push(insert);
    }

    transaction.commit();
}

void command_uppercase_region_or_word(Editor* editor, Command_Source source) {
    WITH_SELECTED_BUFFER(source.client);

    Transaction transaction;
    transaction.init(buffer);
    CZ_DEFER(transaction.drop());

    cz::Slice<Cursor> cursors = window->cursors;
    for (size_t c = 0; c < cursors.len; ++c) {
        Contents_Iterator start;
        uint64_t end;

        if (window->show_marks) {
            start = buffer->contents.iterator_at(cursors[c].start());
            end = cursors[c].end();
        } else {
            start = buffer->contents.iterator_at(cursors[c].point);
            Contents_Iterator end_it = start;
            forward_word(&end_it);
            end = end_it.position;
        }

        Edit remove;
        remove.value = buffer->contents.slice(transaction.value_allocator(), start, end);
        remove.position = start.position;
        remove.flags = Edit::REMOVE;
        transaction.push(remove);

        Edit insert;
        insert.value = remove.value.duplicate(transaction.value_allocator());
        cz::Str str = insert.value.as_str();
        for (size_t i = 0; i < str.len; ++i) {
            ((char*)str.buffer)[i] = cz::to_upper(str.buffer[i]);
        }

        insert.position = start.position;
        insert.flags = Edit::INSERT;
        transaction.push(insert);
    }

    transaction.commit();

    window->show_marks = false;
}

void command_lowercase_region_or_word(Editor* editor, Command_Source source) {
    WITH_SELECTED_BUFFER(source.client);

    Transaction transaction;
    transaction.init(buffer);
    CZ_DEFER(transaction.drop());

    cz::Slice<Cursor> cursors = window->cursors;
    for (size_t c = 0; c < cursors.len; ++c) {
        Contents_Iterator start;
        uint64_t end;

        if (window->show_marks) {
            start = buffer->contents.iterator_at(cursors[c].start());
            end = cursors[c].end();
        } else {
            start = buffer->contents.iterator_at(cursors[c].point);
            Contents_Iterator end_it = start;
            forward_word(&end_it);
            end = end_it.position;
        }

        Edit remove;
        remove.value = buffer->contents.slice(transaction.value_allocator(), start, end);
        remove.position = start.position;
        remove.flags = Edit::REMOVE;
        transaction.push(remove);

        Edit insert;
        insert.value = remove.value.duplicate(transaction.value_allocator());
        cz::Str str = insert.value.as_str();
        for (size_t i = 0; i < str.len; ++i) {
            ((char*)str.buffer)[i] = cz::to_lower(str.buffer[i]);
        }

        insert.position = start.position;
        insert.flags = Edit::INSERT;
        transaction.push(insert);
    }

    transaction.commit();

    window->show_marks = false;
}

static bool parse_components_separator(cz::Str in,
                                       cz::Allocator allocator,
                                       cz::Vector<cz::Str>* out,
                                       char sep) {
    const char* separator = in.find(sep);
    if (!separator) {
        return false;
    }

    // Snake case.
    while (1) {
        out->reserve(allocator, 1);
        out->push(in.slice_end(separator));
        if (separator + 1 >= in.end()) {
            break;
        }

        in = in.slice_start(separator + 1);
        separator = in.find(sep);
        if (!separator) {
            separator = in.end();
        }
    }

    return true;
}

void parse_components(cz::Str in, cz::Allocator allocator, cz::Vector<cz::Str>* out) {
    if (in.len == 0) {
        return;
    }

    if (parse_components_separator(in, allocator, out, '_')) {
        // Found snake string.
        return;
    } else if (parse_components_separator(in, allocator, out, '-')) {
        // Found kebab string.
        return;
    }

    // Parse Camel / Pascal string.
    size_t component_start = 0;
    size_t component_end = 0;
    while (1) {
        // A component must be at least one character long.
        ++component_end;

        if (component_end < in.len) {
            // Note: use !is_upper to catch digits and misc shit as well as lower.
            if (!cz::is_upper(in[component_end])) {
                // Normal case; advance until we see an upper case letter or end of string.
                for (++component_end; component_end < in.len; ++component_end) {
                    if (cz::is_upper(in[component_end])) {
                        break;
                    }
                }
            } else {
                // Case 1: we just started a Camel case string that looks like
                // `aComponent`.  In this case treat `a` as its own component and stop.
                if (component_end == 1 && !cz::is_upper(in[0])) {
                    goto finish_component;
                }

                CZ_DEBUG_ASSERT(cz::is_upper(in[component_start]));

                // Now we can either see `COMPONENT` or `ABCDEComponent`.
                // Advance until we get to the first lower case character.
                for (++component_end; component_end < in.len; ++component_end) {
                    if (cz::is_lower(in[component_end])) {
                        // Parse `ABCDEComponent` as `ABCDE` then `Component`.
                        --component_end;
                        break;
                    }
                }
            }
        }

    finish_component:
        out->reserve(allocator, 1);
        out->push(in.slice(component_start, component_end));
        if (component_end == in.len) {
            return;
        }
        component_start = component_end;
    }
}

static void strip(cz::Str* in, cz::Str* prefix, cz::Str* suffix) {
    size_t prefix_end = 0;
    for (; prefix_end < in->len; ++prefix_end) {
        if ((*in)[prefix_end] != '_' && (*in)[prefix_end] != '-') {
            break;
        }
    }

    *prefix = in->slice_end(prefix_end);
    *in = in->slice_start(prefix_end);

    size_t suffix_start = in->len;
    for (; suffix_start-- > 0;) {
        if ((*in)[suffix_start] != '_' && (*in)[suffix_start] != '-') {
            break;
        }
    }
    ++suffix_start;

    *suffix = in->slice_start(suffix_start);
    *in = in->slice_end(suffix_start);
}

void to_camel(cz::Str in, cz::Allocator allocator, cz::String* out) {
    cz::Str prefix, suffix;
    strip(&in, &prefix, &suffix);
    cz::append(allocator, out, prefix);

    cz::Heap_Vector<cz::Str> components = {};
    CZ_DEFER(components.drop());
    parse_components(in, cz::heap_allocator(), &components);

    for (size_t i = 0; i < components.len(); ++i) {
        cz::Str component = components[i];
        out->reserve(allocator, component.len);

        if (i == 0) {
            out->push(cz::to_lower(component[0]));
        } else {
            out->push(cz::to_upper(component[0]));
        }
        for (size_t j = 1; j < component.len; ++j) {
            out->push(cz::to_lower(component[j]));
        }
    }

    cz::append(allocator, out, suffix);
}
void to_pascal(cz::Str in, cz::Allocator allocator, cz::String* out) {
    cz::Str prefix, suffix;
    strip(&in, &prefix, &suffix);
    cz::append(allocator, out, prefix);

    cz::Heap_Vector<cz::Str> components = {};
    CZ_DEFER(components.drop());
    parse_components(in, cz::heap_allocator(), &components);

    for (size_t i = 0; i < components.len(); ++i) {
        cz::Str component = components[i];
        out->reserve(allocator, component.len);
        out->push(cz::to_upper(component[0]));
        for (size_t j = 1; j < component.len; ++j) {
            out->push(cz::to_lower(component[j]));
        }
    }

    cz::append(allocator, out, suffix);
}
void to_snake(cz::Str in, cz::Allocator allocator, cz::String* out) {
    cz::Str prefix, suffix;
    strip(&in, &prefix, &suffix);
    cz::append(allocator, out, prefix);

    cz::Heap_Vector<cz::Str> components = {};
    CZ_DEFER(components.drop());
    parse_components(in, cz::heap_allocator(), &components);

    for (size_t i = 0; i < components.len(); ++i) {
        if (i != 0) {
            cz::append(allocator, out, '_');
        }

        cz::Str component = components[i];
        out->reserve(allocator, component.len);
        for (size_t j = 0; j < component.len; ++j) {
            out->push(cz::to_lower(component[j]));
        }
    }

    cz::append(allocator, out, suffix);
}
void to_usnake(cz::Str in, cz::Allocator allocator, cz::String* out) {
    cz::Str prefix, suffix;
    strip(&in, &prefix, &suffix);
    cz::append(allocator, out, prefix);

    cz::Heap_Vector<cz::Str> components = {};
    CZ_DEFER(components.drop());
    parse_components(in, cz::heap_allocator(), &components);

    for (size_t i = 0; i < components.len(); ++i) {
        if (i != 0) {
            cz::append(allocator, out, '_');
        }

        cz::Str component = components[i];
        out->reserve(allocator, component.len);
        out->push(cz::to_upper(component[0]));
        for (size_t j = 1; j < component.len; ++j) {
            out->push(cz::to_lower(component[j]));
        }
    }

    cz::append(allocator, out, suffix);
}
void to_ssnake(cz::Str in, cz::Allocator allocator, cz::String* out) {
    cz::Str prefix, suffix;
    strip(&in, &prefix, &suffix);
    cz::append(allocator, out, prefix);

    cz::Heap_Vector<cz::Str> components = {};
    CZ_DEFER(components.drop());
    parse_components(in, cz::heap_allocator(), &components);

    for (size_t i = 0; i < components.len(); ++i) {
        if (i != 0) {
            cz::append(allocator, out, '_');
        }

        cz::Str component = components[i];
        out->reserve(allocator, component.len);
        for (size_t j = 0; j < component.len; ++j) {
            out->push(cz::to_upper(component[j]));
        }
    }

    cz::append(allocator, out, suffix);
}
void to_kebab(cz::Str in, cz::Allocator allocator, cz::String* out) {
    cz::Str prefix, suffix;
    strip(&in, &prefix, &suffix);
    cz::append(allocator, out, prefix);

    cz::Heap_Vector<cz::Str> components = {};
    CZ_DEFER(components.drop());
    parse_components(in, cz::heap_allocator(), &components);

    for (size_t i = 0; i < components.len(); ++i) {
        if (i != 0) {
            cz::append(allocator, out, '-');
        }

        cz::Str component = components[i];
        out->reserve(allocator, component.len);
        for (size_t j = 0; j < component.len; ++j) {
            out->push(cz::to_lower(component[j]));
        }
    }

    cz::append(allocator, out, suffix);
}
void to_ukebab(cz::Str in, cz::Allocator allocator, cz::String* out) {
    cz::Str prefix, suffix;
    strip(&in, &prefix, &suffix);
    cz::append(allocator, out, prefix);

    cz::Heap_Vector<cz::Str> components = {};
    CZ_DEFER(components.drop());
    parse_components(in, cz::heap_allocator(), &components);

    for (size_t i = 0; i < components.len(); ++i) {
        if (i != 0) {
            cz::append(allocator, out, '-');
        }

        cz::Str component = components[i];
        out->reserve(allocator, component.len);
        out->push(cz::to_upper(component[0]));
        for (size_t j = 1; j < component.len; ++j) {
            out->push(cz::to_lower(component[j]));
        }
    }

    cz::append(allocator, out, suffix);
}
void to_skebab(cz::Str in, cz::Allocator allocator, cz::String* out) {
    cz::Str prefix, suffix;
    strip(&in, &prefix, &suffix);
    cz::append(allocator, out, prefix);

    cz::Heap_Vector<cz::Str> components = {};
    CZ_DEFER(components.drop());
    parse_components(in, cz::heap_allocator(), &components);

    for (size_t i = 0; i < components.len(); ++i) {
        if (i != 0) {
            cz::append(allocator, out, '-');
        }

        cz::Str component = components[i];
        out->reserve(allocator, component.len);
        for (size_t j = 0; j < component.len; ++j) {
            out->push(cz::to_upper(component[j]));
        }
    }

    cz::append(allocator, out, suffix);
}

void command_recapitalize_token_to(Editor* editor,
                                   Client* client,
                                   void (*convert)(cz::Str, cz::Allocator, cz::String*)) {
    WITH_SELECTED_BUFFER(client);

    Transaction transaction;
    transaction.init(buffer);
    CZ_DEFER(transaction.drop());

    int64_t offset = 0;
    Contents_Iterator it = buffer->contents.start();
    for (size_t i = 0; i < window->cursors.len(); ++i) {
        it.advance_to(window->cursors[i].point);

        Token token;
        if (!get_token_at_position(buffer, &it, &token)) {
            continue;
        }

        it.go_to(token.start);

        SSOStr original = buffer->contents.slice(transaction.value_allocator(), it, token.end);

        cz::String replacement = {};
        convert(original.as_str(), transaction.value_allocator(), &replacement);

        if (replacement == original.as_str()) {
            replacement.drop(transaction.value_allocator());
            original.drop(transaction.value_allocator());
            continue;
        }

        Edit remove;
        remove.value = original;
        remove.position = it.position + offset;
        remove.flags = Edit::REMOVE;
        transaction.push(remove);

        Edit insert;
        insert.value = SSOStr::from_constant(replacement);
        if (insert.value.is_short()) {
            replacement.drop(transaction.value_allocator());
        }
        insert.position = it.position + offset;
        insert.flags = Edit::INSERT;
        transaction.push(insert);

        offset += insert.value.len() - remove.value.len();
    }

    transaction.commit();
}

static void command_recapitalize_token_prompt_callback(Editor* editor,
                                                       Client* client,
                                                       cz::Str mini_buffer_contents,
                                                       void*) {
    void (*convert)(cz::Str, cz::Allocator, cz::String*);
    if (mini_buffer_contents.starts_with("(c")) {
        convert = to_camel;
    } else if (mini_buffer_contents.starts_with("(p") || mini_buffer_contents.starts_with("(C")) {
        convert = to_pascal;
    } else if (mini_buffer_contents.starts_with("(s") || mini_buffer_contents.starts_with("(_")) {
        convert = to_snake;
    } else if (mini_buffer_contents.starts_with("(SS")) {
        convert = to_ssnake;
    } else if (mini_buffer_contents.starts_with("(S")) {
        convert = to_usnake;
    } else if (mini_buffer_contents.starts_with("(k") || mini_buffer_contents.starts_with("(-")) {
        convert = to_kebab;
    } else if (mini_buffer_contents.starts_with("(KK")) {
        convert = to_skebab;
    } else if (mini_buffer_contents.starts_with("(K")) {
        convert = to_ukebab;
    } else {
        return;
    }

    command_recapitalize_token_to(editor, client, convert);
}

static bool recapitalize_to_completion_engine(Editor* editor,
                                              Completion_Engine_Context* context,
                                              bool is_initial_frame) {
    if (context->results.len() > 0) {
        // We never change the results so ignore input changes.
        return false;
    }

    context->results.reserve(10);
    context->results.push("(c) camelCase");
    context->results.push("(p) (C) PascalCase");
    context->results.push("(s) (_) snake_case");
    context->results.push("(S) Upper_Snake_Case");
    context->results.push("(SS) SCREAMING_SNAKE_CASE");
    context->results.push("(k) (-) kebab-case");
    context->results.push("(K) Upper-Kebab-Case");
    context->results.push("(KK) SCREAMING-KEBAB-CASE");
    return true;
}

void command_recapitalize_token_prompt(Editor* editor, Command_Source source) {
    source.client->show_dialog(editor,
                               "Recapitalize to format: ", recapitalize_to_completion_engine,
                               command_recapitalize_token_prompt_callback, nullptr);

    fill_mini_buffer_with(editor, source.client, "(");
}

void command_recapitalize_token_to_camel(Editor* editor, Command_Source source) {
    return command_recapitalize_token_to(editor, source.client, to_camel);
}
void command_recapitalize_token_to_pascal(Editor* editor, Command_Source source) {
    return command_recapitalize_token_to(editor, source.client, to_pascal);
}
void command_recapitalize_token_to_snake(Editor* editor, Command_Source source) {
    return command_recapitalize_token_to(editor, source.client, to_snake);
}
void command_recapitalize_token_to_usnake(Editor* editor, Command_Source source) {
    return command_recapitalize_token_to(editor, source.client, to_usnake);
}
void command_recapitalize_token_to_ssnake(Editor* editor, Command_Source source) {
    return command_recapitalize_token_to(editor, source.client, to_ssnake);
}
void command_recapitalize_token_to_kebab(Editor* editor, Command_Source source) {
    return command_recapitalize_token_to(editor, source.client, to_kebab);
}
void command_recapitalize_token_to_ukebab(Editor* editor, Command_Source source) {
    return command_recapitalize_token_to(editor, source.client, to_ukebab);
}
void command_recapitalize_token_to_skebab(Editor* editor, Command_Source source) {
    return command_recapitalize_token_to(editor, source.client, to_skebab);
}

}
}
