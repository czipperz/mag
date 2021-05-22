#pragma once

#include "command.hpp"
#include "editor.hpp"

namespace mag {
namespace basic {

void command_uppercase_letter(Editor* editor, Command_Source source);
void command_lowercase_letter(Editor* editor, Command_Source source);

void command_uppercase_region(Editor* editor, Command_Source source);
void command_lowercase_region(Editor* editor, Command_Source source);

/// Recapitalization commands get the current token and then reformat it as the specified case.
///
/// camel -- `camelCase`.
/// pascal -- `PascalCase`.
///
/// snake -- `lower_snake_case`.
/// usnake -- `Upper_Snake_Case`.
/// ssnake -- `SCREAMING_SNAKE_CASE`.
///
/// kebab -- `lower-kebab-case`.
/// ukebab -- `Upper-Kebab-Case`.
/// skebab -- `SCREAMING-KEBAB-CASE`.
///
/// If the input text is in pascal or camel case, and multiple capital letters
/// are in a row, there are special rules.
///
/// 1. If the capital chain ends at the end of the string,
///    then the entire chain will be treated as one component.
///
///    For example,  `AnsiSwissMAP` will be converted to `ansi_swiss_map` by `to_snake`.
///
/// 2. Otherwise, the last letter of the capital chain
///    will be treated as the start of the next component.
///
///    For example, `ANSISwissMap` will be converted to `ansi_swiss_map` by `to_snake`.

/// These functions convert strings.
void to_camel(cz::Str in, cz::Allocator allocator, cz::String* out);
void to_pascal(cz::Str in, cz::Allocator allocator, cz::String* out);
void to_snake(cz::Str in, cz::Allocator allocator, cz::String* out);
void to_usnake(cz::Str in, cz::Allocator allocator, cz::String* out);
void to_ssnake(cz::Str in, cz::Allocator allocator, cz::String* out);
void to_kebab(cz::Str in, cz::Allocator allocator, cz::String* out);
void to_ukebab(cz::Str in, cz::Allocator allocator, cz::String* out);
void to_skebab(cz::Str in, cz::Allocator allocator, cz::String* out);

/// Generic command wrapper.
void command_recapitalize_token_to(Editor* editor,
                                   Client* client,
                                   void (*convert)(cz::Str, cz::Allocator, cz::String*));

/// Prompt for the output format.
void command_recapitalize_token_prompt(Editor* editor, Command_Source source);

/// These commands replace the current token.
void command_recapitalize_token_to_camel(Editor* editor, Command_Source source);
void command_recapitalize_token_to_pascal(Editor* editor, Command_Source source);
void command_recapitalize_token_to_snake(Editor* editor, Command_Source source);
void command_recapitalize_token_to_usnake(Editor* editor, Command_Source source);
void command_recapitalize_token_to_ssnake(Editor* editor, Command_Source source);
void command_recapitalize_token_to_kebab(Editor* editor, Command_Source source);
void command_recapitalize_token_to_ukebab(Editor* editor, Command_Source source);
void command_recapitalize_token_to_skebab(Editor* editor, Command_Source source);

}
}
