#include "ws/app/runtime_shell.hpp"

#include "ws/app/checkpoint_io.hpp"
#include "ws/app/profile_store.hpp"
#include "ws/app/shell_support.hpp"
#include "ws/app/world_store.hpp"
#include "ws/core/runtime.hpp"
#include "ws/core/subsystems/subsystems.hpp"

#include <algorithm>
#include <chrono>
#include <exception>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <map>
#include <memory>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace ws::app {

namespace {

class WorldSimApp {
public:
    int run() {
        printBanner();
        startSession();

        std::string line;
        while (true) {
            printPrompt();
            if (!std::getline(std::cin, line)) {
                std::cout << "\ninput stream closed; exiting.\n";
                shutdown();
                return 0;
            }

            if (!handleCommand(line)) {
                shutdown();
                return 0;
            }
        }
    }

private:
    void printBanner() const {
        std::cout << "World Simulator Runtime Shell\n";
        std::cout << "Type 'help' for commands; type 'dashboard' for session overview.\n";
        std::cout << "Model selection is scoped with 'model' and 'world' commands.\n";
    }

    void printPrompt() const {
        if (!runtime_ || runtime_->status() != RuntimeStatus::Running) {
            std::cout << "world-sim[model=" << currentModelKey() << ",stopped]> " << std::flush;
            return;
        }

        const auto& snapshot = runtime_->snapshot();
        std::cout << "world-sim[step=" << snapshot.stateHeader.stepIndex
                  << ",model=" << currentModelKey()
                  << ",tier=" << toString(launchConfig_.tier)
                  << "]> " << std::flush;
    }

    void printHelp() const {
        std::cout
            << "Runtime control:\n"
            << "  help                                 Show this help\n"
            << "  dashboard                            Show rich session dashboard\n"
            << "  status                               Show runtime snapshot summary\n"
            << "  step [count]                         Controlled step (default count=1)\n"
            << "  run [count]                          Alias for step [count]\n"
            << "  rununtil <step_index>                Step until absolute step index\n"
            << "  bench <steps>                        Run timed stepping benchmark\n"
            << "  pause | resume                       Pause/resume runtime\n"
            << "  restart                              Start a fresh runtime session\n"
            << "  stop                                 Stop current runtime\n"
            << "\n"
            << "State interaction:\n"
            << "  input <var> <x> <y> <value>          Queue scalar input patch\n"
            << "  event <x> <y> <signal>               Queue event signal patch\n"
            << "  summary <variable>                   Show min/max/avg for one field\n"
            << "  fields                               List variables in current snapshot\n"
            << "  sample <variable> <x> <y>            Sample one cell value\n"
            << "  heatmap <variable> [w h]             Render ASCII heatmap\n"
            << "  trace [count]                        Show latest trace records\n"
            << "  metrics                              Show observability counters\n"
            << "\n"
            << "Checkpoint persistence:\n"
            << "  checkpoint <label>                   Save in-memory checkpoint\n"
            << "  restore <label>                      Restore checkpoint\n"
            << "  listcp                               List in-memory checkpoints\n"
            << "  savecp <label> <path>                Save checkpoint to disk\n"
            << "  loadcp <label> <path>                Load checkpoint from disk\n"
            << "\n"
            << "Profiles and automation:\n"
            << "  config                               Show launch configuration\n"
            << "  set seed <u64>                       Configure seed (requires restart)\n"
            << "  set grid <width> <height>            Configure grid (requires restart)\n"
            << "  set tier <A|B|C>                     Configure model tier (requires restart)\n"
            << "  set temporal <uniform|phased|multirate>  Configure temporal policy\n"
            << "  set init <terrain|conway|gray_scott|waves|blank>  Configure initialization mode\n"
            << "  preset                               List available built-in presets\n"
            << "  preset <name>                        Apply built-in preset (requires restart)\n"
            << "  model                                Show current model scope and catalog\n"
            << "  model list                           List available models in the workspace\n"
            << "  model current                        Show the active model key\n"
            << "  model select <id|path>               Select active model scope\n"
            << "  world list [--model <id|path>]       List worlds for a model scope\n"
            << "  world open --model <id|path> <name>  Open a world for a model scope\n"
            << "  profile list                         List saved runtime profiles\n"
            << "  profile save <name>                  Persist current launch configuration\n"
            << "  profile load <name>                  Load saved launch configuration\n"
            << "  runscript <path>                     Execute commands from script file\n"
            << "  history [count]                      Show recent command history\n"
            << "  savehistory <path>                   Save command history to disk\n"
            << "\n"
            << "Exit:\n"
            << "  exit | quit                          Exit application\n";
    }

    bool handleCommand(const std::string& rawLine) {
        std::istringstream input(rawLine);
        std::string command;
        input >> command;
        if (command.empty()) {
            return true;
        }

        commandHistory_.push_back(rawLine);
        if (commandHistory_.size() > 5000) {
            commandHistory_.erase(commandHistory_.begin(), commandHistory_.begin() + 1000);
        }

        command = toLower(command);
        try {
            if (command == "help") {
                printHelp();
                return true;
            }
            if (command == "dashboard") {
                printDashboard();
                return true;
            }
            if (command == "status") {
                printStatus();
                return true;
            }
            if (command == "step" || command == "run") {
                executeStepCommand(input);
                return true;
            }
            if (command == "rununtil") {
                executeRunUntilCommand(input);
                return true;
            }
            if (command == "bench") {
                executeBenchmarkCommand(input);
                return true;
            }
            if (command == "runscript") {
                return executeScriptCommand(input);
            }
            if (command == "pause") {
                requireRuntime("pause");
                runtime_->pause();
                std::cout << "runtime paused\n";
                return true;
            }
            if (command == "resume") {
                requireRuntime("resume");
                runtime_->resume();
                std::cout << "runtime resumed\n";
                return true;
            }
            if (command == "checkpoint") {
                executeCheckpointCommand(input);
                return true;
            }
            if (command == "restore") {
                executeRestoreCommand(input);
                return true;
            }
            if (command == "listcp") {
                executeListCheckpointCommand();
                return true;
            }
            if (command == "savecp") {
                executeSaveCheckpointCommand(input);
                return true;
            }
            if (command == "loadcp") {
                executeLoadCheckpointCommand(input);
                return true;
            }
            if (command == "input") {
                executeInputCommand(input);
                return true;
            }
            if (command == "event") {
                executeEventCommand(input);
                return true;
            }
            if (command == "trace") {
                executeTraceCommand(input);
                return true;
            }
            if (command == "summary") {
                executeSummaryCommand(input);
                return true;
            }
            if (command == "model") {
                executeModelCommand(input);
                return true;
            }
            if (command == "world") {
                executeWorldCommand(input);
                return true;
            }
            if (command == "fields") {
                executeFieldsCommand();
                return true;
            }
            if (command == "sample") {
                executeSampleCommand(input);
                return true;
            }
            if (command == "heatmap") {
                executeHeatmapCommand(input);
                return true;
            }
            if (command == "metrics") {
                printMetrics();
                return true;
            }
            if (command == "config") {
                printConfig();
                return true;
            }
            if (command == "preset") {
                executePresetCommand(input);
                return true;
            }
            if (command == "profile") {
                executeProfileCommand(input);
                return true;
            }
            if (command == "set") {
                executeSetCommand(input);
                return true;
            }
            if (command == "history") {
                executeHistoryCommand(input);
                return true;
            }
            if (command == "savehistory") {
                executeSaveHistoryCommand(input);
                return true;
            }
            if (command == "restart") {
                startSession();
                return true;
            }
            if (command == "stop") {
                stopRuntime();
                return true;
            }
            if (command == "exit" || command == "quit") {
                std::cout << "Goodbye.\n";
                return false;
            }

            std::cout << "unknown command: " << command << " (type 'help')\n";
            return true;
        } catch (const std::exception& exception) {
            std::cout << "command_error=" << exception.what() << "\n";
            return true;
        }
    }

    bool executeScriptCommand(std::istringstream& input) {
        std::string pathToken;
        std::getline(input, pathToken);
        const std::string scriptPath = trim(pathToken);
        if (scriptPath.empty()) {
            throw std::invalid_argument("usage: runscript <path>");
        }

        std::ifstream script(scriptPath);
        if (!script.is_open()) {
            throw std::runtime_error("failed to open script file: " + scriptPath);
        }

        std::string line;
        std::size_t lineNumber = 0;
        while (std::getline(script, line)) {
            lineNumber += 1;
            const std::string trimmed = trim(line);
            if (trimmed.empty() || trimmed.starts_with('#')) {
                continue;
            }

            std::cout << "script> " << trimmed << "\n";
            const bool keepRunning = handleCommand(trimmed);
            if (!keepRunning) {
                std::cout << "script terminated session at line=" << lineNumber << "\n";
                return false;
            }
        }

        std::cout << "script_complete path=" << scriptPath << "\n";
        return true;
    }

    void executeStepCommand(std::istringstream& input) {
        requireRuntime("step");

        std::uint32_t count = 1;
        std::string token;
        if (input >> token) {
            const auto parsed = parseU32(token);
            if (!parsed.has_value() || *parsed == 0) {
                throw std::invalid_argument("step count must be a positive integer");
            }
            count = *parsed;
        }

        runtime_->controlledStep(count);
        const auto& snapshot = runtime_->snapshot();
        const auto& diagnostics = runtime_->lastStepDiagnostics();

        std::cout << "stepped=" << count
                  << " step_index=" << snapshot.stateHeader.stepIndex
                  << " state_hash=" << snapshot.stateHash
                  << " events_applied=" << diagnostics.eventsApplied
                  << " reproducibility=" << toString(snapshot.reproducibilityClass)
                  << "\n";
    }

    void executeRunUntilCommand(std::istringstream& input) {
        requireRuntime("rununtil");

        std::string targetToken;
        input >> targetToken;
        const auto targetStep = parseU64(targetToken);
        if (!targetStep.has_value()) {
            throw std::invalid_argument("usage: rununtil <step_index>");
        }

        const auto current = runtime_->snapshot().stateHeader.stepIndex;
        if (*targetStep <= current) {
            std::cout << "rununtil_noop current_step=" << current << " target_step=" << *targetStep << "\n";
            return;
        }

        std::uint64_t remaining = *targetStep - current;
        while (remaining > 0) {
            const std::uint32_t chunk = static_cast<std::uint32_t>(std::min<std::uint64_t>(remaining, 10000));
            runtime_->controlledStep(chunk);
            remaining -= chunk;
        }

        const auto& snapshot = runtime_->snapshot();
        std::cout << "rununtil_complete step_index=" << snapshot.stateHeader.stepIndex
                  << " state_hash=" << snapshot.stateHash << "\n";
    }

    void executeBenchmarkCommand(std::istringstream& input) {
        requireRuntime("bench");

        std::string token;
        input >> token;
        const auto steps = parseU32(token);
        if (!steps.has_value() || *steps == 0) {
            throw std::invalid_argument("usage: bench <positive_steps>");
        }

        const auto start = std::chrono::steady_clock::now();
        runtime_->controlledStep(*steps);
        const auto end = std::chrono::steady_clock::now();

        const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);
        const double seconds = static_cast<double>(elapsed.count()) / 1000.0;
        const double throughput = (seconds <= 0.0)
            ? 0.0
            : (static_cast<double>(*steps) / seconds);

        std::cout << std::fixed << std::setprecision(3)
                  << "bench steps=" << *steps
                  << " elapsed_ms=" << elapsed.count()
                  << " throughput_steps_per_sec=" << throughput
                  << " final_step=" << runtime_->snapshot().stateHeader.stepIndex
                  << "\n";
    }

    void executeCheckpointCommand(std::istringstream& input) {
        requireRuntime("checkpoint");

        std::string label;
        input >> label;
        if (label.empty()) {
            throw std::invalid_argument("checkpoint label is required");
        }

        checkpoints_[label] = runtime_->createCheckpoint(label);
        std::cout << "checkpoint_saved=" << label
                  << " step_index=" << runtime_->snapshot().stateHeader.stepIndex
                  << "\n";
    }

    void executeRestoreCommand(std::istringstream& input) {
        requireRuntime("restore");

        std::string label;
        input >> label;
        if (label.empty()) {
            throw std::invalid_argument("restore label is required");
        }

        const auto it = checkpoints_.find(label);
        if (it == checkpoints_.end()) {
            throw std::invalid_argument("unknown checkpoint label: " + label);
        }

        runtime_->resetToCheckpoint(it->second);
        std::cout << "checkpoint_restored=" << label
                  << " step_index=" << runtime_->snapshot().stateHeader.stepIndex
                  << "\n";
    }

    void executeListCheckpointCommand() const {
        if (checkpoints_.empty()) {
            std::cout << "checkpoints=empty\n";
            return;
        }

        for (const auto& [label, checkpoint] : checkpoints_) {
            std::cout << "checkpoint label=" << label
                      << " step_index=" << checkpoint.stateSnapshot.header.stepIndex
                      << " state_hash=" << checkpoint.stateSnapshot.stateHash
                      << " payload_bytes=" << checkpoint.stateSnapshot.payloadBytes
                      << "\n";
        }
    }

    void executeSaveCheckpointCommand(std::istringstream& input) const {
        std::string label;
        std::string pathToken;
        input >> label >> pathToken;
        if (label.empty() || pathToken.empty()) {
            throw std::invalid_argument("usage: savecp <label> <path>");
        }

        const auto it = checkpoints_.find(label);
        if (it == checkpoints_.end()) {
            throw std::invalid_argument("unknown checkpoint label: " + label);
        }

        const std::filesystem::path path(pathToken);
        writeCheckpointFile(it->second, path);
        std::cout << "checkpoint_written label=" << label << " path=" << path.string() << "\n";
    }

    void executeLoadCheckpointCommand(std::istringstream& input) {
        std::string label;
        std::string pathToken;
        input >> label >> pathToken;
        if (label.empty() || pathToken.empty()) {
            throw std::invalid_argument("usage: loadcp <label> <path>");
        }

        const std::filesystem::path path(pathToken);
        auto checkpoint = readCheckpointFile(path);
        checkpoints_[label] = std::move(checkpoint);
        std::cout << "checkpoint_loaded label=" << label << " path=" << path.string() << "\n";
    }

    void executeInputCommand(std::istringstream& input) {
        requireRuntime("input");

        std::string variable;
        std::string xToken;
        std::string yToken;
        std::string valueToken;
        input >> variable >> xToken >> yToken >> valueToken;

        if (variable.empty() || xToken.empty() || yToken.empty() || valueToken.empty()) {
            throw std::invalid_argument("usage: input <var> <x> <y> <value>");
        }

        const auto x = parseU32(xToken);
        const auto y = parseU32(yToken);
        const auto value = parseFloat(valueToken);

        if (!x.has_value() || !y.has_value() || !value.has_value()) {
            throw std::invalid_argument("input arguments must be numeric");
        }

        RuntimeInputFrame frame;
        frame.scalarPatches.push_back(ScalarWritePatch{variable, Cell{*x, *y}, *value});
        runtime_->queueInput(std::move(frame));
        std::cout << "input_queued variable=" << variable << " cell=(" << *x << ',' << *y << ")\n";
    }

    void executeEventCommand(std::istringstream& input) {
        requireRuntime("event");

        std::string xToken;
        std::string yToken;
        std::string signalToken;
        input >> xToken >> yToken >> signalToken;

        if (xToken.empty() || yToken.empty() || signalToken.empty()) {
            throw std::invalid_argument("usage: event <x> <y> <signal>");
        }

        const auto x = parseU32(xToken);
        const auto y = parseU32(yToken);
        const auto signal = parseFloat(signalToken);

        if (!x.has_value() || !y.has_value() || !signal.has_value()) {
            throw std::invalid_argument("event arguments must be numeric");
        }

        RuntimeEvent event;
        event.eventName = "manual_event_signal";
        event.scalarPatches.push_back(ScalarWritePatch{"event_signal_e", Cell{*x, *y}, *signal});
        runtime_->enqueueEvent(std::move(event));

        std::cout << "event_queued cell=(" << *x << ',' << *y << ") signal=" << *signal << "\n";
    }

    void executeTraceCommand(std::istringstream& input) const {
        requireRuntime("trace");

        std::size_t count = 8;
        std::string token;
        if (input >> token) {
            const auto parsed = parseU64(token);
            if (!parsed.has_value()) {
                throw std::invalid_argument("trace count must be a positive integer");
            }
            count = static_cast<std::size_t>(*parsed);
        }

        const auto& records = runtime_->traceRecords();
        if (records.empty()) {
            std::cout << "trace is empty\n";
            return;
        }

        const std::size_t first = (count >= records.size()) ? 0 : (records.size() - count);
        for (std::size_t i = first; i < records.size(); ++i) {
            const auto& record = records[i];
            std::cout << "trace seq=" << record.sequence
                      << " step=" << record.stepIndex
                      << " name=" << record.name
                      << " detail=" << record.detail
                      << " payload=" << record.payloadFingerprint
                      << "\n";
        }
    }

    void executeSummaryCommand(std::istringstream& input) const {
        requireRuntime("summary");

        std::string variableName;
        input >> variableName;
        if (variableName.empty()) {
            throw std::invalid_argument("usage: summary <variable>");
        }

        const auto probeCheckpoint = runtime_->createCheckpoint("summary_probe");
        const auto& fields = probeCheckpoint.stateSnapshot.fields;
        const auto it = std::find_if(fields.begin(), fields.end(), [&](const auto& field) {
            return field.spec.name == variableName;
        });

        if (it == fields.end()) {
            throw std::invalid_argument("unknown variable in snapshot: " + variableName);
        }

        const auto summary = summarizeField(*it);
        std::cout << std::fixed << std::setprecision(6)
                  << "summary variable=" << variableName
                  << " valid_count=" << summary.validCount
                  << " invalid_count=" << summary.invalidCount
                  << " min=" << summary.minValue
                  << " max=" << summary.maxValue
                  << " avg=" << summary.average
                  << "\n";
    }

    void executeFieldsCommand() const {
        requireRuntime("fields");

        const auto probeCheckpoint = runtime_->createCheckpoint("fields_probe");
        std::cout << "fields count=" << probeCheckpoint.stateSnapshot.fields.size() << "\n";
        for (const auto& field : probeCheckpoint.stateSnapshot.fields) {
            std::cout << "  - " << field.spec.name << "\n";
        }
    }

    void executeModelCommand(std::istringstream& input) {
        std::string action;
        input >> action;
        action = toLower(action);

        if (action.empty() || action == "list") {
            const auto models = listAvailableModels();
            std::cout << "model_current key=" << currentModelKey() << "\n";
            if (models.empty()) {
                std::cout << "models=empty\n";
                return;
            }

            std::cout << "models:\n";
            for (const auto& model : models) {
                std::cout << "  - " << model.key
                          << " path=" << model.path.string()
                          << " kind=" << (model.isDirectory ? "directory" : "archive")
                          << "\n";
            }
            return;
        }

        if (action == "current") {
            std::cout << "model_current key=" << currentModelKey() << "\n";
            return;
        }

        if (action == "select") {
            std::string token;
            input >> token;
            if (token.empty()) {
                throw std::invalid_argument("usage: model select <id|path>");
            }

            const std::string modelKey = normalizeModelKey(token);
            if (modelKey.empty()) {
                throw std::invalid_argument("model selection resolved to an empty key");
            }

            selectedModelKey_ = modelKey;
            std::cout << "model_selected key=" << selectedModelKey_ << "\n";
            return;
        }

        throw std::invalid_argument("usage: model <list|current|select> [id|path]");
    }

    void executeWorldCommand(std::istringstream& input) {
        std::string action;
        input >> action;
        action = toLower(action);

        if (action.empty() || action == "list") {
            const auto [modelKey, worldName] = parseWorldCommandScope(input);
            if (!worldName.empty()) {
                throw std::invalid_argument("usage: world list [--model <id|path>]");
            }

            std::string message;
            const auto records = worldStore_.list(modelKey, message);
            std::cout << "worlds model_key=" << modelKey << " count=" << records.size() << "\n";
            if (!message.empty()) {
                std::cout << "worlds_message " << message << "\n";
            }
            for (const auto& record : records) {
                std::cout << "  - " << record.worldName
                          << " profile=" << (record.hasProfile ? "yes" : "no")
                          << " checkpoint=" << (record.hasCheckpoint ? "yes" : "no")
                          << " seed=" << record.seed
                          << " step=" << record.stepIndex
                          << " temporal=" << record.temporalPolicy
                          << " init=" << record.initialConditionMode
                          << "\n";
            }
            return;
        }

        if (action == "open") {
            const auto [modelKey, worldName] = parseWorldCommandScope(input);
            if (worldName.empty()) {
                throw std::invalid_argument("usage: world open --model <id|path> <name>");
            }

            std::string message;
            try {
                launchConfig_ = worldProfileStore_.load(worldName, modelKey);
            } catch (const std::exception& exception) {
                throw std::runtime_error(std::string("world_open_failed error=") + exception.what());
            }

            selectedModelKey_ = modelKey;
            startSession();

            const auto checkpointPath = worldStore_.checkpointPathFor(worldName, modelKey);
            const bool hasCheckpoint = std::filesystem::exists(checkpointPath);
            if (hasCheckpoint) {
                try {
                    const auto checkpoint = app::readCheckpointFile(checkpointPath);
                    runtime_->resetToCheckpoint(checkpoint);
                    checkpoints_.clear();
                    std::cout << "world_opened name=" << worldName
                              << " model_key=" << modelKey
                              << " source=checkpoint\n";
                    return;
                } catch (const std::exception& exception) {
                    throw std::runtime_error(std::string("world_open_failed checkpoint_restore_error=") + exception.what());
                }
            }

            std::cout << "world_opened name=" << worldName
                      << " model_key=" << modelKey
                      << " source=profile\n";
            return;
        }

        throw std::invalid_argument("usage: world <list|open>");
    }

    void executeSampleCommand(std::istringstream& input) const {
        requireRuntime("sample");

        std::string variableName;
        std::string xToken;
        std::string yToken;
        input >> variableName >> xToken >> yToken;
        if (variableName.empty() || xToken.empty() || yToken.empty()) {
            throw std::invalid_argument("usage: sample <variable> <x> <y>");
        }

        const auto x = parseU32(xToken);
        const auto y = parseU32(yToken);
        if (!x.has_value() || !y.has_value()) {
            throw std::invalid_argument("sample coordinates must be unsigned integers");
        }

        const auto probeCheckpoint = runtime_->createCheckpoint("sample_probe");
        const auto& snapshot = probeCheckpoint.stateSnapshot;
        if (*x >= snapshot.grid.width || *y >= snapshot.grid.height) {
            throw std::invalid_argument("sample coordinates out of current grid bounds");
        }

        const auto it = std::find_if(snapshot.fields.begin(), snapshot.fields.end(), [&](const auto& field) {
            return field.spec.name == variableName;
        });
        if (it == snapshot.fields.end()) {
            throw std::invalid_argument("unknown variable in snapshot: " + variableName);
        }

        const std::size_t index = static_cast<std::size_t>(*y) * snapshot.grid.width + static_cast<std::size_t>(*x);
        if (index >= it->values.size() || index >= it->validityMask.size()) {
            throw std::runtime_error("snapshot field payload has inconsistent dimensions");
        }

        if (it->validityMask[index] == 0u) {
            std::cout << "sample variable=" << variableName << " cell=(" << *x << ',' << *y << ") value=<invalid>\n";
            return;
        }

        std::cout << std::fixed << std::setprecision(6)
                  << "sample variable=" << variableName
                  << " cell=(" << *x << ',' << *y << ')'
                  << " value=" << it->values[index]
                  << "\n";
    }

    void executeHeatmapCommand(std::istringstream& input) const {
        requireRuntime("heatmap");

        std::string variableName;
        std::string widthToken;
        std::string heightToken;
        input >> variableName >> widthToken >> heightToken;
        if (variableName.empty()) {
            throw std::invalid_argument("usage: heatmap <variable> [w h]");
        }

        std::uint32_t viewWidth = 64;
        std::uint32_t viewHeight = 24;
        if (!widthToken.empty()) {
            const auto width = parseU32(widthToken);
            if (!width.has_value() || *width == 0) {
                throw std::invalid_argument("heatmap width must be a positive integer");
            }
            viewWidth = *width;
        }
        if (!heightToken.empty()) {
            const auto height = parseU32(heightToken);
            if (!height.has_value() || *height == 0) {
                throw std::invalid_argument("heatmap height must be a positive integer");
            }
            viewHeight = *height;
        }

        const auto probeCheckpoint = runtime_->createCheckpoint("heatmap_probe");
        const auto& snapshot = probeCheckpoint.stateSnapshot;

        const auto it = std::find_if(snapshot.fields.begin(), snapshot.fields.end(), [&](const auto& field) {
            return field.spec.name == variableName;
        });
        if (it == snapshot.fields.end()) {
            throw std::invalid_argument("unknown variable in snapshot: " + variableName);
        }

        const auto summary = summarizeField(*it);
        const std::uint32_t sampleWidth = std::min<std::uint32_t>(viewWidth, snapshot.grid.width);
        const std::uint32_t sampleHeight = std::min<std::uint32_t>(viewHeight, snapshot.grid.height);

        std::cout << "heatmap variable=" << variableName
                  << " grid=" << snapshot.grid.width << 'x' << snapshot.grid.height
                  << " view=" << sampleWidth << 'x' << sampleHeight
                  << " min=" << summary.minValue
                  << " max=" << summary.maxValue
                  << "\n";

        for (std::uint32_t y = 0; y < sampleHeight; ++y) {
            std::string row;
            row.reserve(sampleWidth);
            for (std::uint32_t x = 0; x < sampleWidth; ++x) {
                const std::uint32_t sourceX = (sampleWidth <= 1)
                    ? 0
                    : static_cast<std::uint32_t>((static_cast<double>(x) / static_cast<double>(sampleWidth - 1)) * static_cast<double>(snapshot.grid.width - 1));
                const std::uint32_t sourceY = (sampleHeight <= 1)
                    ? 0
                    : static_cast<std::uint32_t>((static_cast<double>(y) / static_cast<double>(sampleHeight - 1)) * static_cast<double>(snapshot.grid.height - 1));

                const std::size_t index = static_cast<std::size_t>(sourceY) * snapshot.grid.width + static_cast<std::size_t>(sourceX);
                if (index >= it->values.size() || index >= it->validityMask.size() || it->validityMask[index] == 0u) {
                    row.push_back(' ');
                    continue;
                }

                row.push_back(heatmapGlyph(it->values[index], summary.minValue, summary.maxValue));
            }
            std::cout << row << "\n";
        }
    }

    void executePresetCommand(std::istringstream& input) {
        std::string name;
        input >> name;

        if (name.empty()) {
            std::cout << "presets:\n";
            for (const auto& preset : allPresets()) {
                std::cout << "  - " << preset.name
                          << " (seed=" << preset.config.seed
                          << ", grid=" << preset.config.grid.width << 'x' << preset.config.grid.height
                          << ", tier=" << toString(preset.config.tier)
                          << ", temporal=" << temporalPolicyToString(preset.config.temporalPolicy)
                          << ", init=" << initialConditionTypeToString(preset.config.initialConditions.type)
                          << ") : " << preset.description
                          << "\n";
            }
            return;
        }

        const auto preset = presetByName(name);
        if (!preset.has_value()) {
            throw std::invalid_argument("unknown preset: " + name);
        }

        launchConfig_ = preset->config;
        std::cout << "preset_applied name=" << preset->name
                  << " seed=" << launchConfig_.seed
                  << " grid=" << launchConfig_.grid.width << 'x' << launchConfig_.grid.height
                  << " tier=" << toString(launchConfig_.tier)
                  << " temporal=" << temporalPolicyToString(launchConfig_.temporalPolicy)
                  << " init=" << initialConditionTypeToString(launchConfig_.initialConditions.type)
                  << " (use restart)\n";
    }

    void executeProfileCommand(std::istringstream& input) {
        std::string action;
        input >> action;
        action = toLower(action);

        if (action.empty() || action == "list") {
            const auto names = profileStore_.list();
            if (names.empty()) {
                std::cout << "profiles=empty\n";
                return;
            }
            std::cout << "profiles:\n";
            for (const auto& name : names) {
                std::cout << "  - " << name << "\n";
            }
            return;
        }

        if (action == "save") {
            std::string name;
            input >> name;
            if (name.empty()) {
                throw std::invalid_argument("usage: profile save <name>");
            }
            profileStore_.save(name, launchConfig_);
            std::cout << "profile_saved name=" << name << " path=" << profileStore_.pathFor(name).string() << "\n";
            return;
        }

        if (action == "load") {
            std::string name;
            input >> name;
            if (name.empty()) {
                throw std::invalid_argument("usage: profile load <name>");
            }
            launchConfig_ = profileStore_.load(name);
            std::cout << "profile_loaded name=" << name
                      << " seed=" << launchConfig_.seed
                      << " grid=" << launchConfig_.grid.width << 'x' << launchConfig_.grid.height
                      << " tier=" << toString(launchConfig_.tier)
                      << " temporal=" << temporalPolicyToString(launchConfig_.temporalPolicy)
                      << " init=" << initialConditionTypeToString(launchConfig_.initialConditions.type)
                      << " (use restart)\n";
            return;
        }

        throw std::invalid_argument("usage: profile <list|save|load> [name]");
    }

    void executeSetCommand(std::istringstream& input) {
        std::string field;
        input >> field;
        field = toLower(field);

        if (field.empty()) {
            throw std::invalid_argument("usage: set <seed|grid|tier|temporal|init> ...");
        }

        if (field == "seed") {
            std::string token;
            input >> token;
            const auto seed = parseU64(token);
            if (!seed.has_value()) {
                throw std::invalid_argument("set seed requires unsigned integer value");
            }
            launchConfig_.seed = *seed;
            std::cout << "seed updated to " << launchConfig_.seed << " (use restart)\n";
            return;
        }

        if (field == "grid") {
            std::string widthToken;
            std::string heightToken;
            input >> widthToken >> heightToken;

            const auto width = parseU32(widthToken);
            const auto height = parseU32(heightToken);
            if (!width.has_value() || !height.has_value() || *width == 0 || *height == 0) {
                throw std::invalid_argument("set grid requires positive width and height");
            }
            launchConfig_.grid = GridSpec{*width, *height};
            std::cout << "grid updated to " << widthToken << "x" << heightToken << " (use restart)\n";
            return;
        }

        if (field == "tier") {
            std::string token;
            input >> token;
            const auto tier = parseTier(token);
            if (!tier.has_value()) {
                throw std::invalid_argument("set tier requires A, B, or C");
            }
            launchConfig_.tier = *tier;
            std::cout << "tier updated to " << toString(launchConfig_.tier) << " (use restart)\n";
            return;
        }

        if (field == "temporal") {
            std::string token;
            input >> token;
            const auto policy = parseTemporalPolicy(token);
            if (!policy.has_value()) {
                throw std::invalid_argument("set temporal requires uniform, phased, or multirate");
            }
            launchConfig_.temporalPolicy = *policy;
            std::cout << "temporal policy updated to " << temporalPolicyToString(launchConfig_.temporalPolicy)
                      << " (use restart)\n";
            return;
        }

        if (field == "init") {
            std::string token;
            input >> token;
            const auto initType = parseInitialConditionType(token);
            if (!initType.has_value()) {
                throw std::invalid_argument("set init requires terrain, conway, gray_scott, waves, or blank");
            }
            launchConfig_.initialConditions.type = *initType;
            std::cout << "initial condition mode updated to "
                      << initialConditionTypeToString(launchConfig_.initialConditions.type)
                      << " (use restart)\n";
            return;
        }

        throw std::invalid_argument("unknown set field: " + field);
    }

    void executeHistoryCommand(std::istringstream& input) const {
        std::size_t count = 20;
        std::string token;
        if (input >> token) {
            const auto parsed = parseU64(token);
            if (!parsed.has_value() || *parsed == 0) {
                throw std::invalid_argument("history count must be a positive integer");
            }
            count = static_cast<std::size_t>(*parsed);
        }

        if (commandHistory_.empty()) {
            std::cout << "history=empty\n";
            return;
        }

        const std::size_t first = (count >= commandHistory_.size()) ? 0 : (commandHistory_.size() - count);
        for (std::size_t i = first; i < commandHistory_.size(); ++i) {
            std::cout << "history[" << i << "] " << commandHistory_[i] << "\n";
        }
    }

    void executeSaveHistoryCommand(std::istringstream& input) const {
        std::string pathToken;
        std::getline(input, pathToken);
        const auto pathValue = trim(pathToken);
        if (pathValue.empty()) {
            throw std::invalid_argument("usage: savehistory <path>");
        }

        const std::filesystem::path path(pathValue);
        std::error_code ec;
        const auto parent = path.parent_path();
        if (!parent.empty()) {
            std::filesystem::create_directories(parent, ec);
            if (ec) {
                throw std::runtime_error("failed to create history directory: " + parent.string());
            }
        }

        std::ofstream output(path, std::ios::trunc);
        if (!output.is_open()) {
            throw std::runtime_error("failed to open history output file: " + path.string());
        }

        output << "# world-sim command history\n";
        output << "# entries=" << commandHistory_.size() << "\n";
        for (const auto& entry : commandHistory_) {
            output << entry << '\n';
        }

        std::cout << "history_written path=" << path.string() << " entries=" << commandHistory_.size() << "\n";
    }

    void printStatus() const {
        requireRuntime("status");
        const auto& snapshot = runtime_->snapshot();
        const auto& diagnostics = runtime_->lastStepDiagnostics();

        std::cout << "status=running"
                  << " paused=" << (runtime_->paused() ? "yes" : "no")
                  << " step_index=" << snapshot.stateHeader.stepIndex
                  << " state_hash=" << snapshot.stateHash
                  << " run_identity_hash=" << snapshot.runSignature.identityHash()
                  << " reproducibility=" << toString(snapshot.reproducibilityClass)
                  << "\n";

        std::cout << "diag events_applied=" << diagnostics.eventsApplied
                  << " input_patches=" << diagnostics.inputPatchesApplied
                  << " event_patches=" << diagnostics.eventPatchesApplied
                  << " constraints=" << diagnostics.constraintViolations.size()
                  << " stability_alerts=" << diagnostics.stabilityAlerts.size()
                  << "\n";
    }

    void printDashboard() const {
        requireRuntime("dashboard");
        const auto& snapshot = runtime_->snapshot();
        const auto& diagnostics = runtime_->lastStepDiagnostics();

        std::cout << "================ WORLD SIM DASHBOARD ================\n";
        std::cout << "run_identity_hash : " << snapshot.runSignature.identityHash() << "\n";
        std::cout << "step_index        : " << snapshot.stateHeader.stepIndex << "\n";
        std::cout << "state_hash        : " << snapshot.stateHash << "\n";
        std::cout << "model_key         : " << currentModelKey() << "\n";
        std::cout << "reproducibility   : " << toString(snapshot.reproducibilityClass) << "\n";
        std::cout << "grid              : " << launchConfig_.grid.width << 'x' << launchConfig_.grid.height << "\n";
        std::cout << "tier / temporal   : " << toString(launchConfig_.tier)
                  << " / " << temporalPolicyToString(launchConfig_.temporalPolicy) << "\n";
        std::cout << "seed              : " << launchConfig_.seed << "\n";
        std::cout << "checkpoints(mem)  : " << checkpoints_.size() << "\n";
        std::cout << "constraints       : " << diagnostics.constraintViolations.size() << "\n";
        std::cout << "stability_alerts  : " << diagnostics.stabilityAlerts.size() << "\n";
        std::cout << "=====================================================\n";
    }

    void printMetrics() const {
        requireRuntime("metrics");
        const auto metrics = runtime_->metrics();
        std::cout << "metrics"
                  << " steps_executed=" << metrics.stepsExecuted
                  << " events_applied=" << metrics.eventsApplied
                  << " events_queued=" << metrics.eventsQueued
                  << " input_patches=" << metrics.inputPatches
                  << " checkpoints_created=" << metrics.checkpointsCreated
                  << " checkpoints_loaded=" << metrics.checkpointsLoaded
                  << "\n";
    }

    void printConfig() const {
        std::cout << "config"
                  << " model_key=" << currentModelKey()
                  << " seed=" << launchConfig_.seed
                  << " grid=" << launchConfig_.grid.width << 'x' << launchConfig_.grid.height
                  << " tier=" << toString(launchConfig_.tier)
                  << " temporal=" << temporalPolicyToString(launchConfig_.temporalPolicy)
                  << " init=" << initialConditionTypeToString(launchConfig_.initialConditions.type)
                  << "\n";
    }

    void requireRuntime(const std::string_view commandName) const {
        if (!runtime_ || runtime_->status() != RuntimeStatus::Running) {
            throw std::runtime_error("runtime is not running; command unavailable: " + std::string(commandName));
        }
    }

    void startSession() {
        stopRuntime();
        checkpoints_.clear();

        auto runtime = std::make_unique<Runtime>(makeRuntimeConfig(launchConfig_));
        for (const auto& subsystem : makePhase4Subsystems()) {
            runtime->registerSubsystem(subsystem);
        }
        runtime->start();
        runtime_ = std::move(runtime);

        const auto& snapshot = runtime_->snapshot();
        std::cout << "session_started"
                  << " run_identity_hash=" << snapshot.runSignature.identityHash()
                  << " model_key=" << currentModelKey()
                  << " grid=" << launchConfig_.grid.width << 'x' << launchConfig_.grid.height
                  << " tier=" << toString(launchConfig_.tier)
                  << " temporal=" << temporalPolicyToString(launchConfig_.temporalPolicy)
                  << "\n";
    }

    std::pair<std::string, std::string> parseWorldCommandScope(std::istringstream& input) const {
        std::string modelKey = currentModelKey();
        std::string worldName;

        std::vector<std::string> tokens;
        std::string token;
        while (input >> token) {
            tokens.push_back(token);
        }

        for (std::size_t i = 0; i < tokens.size(); ++i) {
            const std::string& current = tokens[i];
            if (current == "--model" && (i + 1u) < tokens.size()) {
                modelKey = normalizeModelKey(tokens[++i]);
                continue;
            }
            if (current.rfind("--model=", 0) == 0) {
                modelKey = normalizeModelKey(current.substr(8));
                continue;
            }
            if (worldName.empty()) {
                worldName = current;
            } else {
                throw std::invalid_argument("unexpected argument: " + current);
            }
        }

        if (modelKey.empty()) {
            modelKey = "legacy";
        }

        return {modelKey, worldName};
    }

    std::string currentModelKey() const {
        return selectedModelKey_.empty() ? std::string{"legacy"} : selectedModelKey_;
    }

    void stopRuntime() {
        if (runtime_ && runtime_->status() == RuntimeStatus::Running) {
            runtime_->stop();
            std::cout << "runtime_stopped step_index=" << runtime_->snapshot().stateHeader.stepIndex << "\n";
        }
    }

    void shutdown() {
        try {
            stopRuntime();
        } catch (...) {
        }
    }

    LaunchConfig launchConfig_{};
    std::string selectedModelKey_{"legacy"};
    std::unique_ptr<Runtime> runtime_;
    std::map<std::string, RuntimeCheckpoint, std::less<>> checkpoints_;
    std::vector<std::string> commandHistory_;
    ProfileStore profileStore_{};
    ProfileStore worldProfileStore_{std::filesystem::path("checkpoints") / "world_profiles"};
    WorldStore worldStore_{};
};

} // namespace

int RuntimeShell::run() {
    WorldSimApp app;
    return app.run();
}

} // namespace ws::app
