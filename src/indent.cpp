#include "indent.hpp"

#include "basic/token_movement_commands.hpp"
#include "buffer.hpp"
#include "movement.hpp"

namespace mag {

using namespace basic;

uint64_t find_indent_width(Buffer* buffer, Contents_Iterator it) {
    return find_indent_width(buffer, it, buffer->mode.discover_indent_policy);
}

uint64_t find_indent_width(Buffer* buffer,
                           Contents_Iterator it,
                           Discover_Indent_Policy discover_indent_policy) {
    uint64_t start_position = it.position;

    switch (discover_indent_policy) {
    case Discover_Indent_Policy::UP_THEN_BACK_PAIR: {
        if (!backward_up_token_pair(buffer, &it, /*non_pair=*/false)) {
            return 0;
        }

        // If the open pair token is at the end of the line then indent starting after it.
        uint64_t state;
        Token token;
        Contents_Iterator token_iterator = it;
        if (!get_token_after_position(buffer, &token_iterator, &state, &token)) {
            token.end = it.position;
        }
        token_iterator.retreat_to(token.end);
        if (!at_end_of_line(token_iterator) && token.end != start_position) {
            return get_visual_column(buffer->mode, token_iterator);
        }
    }  // fallthrough

    case Discover_Indent_Policy::BACK_PAIR: {
        uint64_t base_columns = buffer->mode.indent_width;

        // Skip empty lines.
        if (discover_indent_policy == Discover_Indent_Policy::BACK_PAIR) {
            base_columns = 0;
            while (!it.at_bob() && at_start_of_line(it)) {
                it.retreat();
            }
        }

        // This is super inefficient but it's not invoked programmatically so it's not a big deal.
        while (1) {
            // If at the start of the line then copy it.
            Contents_Iterator solt = it;
            start_of_line_text(&solt);
            if (solt.position == it.position) {
                return get_visual_column(buffer->mode, it) + base_columns;
            }

            // We found an open pair but no other tokens so just indent one level.
            uint64_t state;
            Token token;
            if (!get_token_before_position(buffer, &it, &state, &token)) {
                return base_columns;
            }

            // Indent starting after the open pair token.  We hit this case
            // if the open pair we found above was at the end of the line.
            if (token.type == Token_Type::OPEN_PAIR) {
                it.retreat_to(token.end);
                if (at_end_of_line(it) || token.end == start_position) {
                    base_columns = buffer->mode.indent_width;
                } else {
                    return get_visual_column(buffer->mode, it);
                }
            }

            it.retreat_to(token.start);

            // Find the corresponding token.
            if (token.type == Token_Type::CLOSE_PAIR) {
                backward_up_token_pair(buffer, &it, /*non_pair=*/false);
            }
        }
    }

    case Discover_Indent_Policy::COPY_PREVIOUS_LINE: {
        start_of_line(&it);

        // Skip empty lines.
        if (at_end_of_line(it)) {
            do {
                // If at bob then we found no indent.
                if (it.at_bob()) {
                    return 0;
                }

                it.retreat();
                continue;
            } while (at_start_of_line(it));
        }

        start_of_line(&it);

        Contents_Iterator indent = it;
        forward_through_whitespace(&indent);

        // If there is any indent this line then stop.
        return count_visual_columns(buffer->mode, it, indent.position);
    }

    default:
        CZ_PANIC("invalid Discover_Indent_Policy");
    }
}

}
