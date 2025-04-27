#pragma once

namespace mag {
struct Buffer;
struct Client;
struct Command_Source;
struct Contents_Iterator;
struct Editor;

namespace markdown {

void command_reformat_paragraph(Editor* editor, Command_Source source);
void command_reformat_paragraph_or_hash_comment(Editor* editor, Command_Source source);

void reformat_at(Client* client, Buffer* buffer, Contents_Iterator iterator);

}
}
