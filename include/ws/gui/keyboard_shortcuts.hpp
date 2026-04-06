#pragma once

#include <string>
#include <map>
#include <functional>

namespace ws::gui {

// =============================================================================
// Key Modifier
// =============================================================================

// Keyboard modifier keys.
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

// =============================================================================
// Key Code
// =============================================================================

// Virtual key codes for keyboard input.
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

// =============================================================================
// Shortcut Definition
// =============================================================================

// Definition of a keyboard shortcut.
struct ShortcutDef {
    std::string id;                             // Unique identifier.
    std::string description;                    // Human-readable description.
    KeyCode key;                                // Key code.
    KeyModifier modifier;                       // Modifier keys.
    std::function<void()> action;               // Callback when shortcut triggered.
    bool enabled;                               // Whether shortcut is active.
};

// =============================================================================
// Keyboard Shortcut Manager
// =============================================================================

// Centralized keyboard shortcut management.
class KeyboardShortcutManager {
public:
    // Constructs the manager with default shortcuts.
    KeyboardShortcutManager();
    
    // Registers a new shortcut.
    void registerShortcut(const ShortcutDef& def);
    
    // Unregisters a shortcut by ID.
    void unregisterShortcut(const std::string& id);
    
    // Processes a key press and triggers matching shortcut.
    bool handleKeyPress(KeyCode key, KeyModifier modifiers);
    
    // Enables or disables a shortcut.
    void setShortcutEnabled(const std::string& id, bool enabled);
    
    // Returns all registered shortcuts.
    const std::vector<ShortcutDef>& getShortcuts() const;
    
    // Finds a shortcut by ID.
    ShortcutDef* findShortcut(const std::string& id);
    
    // Builds help text for display.
    std::string buildHelpText() const;
    
    // Saves shortcuts to a file.
    void saveShortcuts(const std::string& filename);
    
    // Loads shortcuts from a file.
    void loadShortcuts(const std::string& filename);

private:
    std::vector<ShortcutDef> shortcuts_;
    std::map<std::string, size_t> idToIndex_;
};

}  // namespace ws::gui
