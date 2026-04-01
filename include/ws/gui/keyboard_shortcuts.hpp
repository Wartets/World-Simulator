#pragma once

#include <string>
#include <map>
#include <functional>

/**
 * @file keyboard_shortcuts.hpp
 * @brief Keyboard shortcut manager for GUI layer
 * 
 * Provides centralized keyboard shortcut registration and dispatch.
 * All shortcuts are non-blocking and context-aware.
 */

namespace ws::gui {

/**
 * @enum KeyModifier
 * @brief Keyboard modifiers (Ctrl, Shift, Alt)
 */
enum class KeyModifier {
    None = 0,
    Ctrl = 1 << 0,
    Shift = 1 << 1,
    Alt = 1 << 2,
    CtrlShift = Ctrl | Shift,
    CtrlAlt = Ctrl | Alt,
    ShiftAlt = Shift | Alt,
    CtrlShiftAlt = Ctrl | Shift | Alt
};

/**
 * @enum KeyCode
 * @brief Virtual key codes (subset of important keys)
 */
enum class KeyCode {
    Unknown = 0,
    
    // Letters
    A = 'A', B = 'B', C = 'C', D = 'D', E = 'E', F = 'F',
    G = 'G', H = 'H', I = 'I', J = 'J', K = 'K', L = 'L',
    M = 'M', N = 'N', O = 'O', P = 'P', Q = 'Q', R = 'R',
    S = 'S', T = 'T', U = 'U', V = 'V', W = 'W', X = 'X',
    Y = 'Y', Z = 'Z',
    
    // Numbers
    Num0 = '0', Num1 = '1', Num2 = '2', Num3 = '3', Num4 = '4',
    Num5 = '5', Num6 = '6', Num7 = '7', Num8 = '8', Num9 = '9',
    
    // Function keys
    F1 = 0x70, F2 = 0x71, F3 = 0x72, F4 = 0x73, F5 = 0x74,
    F6 = 0x75, F7 = 0x76, F8 = 0x77, F9 = 0x78, F10 = 0x79,
    F11 = 0x7A, F12 = 0x7B,
    
    // Navigation
    Left = 0x25, Right = 0x27, Up = 0x26, Down = 0x28,
    Home = 0x24, End = 0x23, PageUp = 0x21, PageDown = 0x22,
    
    // Other
    Space = ' ', Tab = 0x09, Enter = 0x0D, Escape = 0x1B,
    Delete = 0x2E, Backspace = 0x08
};

/**
 * @struct ShortcutDef
 * @brief Definition of a keyboard shortcut
 */
struct ShortcutDef {
    std::string id;              ///< Unique identifier
    std::string description;     ///< Human-readable description
    KeyCode key;                 ///< Key code
    KeyModifier modifier;        ///< Modifier keys
    std::function<void()> action; ///< Callback when shortcut triggered
    bool enabled;                ///< Whether shortcut is active
};

/**
 * @class KeyboardShortcutManager
 * @brief Centralized keyboard shortcut management
 * 
 * Features:
 * - Register/unregister shortcuts dynamically
 * - Context-aware enable/disable
 * - Query shortcuts and build help text
 * - ImGui integration for shortcut remapping
 */
class KeyboardShortcutManager {
public:
    /// Initialize with default shortcuts
    KeyboardShortcutManager();
    
    /**
     * @brief Register a new shortcut
     * @param def Shortcut definition
     */
    void registerShortcut(const ShortcutDef& def);
    
    /**
     * @brief Unregister a shortcut
     * @param id Shortcut ID
     */
    void unregisterShortcut(const std::string& id);
    
    /**
     * @brief Process keyboard event
     * @param key Key code pressed
     * @param modifiers Active modifiers
     * @return true if shortcut was handled
     */
    bool handleKeyPress(KeyCode key, KeyModifier modifiers);
    
    /**
     * @brief Enable/disable shortcut
     * @param id Shortcut ID
     * @param enabled Enable state
     */
    void setShortcutEnabled(const std::string& id, bool enabled);
    
    /**
     * @brief Get all registered shortcuts
     * @return Vector of shortcut definitions
     */
    const std::vector<ShortcutDef>& getShortcuts() const;
    
    /**
     * @brief Get shortcut by ID
     * @param id Shortcut ID
     * @return ShortcutDef or nullptr
     */
    ShortcutDef* findShortcut(const std::string& id);
    
    /**
     * @brief Build help text for display
     * @return Formatted shortcut help string
     */
    std::string buildHelpText() const;
    
    /**
     * @brief Persist shortcuts to file
     * @param filename Target file path
     */
    void saveShortcuts(const std::string& filename);
    
    /**
     * @brief Load shortcuts from file
     * @param filename Source file path
     */
    void loadShortcuts(const std::string& filename);

private:
    std::vector<ShortcutDef> shortcuts_;
    std::map<std::string, size_t> idToIndex_;
};

}  // namespace ws::gui
