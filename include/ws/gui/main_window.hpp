#pragma once

namespace ws::gui {

/**
 * @brief Main GUI application coordinator.
 *
 * @details
 * `MainWindow` owns the top-level GUI runtime lifecycle for a single process run.
 * The instance is expected to be created and executed on the same thread that will
 * own the platform window and rendering context.
 *
 * Ownership and lifecycle contract:
 * - Construct once per GUI process entry.
 * - Invoke `run()` at most once per instance.
 * - Destroy after `run()` returns.
 * - This type is intentionally non-copyable and non-movable to keep ownership explicit.
 */
class MainWindow {
public:
    /**
     * @brief Constructs the GUI main-window coordinator.
     *
     * @note Dependency injection is currently internal to the implementation.
     *       If external dependencies are introduced, they must be passed via
     *       constructor parameters rather than hidden global state.
     */
    MainWindow() = default;
    MainWindow(const MainWindow&) = delete;
    MainWindow& operator=(const MainWindow&) = delete;
    MainWindow(MainWindow&&) = delete;
    MainWindow& operator=(MainWindow&&) = delete;
    ~MainWindow() = default;

    /**
     * @brief Runs the GUI event/render loop until shutdown.
     *
     * @return Process-style exit code (`0` for normal shutdown, non-zero for failure).
     *
     * @post The main window and associated runtime resources are released before return.
     *
     * @warning This call is blocking and must execute on the GUI thread.
     *          Callers should invoke it only from process entry paths that are prepared
     *          to own the window loop.
     *
     * @throws std::exception May propagate implementation failures; callers should
     *         handle exceptions at the process boundary and translate them into a
     *         stable exit code.
     */
    int run();
};

} // namespace ws::gui
