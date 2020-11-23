#include <ncurses.h>

int main() {
    initscr();

    start_color();
    for (size_t i = 0; i < COLORS && i + 1 < COLOR_PAIRS; ++i) {
        init_pair(i + 1, 0, i);
    }

    printw("color_test: Press any key to quit.\n");
    printw("COLORS: %d, COLOR_PAIRS: %d\n", COLORS, COLOR_PAIRS);

    attrset(A_NORMAL);
    printw(" %.3d: ", 0);

    for (size_t i = 0; i < COLORS && i + 1 < COLOR_PAIRS; ++i) {
        if ((i > 40 && (i + 20) % 36 == 0) || i == 16) {
            addch('\n');
            attrset(A_NORMAL);
            printw(" %.3d: ", i);
        }

        attrset(COLOR_PAIR(i + 1));
        addch(' ');
    }

    curs_set(0);

    getch();

    endwin();
}
