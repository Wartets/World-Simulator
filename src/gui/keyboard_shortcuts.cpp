#include "ws/gui/keyboard_shortcuts.hpp"

namespace ws::gui {

// Constructs keyboard shortcut manager with default application shortcuts.
// Registers built-in shortcuts for play/pause, step, checkpoint, model editor, help, speed.
KeyboardShortcutManager::KeyboardShortcutManager() {
    // Register default shortcuts
    registerShortcut({
        "play_pause",
        "Toggle play/pause",
        KeyCode::Space,
        KeyModifier::None,
        nullptr,
        true
    });
    
    registerShortcut({
        "step_forward",
        "Step forward one frame",
        KeyCode::Right,
        KeyModifier::Ctrl,
        nullptr,
        true
    });
    
    registerShortcut({
        "step_backward",
        "Step backward one frame",
        KeyCode::Left,
        KeyModifier::Ctrl,
        nullptr,
        true
    });
    
    registerShortcut({
        "save_checkpoint",
        "Save checkpoint",
        KeyCode::S,
        KeyModifier::Ctrl,
        nullptr,
        true
    });
    
    registerShortcut({
        "load_checkpoint",
        "Load checkpoint",
        KeyCode::L,
        KeyModifier::Ctrl,
        nullptr,
        true
    });
    
    registerShortcut({
        "model_editor",
        "Open model editor",
        KeyCode::M,
        KeyModifier::Ctrl,
        nullptr,
        true
    });
    
    registerShortcut({
        "help",
        "Show help/shortcuts",
        KeyCode::F1,
        KeyModifier::None,
        nullptr,
        true
    });
    
    registerShortcut({
        "speed_half",
        "Half speed (0.5x)",
        KeyCode::Num1,
        KeyModifier::Ctrl,
        nullptr,
        true
    });
    
    registerShortcut({
        "speed_normal",
        "Normal speed (1.0x)",
        KeyCode::Num2,
        KeyModifier::Ctrl,
        nullptr,
        true
    });
    
    registerShortcut({
        "speed_double",
        "Double speed (2.0x)",
        KeyCode::Num3,
        KeyModifier::Ctrl,
        nullptr,
        true
    });
}

// Registers a keyboard shortcut with the manager.
// @param def Shortcut definition with ID, description, key, modifier, and action
void KeyboardShortcutManager::registerShortcut(const ShortcutDef& def) {
    idToIndex_[def.id] = shortcuts_.size();
    shortcuts_.push_back(def);
}

// Unregisters a keyboard shortcut by ID.
// Updates internal index mapping after removal.
// @param id Shortcut identifier to remove
void KeyboardShortcutManager::unregisterShortcut(const std::string& id) {
    auto it = idToIndex_.find(id);
    if (it != idToIndex_.end()) {
        size_t idx = it->second;
        idToIndex_.erase(it);
        shortcuts_.erase(shortcuts_.begin() + idx);
        // Update indices
        for (auto& [key, index] : idToIndex_) {
            if (index > idx) index--;
        }
    }
}

// Handles key press event and invokes matching shortcut action.
// Searches enabled shortcuts matching key and modifier.
// @param key Key code that was pressed
// @param modifiers Modifier keys held during press
// @return true if shortcut was matched and executed
bool KeyboardShortcutManager::handleKeyPress(KeyCode key, KeyModifier modifiers) {
    for (auto& shortcut : shortcuts_) {
        if (shortcut.enabled && shortcut.key == key && shortcut.modifier == modifiers) {
            if (shortcut.action) {
                shortcut.action();
            }
            return true;
        }
    }
    return false;
}

// Enables or disables a shortcut by ID.
// @param id Shortcut identifier
// @param enabled true to enable, false to disable
void KeyboardShortcutManager::setShortcutEnabled(const std::string& id, bool enabled) {
    auto* shortcut = findShortcut(id);
    if (shortcut) {
        shortcut->enabled = enabled;
    }
}

// Gets all registered shortcuts.
// @return Const reference to shortcut vector
const std::vector<ShortcutDef>& KeyboardShortcutManager::getShortcuts() const {
    return shortcuts_;
}

// Finds shortcut definition by ID.
// @param id Shortcut identifier to search for
// @return Pointer to shortcut definition, nullptr if not found
ShortcutDef* KeyboardShortcutManager::findShortcut(const std::string& id) {
    auto it = idToIndex_.find(id);
    if (it != idToIndex_.end()) {
        return &shortcuts_[it->second];
    }
    return nullptr;
}

// Builds formatted help text listing all enabled shortcuts.
// @return Multi-line string with shortcut descriptions and key bindings
std::string KeyboardShortcutManager::buildHelpText() const {
    std::string help = "Keyboard Shortcuts:\n\n";
    for (const auto& shortcut : shortcuts_) {
        if (!shortcut.enabled) continue;
        help += shortcut.description + ": ";
        
        // Format modifier keys
        if (shortcut.modifier == KeyModifier::Ctrl) help += "Ctrl+";
        else if (shortcut.modifier == KeyModifier::Shift) help += "Shift+";
        else if (shortcut.modifier == KeyModifier::Alt) help += "Alt+";
        else if (shortcut.modifier == KeyModifier::CtrlShift) help += "Ctrl+Shift+";
        
        // Format key
        char keyChar = static_cast<char>(shortcut.key);
        help += keyChar;
        help += "\n";
    }
    return help;
}

// Saves shortcut configuration to JSON file.
// @param filename Output file path
void KeyboardShortcutManager::saveShortcuts(const std::string& filename) {
    // TODO: Implement JSON serialization
}

// Loads shortcut configuration from JSON file.
// @param filename Input file path
void KeyboardShortcutManager::loadShortcuts(const std::string& filename) {
    // TODO: Implement JSON deserialization
}

}  // namespace ws::gui
