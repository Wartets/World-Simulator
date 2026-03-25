#include "ws/gui/main_window.hpp"

#include "ws/app/shell_support.hpp"
#include "ws/gui/runtime_service.hpp"

#include <windows.h>
#include <commctrl.h>

#include <array>
#include <sstream>
#include <string>
#include <vector>

namespace ws::gui {
namespace {

constexpr const char* kWindowClass = "WorldSimGuiWindow";

enum ControlId : int {
    IdTab = 100,
    IdLog = 101,

    IdSessionStart = 200,
    IdSessionStop = 201,
    IdSessionRestart = 202,
    IdSeedEdit = 203,
    IdGridWEdit = 204,
    IdGridHEdit = 205,
    IdApplyConfig = 206,

    IdTierCombo = 300,
    IdTemporalCombo = 301,
    IdProfileNameEdit = 302,
    IdProfileSave = 303,
    IdProfileLoad = 304,
    IdProfileList = 305,

    IdStepCountEdit = 400,
    IdStepRun = 401,
    IdRunUntilEdit = 402,
    IdRunUntil = 403,
    IdPause = 404,
    IdResume = 405,

    IdStatus = 500,
    IdMetrics = 501,
    IdFields = 502,
    IdSummaryVarEdit = 503,
    IdSummary = 504,
    IdCheckpointLabelEdit = 505,
    IdCheckpointCreate = 506,
    IdCheckpointRestore = 507,
    IdCheckpointList = 508
};

struct ControlBinding {
    HWND handle = nullptr;
    int tabIndex = 0;
};

std::string getWindowText(HWND hwnd) {
    const int length = GetWindowTextLengthA(hwnd);
    if (length <= 0) {
        return {};
    }

    std::string value;
    value.resize(static_cast<std::size_t>(length));
    GetWindowTextA(hwnd, value.data(), length + 1);
    return value;
}

void setWindowText(HWND hwnd, const std::string& value) {
    SetWindowTextA(hwnd, value.c_str());
}

class MainWindowImpl {
public:
    int run() {
        INITCOMMONCONTROLSEX initCommonControls{};
        initCommonControls.dwSize = sizeof(INITCOMMONCONTROLSEX);
        initCommonControls.dwICC = ICC_TAB_CLASSES;
        InitCommonControlsEx(&initCommonControls);

        HINSTANCE instance = GetModuleHandleA(nullptr);
        WNDCLASSA windowClass{};
        windowClass.lpfnWndProc = &MainWindowImpl::WindowProc;
        windowClass.hInstance = instance;
        windowClass.lpszClassName = kWindowClass;
        windowClass.hCursor = LoadCursor(nullptr, IDC_ARROW);
        windowClass.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);

        RegisterClassA(&windowClass);

        hwnd_ = CreateWindowExA(
            0,
            kWindowClass,
            "World Simulator - Graphical Control Surface",
            WS_OVERLAPPEDWINDOW,
            CW_USEDEFAULT,
            CW_USEDEFAULT,
            1180,
            760,
            nullptr,
            nullptr,
            instance,
            this);

        if (!hwnd_) {
            return 1;
        }

        ShowWindow(hwnd_, SW_SHOW);
        UpdateWindow(hwnd_);

        std::string startupMessage;
        runtime_.start(startupMessage);
        appendLog(startupMessage);
        refreshConfigControls();

        MSG msg{};
        while (GetMessageA(&msg, nullptr, 0, 0) > 0) {
            TranslateMessage(&msg);
            DispatchMessageA(&msg);
        }

        return static_cast<int>(msg.wParam);
    }

private:
    static LRESULT CALLBACK WindowProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam) {
        if (message == WM_NCCREATE) {
            const auto* cs = reinterpret_cast<CREATESTRUCTA*>(lParam);
            auto* self = static_cast<MainWindowImpl*>(cs->lpCreateParams);
            SetWindowLongPtrA(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
            self->hwnd_ = hwnd;
        }

        auto* self = reinterpret_cast<MainWindowImpl*>(GetWindowLongPtrA(hwnd, GWLP_USERDATA));
        if (!self) {
            return DefWindowProcA(hwnd, message, wParam, lParam);
        }

        switch (message) {
            case WM_CREATE:
                self->createControls();
                return 0;
            case WM_SIZE:
                self->layoutControls(LOWORD(lParam), HIWORD(lParam));
                return 0;
            case WM_NOTIFY:
                self->handleNotify(reinterpret_cast<LPNMHDR>(lParam));
                return 0;
            case WM_COMMAND:
                self->handleCommand(LOWORD(wParam));
                return 0;
            case WM_CLOSE:
                DestroyWindow(hwnd);
                return 0;
            case WM_DESTROY:
                PostQuitMessage(0);
                return 0;
            default:
                return DefWindowProcA(hwnd, message, wParam, lParam);
        }
    }

    void createControls() {
        tab_ = CreateWindowExA(0, WC_TABCONTROLA, "", WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS,
            10, 10, 740, 260, hwnd_, reinterpret_cast<HMENU>(IdTab), nullptr, nullptr);

        std::array<const char*, 4> tabNames = {
            "Session",
            "Profile",
            "Simulation",
            "Analysis"
        };

        for (const auto* name : tabNames) {
            TCITEMA item{};
            item.mask = TCIF_TEXT;
            item.pszText = const_cast<char*>(name);
            TabCtrl_InsertItem(tab_, TabCtrl_GetItemCount(tab_), &item);
        }

        log_ = CreateWindowExA(WS_EX_CLIENTEDGE, "EDIT", "",
            WS_CHILD | WS_VISIBLE | ES_MULTILINE | ES_AUTOVSCROLL | ES_READONLY | WS_VSCROLL,
            10, 280, 1140, 430, hwnd_, reinterpret_cast<HMENU>(IdLog), nullptr, nullptr);

        createSessionTabControls();
        createProfileTabControls();
        createSimulationTabControls();
        createAnalysisTabControls();
        updateTabVisibility();
    }

    HWND createTabControl(
        int tabIndex,
        const char* className,
        const char* text,
        DWORD style,
        int x,
        int y,
        int width,
        int height,
        int controlId) {
        RECT rc{};
        GetClientRect(tab_, &rc);
        const int controlX = rc.left + x;
        const int controlY = rc.top + y;

        HWND control = CreateWindowExA(
            0,
            className,
            text,
            WS_CHILD | style,
            controlX,
            controlY,
            width,
            height,
            hwnd_,
            reinterpret_cast<HMENU>(controlId),
            nullptr,
            nullptr);

        tabControls_.push_back(ControlBinding{control, tabIndex});
        return control;
    }

    void createSessionTabControls() {
        createTabControl(0, "STATIC", "Seed", WS_VISIBLE, 24, 42, 80, 22, -1);
        seedEdit_ = createTabControl(0, "EDIT", "42", WS_VISIBLE | WS_BORDER, 110, 40, 110, 24, IdSeedEdit);

        createTabControl(0, "STATIC", "Grid W", WS_VISIBLE, 240, 42, 80, 22, -1);
        gridWEdit_ = createTabControl(0, "EDIT", "32", WS_VISIBLE | WS_BORDER, 300, 40, 80, 24, IdGridWEdit);

        createTabControl(0, "STATIC", "Grid H", WS_VISIBLE, 400, 42, 80, 22, -1);
        gridHEdit_ = createTabControl(0, "EDIT", "16", WS_VISIBLE | WS_BORDER, 460, 40, 80, 24, IdGridHEdit);

        createTabControl(0, "BUTTON", "Apply Config", WS_VISIBLE | BS_PUSHBUTTON, 570, 38, 120, 28, IdApplyConfig);

        createTabControl(0, "BUTTON", "Start", WS_VISIBLE | BS_PUSHBUTTON, 110, 88, 110, 28, IdSessionStart);
        createTabControl(0, "BUTTON", "Stop", WS_VISIBLE | BS_PUSHBUTTON, 240, 88, 110, 28, IdSessionStop);
        createTabControl(0, "BUTTON", "Restart", WS_VISIBLE | BS_PUSHBUTTON, 370, 88, 110, 28, IdSessionRestart);
    }

    void createProfileTabControls() {
        createTabControl(1, "STATIC", "Tier", WS_VISIBLE, 24, 42, 80, 22, -1);
        tierCombo_ = createTabControl(1, "COMBOBOX", "", WS_VISIBLE | WS_BORDER | CBS_DROPDOWNLIST, 110, 40, 130, 200, IdTierCombo);
        SendMessageA(tierCombo_, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>("A"));
        SendMessageA(tierCombo_, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>("B"));
        SendMessageA(tierCombo_, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>("C"));

        createTabControl(1, "STATIC", "Temporal", WS_VISIBLE, 270, 42, 80, 22, -1);
        temporalCombo_ = createTabControl(1, "COMBOBOX", "", WS_VISIBLE | WS_BORDER | CBS_DROPDOWNLIST, 350, 40, 180, 220, IdTemporalCombo);
        SendMessageA(temporalCombo_, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>("uniform"));
        SendMessageA(temporalCombo_, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>("phased"));
        SendMessageA(temporalCombo_, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>("multirate"));

        createTabControl(1, "STATIC", "Profile Name", WS_VISIBLE, 24, 90, 100, 22, -1);
        profileNameEdit_ = createTabControl(1, "EDIT", "baseline", WS_VISIBLE | WS_BORDER, 130, 88, 220, 24, IdProfileNameEdit);

        createTabControl(1, "BUTTON", "Save", WS_VISIBLE | BS_PUSHBUTTON, 370, 86, 80, 28, IdProfileSave);
        createTabControl(1, "BUTTON", "Load", WS_VISIBLE | BS_PUSHBUTTON, 460, 86, 80, 28, IdProfileLoad);
        createTabControl(1, "BUTTON", "List", WS_VISIBLE | BS_PUSHBUTTON, 550, 86, 80, 28, IdProfileList);
    }

    void createSimulationTabControls() {
        createTabControl(2, "STATIC", "Step Count", WS_VISIBLE, 24, 42, 100, 22, -1);
        stepCountEdit_ = createTabControl(2, "EDIT", "1", WS_VISIBLE | WS_BORDER, 130, 40, 100, 24, IdStepCountEdit);
        createTabControl(2, "BUTTON", "Run Step", WS_VISIBLE | BS_PUSHBUTTON, 250, 38, 110, 28, IdStepRun);

        createTabControl(2, "STATIC", "Run Until", WS_VISIBLE, 24, 90, 100, 22, -1);
        runUntilEdit_ = createTabControl(2, "EDIT", "100", WS_VISIBLE | WS_BORDER, 130, 88, 100, 24, IdRunUntilEdit);
        createTabControl(2, "BUTTON", "Run Until", WS_VISIBLE | BS_PUSHBUTTON, 250, 86, 110, 28, IdRunUntil);

        createTabControl(2, "BUTTON", "Pause", WS_VISIBLE | BS_PUSHBUTTON, 390, 38, 100, 28, IdPause);
        createTabControl(2, "BUTTON", "Resume", WS_VISIBLE | BS_PUSHBUTTON, 390, 86, 100, 28, IdResume);
    }

    void createAnalysisTabControls() {
        createTabControl(3, "BUTTON", "Status", WS_VISIBLE | BS_PUSHBUTTON, 24, 38, 100, 28, IdStatus);
        createTabControl(3, "BUTTON", "Metrics", WS_VISIBLE | BS_PUSHBUTTON, 140, 38, 100, 28, IdMetrics);
        createTabControl(3, "BUTTON", "Fields", WS_VISIBLE | BS_PUSHBUTTON, 256, 38, 100, 28, IdFields);

        createTabControl(3, "STATIC", "Variable", WS_VISIBLE, 24, 86, 80, 22, -1);
        summaryVarEdit_ = createTabControl(3, "EDIT", "temperature_t", WS_VISIBLE | WS_BORDER, 110, 84, 180, 24, IdSummaryVarEdit);
        createTabControl(3, "BUTTON", "Summary", WS_VISIBLE | BS_PUSHBUTTON, 308, 82, 100, 28, IdSummary);

        createTabControl(3, "STATIC", "Checkpoint", WS_VISIBLE, 24, 130, 80, 22, -1);
        checkpointLabelEdit_ = createTabControl(3, "EDIT", "quick", WS_VISIBLE | WS_BORDER, 110, 128, 180, 24, IdCheckpointLabelEdit);
        createTabControl(3, "BUTTON", "Create", WS_VISIBLE | BS_PUSHBUTTON, 308, 126, 100, 28, IdCheckpointCreate);
        createTabControl(3, "BUTTON", "Restore", WS_VISIBLE | BS_PUSHBUTTON, 424, 126, 100, 28, IdCheckpointRestore);
        createTabControl(3, "BUTTON", "List", WS_VISIBLE | BS_PUSHBUTTON, 540, 126, 100, 28, IdCheckpointList);
    }

    void layoutControls(const int width, const int height) {
        const int margin = 10;
        const int tabHeight = 260;
        MoveWindow(tab_, margin, margin, width - (2 * margin), tabHeight, TRUE);
        MoveWindow(log_, margin, tabHeight + (2 * margin), width - (2 * margin), height - tabHeight - (3 * margin), TRUE);

        const int currentTab = TabCtrl_GetCurSel(tab_);
        for (const auto& binding : tabControls_) {
            ShowWindow(binding.handle, binding.tabIndex == currentTab ? SW_SHOW : SW_HIDE);
        }
    }

    void updateTabVisibility() {
        if (!tab_) {
            return;
        }

        const int currentTab = TabCtrl_GetCurSel(tab_);
        for (const auto& binding : tabControls_) {
            ShowWindow(binding.handle, binding.tabIndex == currentTab ? SW_SHOW : SW_HIDE);
        }
    }

    void handleNotify(const LPNMHDR notifyHeader) {
        if (!notifyHeader || notifyHeader->idFrom != IdTab) {
            return;
        }

        if (notifyHeader->code == TCN_SELCHANGE) {
            updateTabVisibility();
        }
    }

    void appendLog(const std::string& line) {
        std::string existing = getWindowText(log_);
        if (!existing.empty()) {
            existing.append("\r\n");
        }
        existing.append(line);
        setWindowText(log_, existing);
        SendMessageA(log_, EM_LINESCROLL, 0, 0x7FFFFFFF);
    }

    void refreshConfigControls() {
        const auto& config = runtime_.config();

        setWindowText(seedEdit_, std::to_string(config.seed));
        setWindowText(gridWEdit_, std::to_string(config.grid.width));
        setWindowText(gridHEdit_, std::to_string(config.grid.height));

        const int tierIndex =
            (config.tier == ModelTier::A) ? 0 :
            (config.tier == ModelTier::B) ? 1 : 2;
        SendMessageA(tierCombo_, CB_SETCURSEL, static_cast<WPARAM>(tierIndex), 0);

        const std::string temporal = app::temporalPolicyToString(config.temporalPolicy);
        const int temporalIndex = (temporal == "uniform") ? 0 : (temporal == "phased") ? 1 : 2;
        SendMessageA(temporalCombo_, CB_SETCURSEL, static_cast<WPARAM>(temporalIndex), 0);
    }

    void applyConfigFromControls() {
        app::LaunchConfig config = runtime_.config();

        const auto seed = app::parseU64(getWindowText(seedEdit_));
        const auto gridW = app::parseU32(getWindowText(gridWEdit_));
        const auto gridH = app::parseU32(getWindowText(gridHEdit_));
        if (!seed.has_value() || !gridW.has_value() || !gridH.has_value() || *gridW == 0 || *gridH == 0) {
            appendLog("config_apply_failed invalid numeric values");
            return;
        }

        config.seed = *seed;
        config.grid = GridSpec{*gridW, *gridH};

        char tierBuffer[16]{};
        const int tierSelection = static_cast<int>(SendMessageA(tierCombo_, CB_GETCURSEL, 0, 0));
        SendMessageA(tierCombo_, CB_GETLBTEXT, static_cast<WPARAM>(tierSelection), reinterpret_cast<LPARAM>(tierBuffer));
        const auto tier = app::parseTier(tierBuffer);
        if (tier.has_value()) {
            config.tier = *tier;
        }

        char temporalBuffer[32]{};
        const int temporalSelection = static_cast<int>(SendMessageA(temporalCombo_, CB_GETCURSEL, 0, 0));
        SendMessageA(temporalCombo_, CB_GETLBTEXT, static_cast<WPARAM>(temporalSelection), reinterpret_cast<LPARAM>(temporalBuffer));
        const auto temporal = app::parseTemporalPolicy(temporalBuffer);
        if (temporal.has_value()) {
            config.temporalPolicy = *temporal;
        }

        runtime_.setConfig(config);

        std::ostringstream message;
        message << "config_applied seed=" << config.seed
                << " grid=" << config.grid.width << 'x' << config.grid.height
                << " tier=" << toString(config.tier)
                << " temporal=" << app::temporalPolicyToString(config.temporalPolicy);
        appendLog(message.str());
    }

    void handleCommand(const int commandId) {
        std::string message;

        switch (commandId) {
            case IdApplyConfig:
                applyConfigFromControls();
                return;
            case IdSessionStart:
                runtime_.start(message);
                break;
            case IdSessionStop:
                runtime_.stop(message);
                break;
            case IdSessionRestart:
                runtime_.restart(message);
                break;
            case IdProfileSave:
                runtime_.saveProfile(getWindowText(profileNameEdit_), message);
                break;
            case IdProfileLoad:
                runtime_.loadProfile(getWindowText(profileNameEdit_), message);
                refreshConfigControls();
                break;
            case IdProfileList:
                runtime_.listProfiles(message);
                break;
            case IdStepRun: {
                const auto count = app::parseU32(getWindowText(stepCountEdit_));
                if (!count.has_value() || *count == 0) {
                    message = "invalid step count";
                } else {
                    runtime_.step(*count, message);
                }
                break;
            }
            case IdRunUntil: {
                const auto target = app::parseU64(getWindowText(runUntilEdit_));
                if (!target.has_value()) {
                    message = "invalid run-until target";
                } else {
                    runtime_.runUntil(*target, message);
                }
                break;
            }
            case IdPause:
                runtime_.pause(message);
                break;
            case IdResume:
                runtime_.resume(message);
                break;
            case IdStatus:
                runtime_.status(message);
                break;
            case IdMetrics:
                runtime_.metrics(message);
                break;
            case IdFields:
                runtime_.listFields(message);
                break;
            case IdSummary:
                runtime_.summarizeField(getWindowText(summaryVarEdit_), message);
                break;
            case IdCheckpointCreate:
                runtime_.createCheckpoint(getWindowText(checkpointLabelEdit_), message);
                break;
            case IdCheckpointRestore:
                runtime_.restoreCheckpoint(getWindowText(checkpointLabelEdit_), message);
                break;
            case IdCheckpointList:
                runtime_.listCheckpoints(message);
                break;
            default:
                return;
        }

        appendLog(message);
    }

    HWND hwnd_ = nullptr;
    HWND tab_ = nullptr;
    HWND log_ = nullptr;

    HWND seedEdit_ = nullptr;
    HWND gridWEdit_ = nullptr;
    HWND gridHEdit_ = nullptr;

    HWND tierCombo_ = nullptr;
    HWND temporalCombo_ = nullptr;
    HWND profileNameEdit_ = nullptr;

    HWND stepCountEdit_ = nullptr;
    HWND runUntilEdit_ = nullptr;

    HWND summaryVarEdit_ = nullptr;
    HWND checkpointLabelEdit_ = nullptr;

    std::vector<ControlBinding> tabControls_;
    RuntimeService runtime_;
};

} // namespace

int MainWindow::run() {
    MainWindowImpl impl;
    return impl.run();
}

} // namespace ws::gui
