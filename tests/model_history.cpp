#include "ws/gui/model_history.hpp"

#include <cassert>
#include <string>

int main() {
    ws::gui::ModelHistory history;

    history.recordSnapshot("initial", "{\"v\":0}");
    history.recordSnapshot("same", "{\"v\":0}");
    assert(history.getUndoCount() == 1u);

    history.recordSnapshot("edit1", "{\"v\":1}");
    history.recordSnapshot("edit2", "{\"v\":2}");
    assert(history.getUndoCount() == 3u);
    assert(!history.getCurrentState().empty());

    std::string restored;
    assert(history.undo(restored));
    assert(restored == "{\"v\":1}");
    assert(history.canRedo());

    assert(history.undo(restored));
    assert(restored == "{\"v\":0}");
    assert(!history.undo(restored));

    assert(history.redo(restored));
    assert(restored == "{\"v\":1}");
    assert(history.redo(restored));
    assert(restored == "{\"v\":2}");
    assert(!history.redo(restored));

    history.clear();
    assert(history.getUndoCount() == 0u);
    assert(history.getRedoCount() == 0u);
    return 0;
}
