#include "reformat_commands.hpp"

#include <cz/char_type.hpp>
#include "buffer.hpp"
#include "command.hpp"
#include "command_macros.hpp"
#include "editor.hpp"
#include "match.hpp"
#include "movement.hpp"
#include "reformat_commands.hpp"

namespace mag {
namespace basic {

static bool end_of_sentence(cz::Str word) {
    if (word.starts_with("(")) {
        word = word.slice_start(1);
    }

    // Special case words that aren't the end of a sentence.
    if (word.equals_case_insensitive("e.g.") || word.equals_case_insensitive("ex.") ||
        word.equals_case_insensitive("i.e.") || word.equals_case_insensitive("ie.")) {
        return false;
    }

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
            if (current_column <= word_column_goal) {
                // I think that it's really ugly to have the start of a sentence dangle on the line
                // before the sentence begins.  So I give that a big penalty.
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
    if (current_column <= word_column_goal) {
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
        } else if (max_column_length <= word_column_limit / 2) {
            // We could just combine lines at this point and be better off.
            break;
        }

        word_column_goal = max_column_length - 1;
    }

    return best_goal;
}

static bool any_patterns_match(Contents_Iterator it, size_t offset, cz::Slice<cz::Str> patterns) {
    it.advance(offset);
    for (size_t i = 0; i < patterns.len; ++i) {
        if (looking_at(it, patterns[i])) {
            return true;
        }
    }
    return false;
}

bool reformat_at(Buffer* buffer,
                 Contents_Iterator iterator,
                 cz::Str acceptable_start,
                 cz::Str acceptable_continuation,
                 cz::Slice<cz::Str> rejected_patterns) {
    size_t acceptable_continuation_offset = 0;
    for (acceptable_continuation_offset = 0;
         acceptable_continuation_offset < acceptable_continuation.len;
         ++acceptable_continuation_offset) {
        if (!cz::is_blank(acceptable_continuation[acceptable_continuation_offset])) {
            break;
        }
    }

    start_of_line_text(&iterator);

    bool stop_on_empty_lines = acceptable_start.len == 0;
    bool detect_start = acceptable_start != acceptable_continuation;
    bool at_start = false;
    if (looking_at(iterator, acceptable_start)) {
        at_start = true;
        if (any_patterns_match(iterator, acceptable_start.len, rejected_patterns)) {
            return false;
        }
    } else {
        if (iterator.position < acceptable_continuation_offset) {
            return false;
        }
        iterator.retreat(acceptable_continuation_offset);
        if (!looking_at(iterator, acceptable_continuation) ||
            any_patterns_match(iterator, acceptable_continuation.len, rejected_patterns)) {
            return false;
        }
    }

    uint64_t column = get_visual_column(buffer->mode, iterator);

    uint64_t start_position = iterator.position;
    while (!detect_start || !at_start) {
        uint64_t point = iterator.position;
        start_of_line(&iterator);
        backward_char(&iterator);
        start_of_line_text(&iterator);

        if (stop_on_empty_lines && !iterator.at_eob() && iterator.get() == '\n') {
            break;
        } else if (looking_at(iterator, acceptable_start)) {
            at_start = true;
        } else {
            if (iterator.position < acceptable_continuation_offset) {
                break;
            }
            iterator.retreat(acceptable_continuation_offset);
            if (!looking_at(iterator, acceptable_continuation) ||
                any_patterns_match(iterator, acceptable_continuation.len, rejected_patterns)) {
                break;
            }
        }

        uint64_t col = get_visual_column(buffer->mode, iterator);
        if (col != column) {
            break;
        }

        start_position = iterator.position;
        if (point == iterator.position) {
            break;
        }
    }

    iterator.advance_to(start_position);
    iterator.advance(acceptable_start.len);
    Contents_Iterator start = iterator;

    cz::Buffer_Array buffer_array;
    buffer_array.init();
    CZ_DEFER(buffer_array.drop());

    size_t words_len_sum = 0;
    size_t extra_spaces = 0;

    cz::Vector<SSOStr> words = {};
    CZ_DEFER(words.drop(cz::heap_allocator()));
    words.reserve(cz::heap_allocator(), 32);

    while (!iterator.at_eob() && cz::is_blank(iterator.get())) {
        iterator.advance();
    }

    uint64_t end_position = start_position;
    while (1) {
        // Parse words on this line.
        while (1) {
            // Parse one word.
            Contents_Iterator word_start = iterator;
            while (!iterator.at_eob() && !cz::is_space(iterator.get())) {
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
                if (ch == '\n' || !cz::is_space(ch)) {
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
        iterator.retreat(acceptable_continuation_offset);
        uint64_t col = get_visual_column(buffer->mode, iterator);
        if (col != column) {
            break;
        }
        if (!looking_at(iterator, acceptable_continuation) ||
            any_patterns_match(iterator, acceptable_continuation.len, rejected_patterns)) {
            break;
        }

        uint64_t end_position_backup = end_position;
        end_position = iterator.position;

        iterator.advance(acceptable_continuation.len);

        if (stop_on_empty_lines && !iterator.at_eob() && iterator.get() == '\n') {
            end_position = end_position_backup;
            break;
        }

        while (!iterator.at_eob() && cz::is_space(iterator.get())) {
            iterator.advance();
        }
    }

    iterator.retreat_to(end_position);
    Contents_Iterator end = iterator;
    end_of_line(&end);

    if (words.len() == 0) {
        return false;
    }

    uint64_t tabs, spaces;
    analyze_indent(buffer->mode, column, &tabs, &spaces);

    size_t word_column_limit = buffer->mode.preferred_column - column - acceptable_continuation.len;

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
            new_region.reserve(cz::heap_allocator(),
                               1 + tabs + spaces + acceptable_continuation.len + word.len());

            new_region.push('\n');
            for (size_t j = 0; j < tabs; ++j) {
                new_region.push('\t');
            }
            for (size_t j = 0; j < spaces; ++j) {
                new_region.push(' ');
            }
            new_region.append(acceptable_continuation);

            new_region.append(word.as_str());

            current_column = word.len();
        }

        previous_spaces = 1;
        if (end_of_sentence(word.as_str())) {
            previous_spaces = 2;
        }
    }

    // We already have the correct contents so don't replace them for no reason.
    if (matches(start, end.position, new_region)) {
        return true;
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
    return true;
}

void command_reformat_paragraph(Editor* editor, Command_Source source) {
    WITH_SELECTED_BUFFER(source.client);

    reformat_at(buffer, buffer->contents.iterator_at(window->cursors[0].point), "", "");
}

void command_reformat_comment_hash(Editor* editor, Command_Source source) {
    WITH_SELECTED_BUFFER(source.client);

    reformat_at(buffer, buffer->contents.iterator_at(window->cursors[0].point), "# ", "# ");
}

}
}
