// Dispatch draft and gameplay; a failed turn is logged and still yields.
#include <cambc.h>
#include <iostream>
#include "bot.h"
#include "draft.h"

int main() {
    Bot bot;
    while (true) {
        try {
            if (cambc::is_draft())
                play_draft();
            else
                bot.play_turn();
        } catch (const std::exception& error) {
            std::cout << "bot error (turn lost): " << error.what() << std::endl;
        } catch (...) {
            std::cout << "bot error (turn lost): unknown exception" << std::endl;
        }

        cambc::end_turn();
    }
}
