#include <ncurses.h>

int main() {
    initscr();

    start_color();
    for (size_t i = 0; i < COLORS && i + 1 < COLOR_PAIRS; ++i) {
        init_pair(i + 1, i, 0);
    }

    printw("color_test: Press any key to quit.\n");
    printw("COLORS: %d, COLOR_PAIRS: %d\n", COLORS, COLOR_PAIRS);

    attrset(A_NORMAL);
    printw(" %.3d: ", 0);

    for (size_t i; i < COLORS && i + 1 < COLOR_PAIRS; ++i) {
        if ((i + 1) % 6 == 0) {
            addch('\n');
            attrset(A_NORMAL);
            printw(" %.3d: ", i);
        }

        attrset(COLOR_PAIR(i));
        addch('X');
    }

    curs_set(0);

    getch();

    endwin();
}
