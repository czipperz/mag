#pragma once

namespace mag {
struct Editor;
struct Command_Source;

namespace markdown {

void command_reformat_paragraph(Editor* editor, Command_Source source);

}
}
