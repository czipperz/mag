#include "cpp_commands.hpp"

#include <ctype.h>
#include "command_macros.hpp"
#include "editor.hpp"
#include "match.hpp"
#include "movement.hpp"
#include "transaction.hpp"
#include "window.hpp"

namespace mag {
namespace cpp {

static bool is_block(Contents_Iterator start, Contents_Iterator end) {
    // If the start is at the start of the line and the end is at the start of a different
    // line then the lines inbetween are commented using //. In every other case we want to
    // insert /* at the start and */ at the end.

    bool block = false;

    Contents_Iterator sol = start;
    start_of_line_text(&sol);
    if (sol.position < start.position) {
        block = true;
    }

    Contents_Iterator sole = end;
    start_of_line_text(&sole);
    if (sole.position < end.position) {
        block = true;
    }

    return block;
}

void command_comment(Editor* editor, Command_Source source) {
    WITH_SELECTED_BUFFER(source.client);
    cz::Slice<Cursor> cursors = window->cursors;

    Transaction transaction = {};
    CZ_DEFER(transaction.drop());
    if (window->show_marks) {
        size_t edits = 0;
        for (size_t c = 0; c < cursors.len; ++c) {
            Contents_Iterator start = buffer->contents.iterator_at(cursors[c].start());
            Contents_Iterator end = start;
            end.advance_to(cursors[c].end());

            if (is_block(start, end)) {
                edits += 2;
            } else {
                while (start.position < end.position) {
                    edits += 1;

                    end_of_line(&start);
                    if (start.position >= end.position) {
                        break;
                    }
                    start.advance();
                }
            }
        }

        transaction.init(edits, 0);

        uint64_t offset = 0;
        for (size_t c = 0; c < cursors.len; ++c) {
            Contents_Iterator start = buffer->contents.iterator_at(cursors[c].start());
            Contents_Iterator end = start;
            end.advance_to(cursors[c].end());

            if (is_block(start, end)) {
                bool space_start, space_end;

                // Don't add an extra space outside the comment if there is already one there.
                if (start.at_bob()) {
                    space_start = true;
                } else {
                    Contents_Iterator s = start;
                    s.retreat();
                    space_start = !isspace(s.get());
                }
                if (end.at_eob()) {
                    space_end = true;
                } else {
                    space_end = !isspace(end.get());
                }

                Edit edit_start;
                if (space_start) {
                    edit_start.value = SSOStr::from_constant(" /* ");
                } else {
                    edit_start.value = SSOStr::from_constant("/* ");
                }
                edit_start.position = start.position + offset;
                offset += edit_start.value.len();
                edit_start.flags = Edit::INSERT_AFTER_POSITION;
                transaction.push(edit_start);

                Edit edit_end;
                if (space_end) {
                    edit_end.value = SSOStr::from_constant(" */ ");
                } else {
                    edit_end.value = SSOStr::from_constant(" */");
                }
                edit_end.position = end.position + offset;
                offset += edit_end.value.len();
                edit_end.flags = Edit::INSERT;
                transaction.push(edit_end);
            } else {
                // We want the line comments to line up even if the lines being commented have
                // different amounts of indentation.  So we first find the minimum amount of
                // indentation on the lines (start_offset).
                Contents_Iterator s2 = start;
                uint64_t start_offset = 0;
                bool set_offset = false;
                while (s2.position < end.position) {
                    uint64_t p = s2.position;
                    forward_through_whitespace(&s2);
                    if (!set_offset || s2.position - p < start_offset) {
                        start_offset = s2.position - p;
                        set_offset = true;
                    }

                    end_of_line(&s2);
                    if (s2.position >= end.position) {
                        break;
                    }
                    s2.advance();
                }

                while (start.position < end.position) {
                    if (set_offset) {
                        start.advance(start_offset);
                    } else {
                        forward_through_whitespace(&start);
                    }

                    Edit edit;
                    edit.value = SSOStr::from_constant("// ");
                    edit.position = start.position + offset;
                    offset += 3;
                    edit.flags = Edit::INSERT_AFTER_POSITION;
                    transaction.push(edit);

                    end_of_line(&start);
                    if (start.position >= end.position) {
                        break;
                    }
                    start.advance();
                }
            }
        }
    } else {
        transaction.init(cursors.len, 0);

        SSOStr value = SSOStr::from_constant("// ");

        uint64_t offset = 0;
        for (size_t c = 0; c < cursors.len; ++c) {
            Contents_Iterator start = buffer->contents.iterator_at(cursors[c].point);
            start_of_line_text(&start);

            Edit edit;
            edit.value = value;
            edit.position = start.position + offset;
            offset += 3;
            edit.flags = Edit::INSERT;
            transaction.push(edit);
        }
    }

    transaction.commit(buffer);
}

static bool end_of_sentence(cz::Str word) {
    return word.ends_with(".") || word.ends_with("!") || word.ends_with("?") ||
           word.ends_with(".)") || word.ends_with("!)") || word.ends_with("?)");
}

static size_t judge_word_column_goal_score(cz::Slice<SSOStr> words,
                                           size_t word_column_limit,
                                           size_t word_column_goal,
                                           size_t* max_column_length) {
    size_t score = 0;
    size_t current_column = 0;
    size_t previous_spaces = 0;
    *max_column_length = 0;
    bool previous_word_just_after_end_of_sentence = false;
    bool next_word_just_after_end_of_sentence = false;
    for (size_t i = 0; i < words.len; ++i) {
        auto& word = words[i];

        // Test if the word fits on this line.
        if (current_column + previous_spaces + word.len() <= word_column_goal) {
            // If so then add it.
            current_column += previous_spaces + word.len();
        } else {
            // Otherwise make a new line.

            // If we went over the limit then we had a giant word; that couldn't be fixed no matter
            // what goal column we adjust to.
            if (current_column <= word_column_limit) {
                size_t offset = previous_word_just_after_end_of_sentence ? 5 : 0;

                score += (word_column_limit - current_column + offset) *
                         (word_column_limit - current_column + offset);

                *max_column_length = std::max(*max_column_length, current_column);
            }

            current_column = word.len();

            previous_word_just_after_end_of_sentence = false;
            next_word_just_after_end_of_sentence = false;
        }

        previous_spaces = 1;
        if (end_of_sentence(word.as_str())) {
            previous_spaces = 2;

            previous_word_just_after_end_of_sentence = false;
            next_word_just_after_end_of_sentence = true;
        } else {
            previous_word_just_after_end_of_sentence = next_word_just_after_end_of_sentence;
            next_word_just_after_end_of_sentence = false;
        }
    }

    // Duplicate the code from end of line above as we need to add the score for the last line of
    // the comment.
    if (current_column <= word_column_limit) {
        score += (word_column_limit - current_column) * (word_column_limit - current_column);

        *max_column_length = std::max(*max_column_length, current_column);
    }

    return score;
}

static size_t find_word_column_goal(cz::Slice<SSOStr> words, size_t word_column_limit) {
    size_t min_score = SIZE_MAX;
    size_t best_goal = word_column_limit;

    size_t word_column_goal = word_column_limit;
    while (1) {
        size_t max_column_length;
        size_t score = judge_word_column_goal_score(words, word_column_limit, word_column_goal,
                                                    &max_column_length);
        if (score < min_score) {
            min_score = score;
            best_goal = word_column_goal;
        } else if (max_column_length < 50) {
            break;
        }

        word_column_goal = max_column_length - 1;
    }

    return best_goal;
}

void command_reformat_comment(Editor* editor, Command_Source source) {
    WITH_SELECTED_BUFFER(source.client);

    Contents_Iterator iterator = buffer->contents.iterator_at(window->cursors[0].point);

    start_of_line_text(&iterator);
    uint64_t column = get_visual_column(buffer->mode, iterator);

    bool doc_comment;
    if (looking_at(iterator, "// ")) {
        doc_comment = false;
    } else if (looking_at(iterator, "/// ")) {
        doc_comment = true;
    } else {
        return;
    }

    uint64_t start_position = iterator.position;
    while (1) {
        uint64_t point = iterator.position;
        backward_line(buffer->mode, &iterator);

        start_of_line_text(&iterator);
        uint64_t col = get_visual_column(buffer->mode, iterator);
        if (col != column) {
            break;
        }
        if (!looking_at(iterator, doc_comment ? "/// " : "// ")) {
            break;
        }

        start_position = iterator.position;
        if (point == iterator.position) {
            break;
        }
    }

    iterator.advance_to(start_position);
    Contents_Iterator start = iterator;
    start.advance(strlen(doc_comment ? "/// " : "// "));

    cz::Buffer_Array buffer_array;
    buffer_array.create();
    CZ_DEFER(buffer_array.drop());

    size_t words_len_sum = 0;
    size_t extra_spaces = 0;

    cz::Vector<SSOStr> words = {};
    CZ_DEFER(words.drop(cz::heap_allocator()));
    words.reserve(cz::heap_allocator(), 32);

    uint64_t end_position = iterator.position;
    while (1) {
        // Skip comment start on this line.
        while (!iterator.at_eob() && !isspace(iterator.get())) {
            iterator.advance();
        }
        while (!iterator.at_eob() && isspace(iterator.get())) {
            iterator.advance();
        }

        // Parse words on this line.
        while (1) {
            // Parse one word.
            Contents_Iterator word_start = iterator;
            while (!iterator.at_eob() && !isspace(iterator.get())) {
                iterator.advance();
            }

            words.reserve(cz::heap_allocator(), 1);
            SSOStr word =
                buffer->contents.slice(buffer_array.allocator(), word_start, iterator.position);
            words_len_sum += word.len();

            // TODO: make "sentences end in two spaces" a config variable (in the Mode).
            if (end_of_sentence(word.as_str())) {
                extra_spaces++;
            }

            words.push(word);

            // Skip to start of next word.
            while (!iterator.at_eob()) {
                char ch = iterator.get();
                if (ch == '\n' || !isspace(ch)) {
                    break;
                }
                iterator.advance();
            }

            // End of line.
            if (iterator.at_eob() || iterator.get() == '\n') {
                break;
            }
        }

        // Check if the next line fits the pattern.
        if (iterator.at_eob()) {
            break;
        }
        iterator.advance();

        start_of_line_text(&iterator);
        uint64_t col = get_visual_column(buffer->mode, iterator);
        if (col != column) {
            break;
        }
        if (!looking_at(iterator, doc_comment ? "/// " : "// ")) {
            break;
        }

        end_position = iterator.position;
    }

    iterator.retreat_to(end_position);
    Contents_Iterator end = iterator;
    end_of_line(&end);

    if (words.len() == 0) {
        return;
    }

    uint64_t tabs, spaces;
    analyze_indent(buffer->mode, column, &tabs, &spaces);

    size_t total_columns = extra_spaces + words_len_sum + words.len() - 1;
    size_t word_column_limit =
        buffer->mode.preferred_column - column - strlen(doc_comment ? "/// " : "// ");

    size_t word_column_goal = find_word_column_goal(words, word_column_limit);

    cz::String new_region = {};
    CZ_DEFER(new_region.drop(cz::heap_allocator()));

    size_t current_column = 0;
    size_t previous_spaces = 0;
    for (size_t i = 0; i < words.len(); ++i) {
        auto& word = words[i];

        // Test if the word fits on this line.
        if (current_column + previous_spaces + word.len() <= word_column_goal) {
            // If so then add it.
            new_region.reserve(cz::heap_allocator(), previous_spaces + word.len());
            for (size_t j = 0; j < previous_spaces; ++j) {
                new_region.push(' ');
            }
            new_region.append(word.as_str());

            current_column += previous_spaces + word.len();
        } else {
            // Otherwise make a new line.
            new_region.reserve(
                cz::heap_allocator(),
                1 + tabs + spaces + strlen(doc_comment ? "/// " : "// ") + word.len());

            new_region.push('\n');
            for (size_t j = 0; j < tabs; ++j) {
                new_region.push('\t');
            }
            for (size_t j = 0; j < spaces; ++j) {
                new_region.push(' ');
            }
            new_region.append(doc_comment ? "/// " : "// ");

            new_region.append(word.as_str());

            current_column = word.len();
        }

        previous_spaces = 1;
        if (end_of_sentence(word.as_str())) {
            previous_spaces = 2;
        }
    }

    Transaction transaction;
    CZ_DEFER(transaction.drop());
    transaction.init(2, end.position - start.position + new_region.len());

    Edit remove;
    remove.value = buffer->contents.slice(transaction.value_allocator(), start, end.position);
    remove.position = start.position;
    remove.flags = Edit::REMOVE;
    transaction.push(remove);

    Edit insert;
    insert.value = SSOStr::from_constant(new_region.clone(transaction.value_allocator()));
    insert.position = start.position;
    insert.flags = Edit::INSERT;
    transaction.push(insert);

    transaction.commit(buffer);
}

}
}
