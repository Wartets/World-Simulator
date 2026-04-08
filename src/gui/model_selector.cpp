#include "ws/gui/model_selector.hpp"
#include "ws/core/model_parser.hpp"
#include "ws/gui/data_operation_contract.hpp"
#include "ws/gui/ui_components.hpp"

#include <imgui.h>
#include <algorithm>
#include <cctype>
#include <chrono>
#include <array>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <iomanip>
#include <fstream>
#include <sstream>
#include <optional>
#include <nlohmann/json.hpp>
#include <utility>

namespace fs = std::filesystem;
using json = nlohmann::json;

namespace ws::gui {

namespace {

std::string gDataOperationReceipt{};
std::string gDataOperationRecoveryHint{};
std::string gDataOperationTechnicalDetail{};

void recordDataOperationReceipt(
    const std::string& operation,
    const DataOperationMode mode,
    const std::string& summary,
    const std::string& recoveryHint,
    const std::string& technicalDetail = {}) {
    gDataOperationReceipt = dataOperationReceipt(operation, mode, summary, recoveryHint);
    gDataOperationRecoveryHint = recoveryHint;
    gDataOperationTechnicalDetail = technicalDetail;
}

// Current engine version for compatibility checking.
constexpr const char* kCurrentEngineVersion = "1.0.0";

// Table column identifiers for model browser table.
enum TableColumnId {
    ColName = 0,
    ColLaunchStatus,
    ColId,
    ColVersion,
    ColFormatVersion,
    ColMinimumEngineVersion,
    ColAuthor,
    ColCreationDate,
    ColTags,
    ColDescription,
    ColCompatibility,
    ColIdentityHash,
    ColLastModified
};

// Creates lowercase copy of string for case-insensitive comparison.
// @param value Input string
// @return Lowercase version of input
std::string toLowerCopy(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return value;
}

// Resolves workspace root by searching for CMakeLists.txt upward.
// @return Path to workspace root directory
fs::path resolveWorkspaceRoot() {
    std::error_code ec;
    fs::path current = fs::current_path(ec);
    if (ec) {
        return fs::path{"."};
    }

    for (fs::path probe = current; !probe.empty(); probe = probe.parent_path()) {
        if (fs::exists(probe / "CMakeLists.txt")) {
            return probe;
        }
        if (probe == probe.parent_path()) {
            break;
        }
    }

    return current;
}

// Gets the models directory path, creating it if necessary.
// @return Path to models root directory
fs::path modelsRoot() {
    const fs::path root = resolveWorkspaceRoot() / "models";
    std::error_code ec;
    fs::create_directories(root, ec);
    return root;
}

// Gets the recent models file path, creating directory if needed.
// @return Path to recent_models.txt
fs::path recentModelsPath() {
    const fs::path root = resolveWorkspaceRoot() / "profiles";
    std::error_code ec;
    fs::create_directories(root, ec);
    return root / "recent_models.txt";
}

// Sanitizes model name by replacing invalid characters with underscore.
// Allows only alphanumeric, underscore, and hyphen.
// @param raw Raw model name input
// @return Sanitized model name
std::string sanitizeModelName(const std::string& raw) {
    std::string name = raw;
    if (name.empty()) {
        return {};
    }
    for (char& c : name) {
        const bool valid =
            (c >= 'a' && c <= 'z') ||
            (c >= 'A' && c <= 'Z') ||
            (c >= '0' && c <= '9') ||
            c == '_' || c == '-';
        if (!valid) {
            c = '_';
        }
    }
    return name;
}

// Joins tag vector into comma-separated string.
// @param tags Vector of tag strings
// @return Comma-separated tag string
std::string joinTags(const std::vector<std::string>& tags) {
    if (tags.empty()) {
        return {};
    }
    std::ostringstream out;
    for (std::size_t i = 0; i < tags.size(); ++i) {
        if (i != 0) {
            out << ", ";
        }
        out << tags[i];
    }
    return out.str();
}

// Checks if model matches search query (case-insensitive).
// Searches in name, id, description, author, and tags.
// @param model Model info to check
// @param lowerQuery Lowercase search query
// @return true if model matches query
bool modelMatchesSearch(const ModelInfo& model, const std::string& lowerQuery) {
    if (lowerQuery.empty()) {
        return true;
    }
    if (toLowerCopy(model.name).find(lowerQuery) != std::string::npos) {
        return true;
    }
    if (toLowerCopy(model.model_id).find(lowerQuery) != std::string::npos) {
        return true;
    }
    if (toLowerCopy(model.description).find(lowerQuery) != std::string::npos) {
        return true;
    }
    if (toLowerCopy(model.author).find(lowerQuery) != std::string::npos) {
        return true;
    }
    if (toLowerCopy(joinTags(model.tags)).find(lowerQuery) != std::string::npos) {
        return true;
    }
    return false;
}

// Gets string value from JSON, falling back to default for missing/non-string.
// Also handles numeric types by converting to string.
// @param j JSON object to query
// @param key Key to retrieve
// @param fallback Default value if key missing or wrong type
// @return String value or fallback
std::string jsonStringOr(const json& j, const char* key, std::string fallback = {}) {
    if (!j.contains(key)) {
        return fallback;
    }
    const auto& value = j.at(key);
    if (value.is_string()) {
        return value.get<std::string>();
    }
    if (value.is_number_integer() || value.is_number_unsigned() || value.is_number_float()) {
        return value.dump();
    }
    return fallback;
}

std::string compatibilityLabel(const std::string& minimumEngineVersion);

struct TemplateDefinition {
    std::string id;
    fs::path source;
};

// Loads template registry from templates.json file.
// @return Vector of template definitions
std::vector<TemplateDefinition> loadTemplateRegistry() {
    const fs::path registryPath = modelsRoot() / "templates.json";
    if (!fs::exists(registryPath)) {
        return {};
    }

    try {
        std::ifstream in(registryPath);
        if (!in) {
            return {};
        }

        json registry = json::parse(in);
        if (!registry.contains("templates") || !registry["templates"].is_array()) {
            return {};
        }

        std::vector<TemplateDefinition> templates;
        for (const auto& entry : registry["templates"]) {
            if (!entry.is_object()) {
                continue;
            }
            const std::string id = jsonStringOr(entry, "id");
            const std::string sourceValue = jsonStringOr(entry, "source");
            if (id.empty() || sourceValue.empty()) {
                continue;
            }

            fs::path sourcePath = sourceValue;
            if (sourcePath.is_relative()) {
                sourcePath = modelsRoot() / sourcePath;
            }
            templates.push_back(TemplateDefinition{id, sourcePath});
        }

        return templates;
    } catch (...) {
        return {};
    }
}

// Resolves template source path using registry.
// @param templateName Template identifier to find
// @return Path to template if found, empty optional otherwise
std::optional<fs::path> resolveTemplateSourceByRegistry(const std::string& templateName) {
    const auto templates = loadTemplateRegistry();
    for (const auto& candidate : templates) {
        if (candidate.id == templateName && fs::exists(candidate.source)) {
            return candidate.source;
        }
    }
    return std::nullopt;
}

// Resolves template source path by discovering .simmodel files.
// @param templateName Template identifier to find
// @return Path to first matching template or nullopt
std::optional<fs::path> resolveTemplateSourceByDiscovery(const std::string& templateName) {
    std::vector<fs::path> candidates;
    const fs::path root = modelsRoot();
    if (fs::exists(root)) {
        for (const auto& entry : fs::directory_iterator(root)) {
            if ((entry.is_directory() || entry.is_regular_file()) && entry.path().extension() == ".simmodel") {
                candidates.push_back(entry.path());
            }
        }
    }

    if (candidates.empty()) {
        return std::nullopt;
    }

    std::sort(candidates.begin(), candidates.end());
    if (templateName == "advection_diffusion" && candidates.size() > 1u) {
        return candidates[1];
    }
    return candidates.front();
}

// Resolves template source using registry, then discovery as fallback.
// @param templateName Template identifier to find
// @return Path to template or nullopt
std::optional<fs::path> resolveTemplateSource(const std::string& templateName) {
    if (auto byRegistry = resolveTemplateSourceByRegistry(templateName)) {
        return byRegistry;
    }
    return resolveTemplateSourceByDiscovery(templateName);
}

// Copies template to model path, handling both directory and file templates.
// For directories: copies recursively. For files: parses and writes JSON components.
// @param templateSource Path to template source
// @param modelPath Destination path for new model
// @return true if materialization succeeded
bool materializeTemplateToModelPath(const fs::path& templateSource, const fs::path& modelPath) {
    std::error_code ec;
    if (fs::is_directory(templateSource, ec)) {
        fs::copy(templateSource, modelPath, fs::copy_options::recursive | fs::copy_options::overwrite_existing, ec);
        return !ec;
    }

    try {
        const ws::ModelContext templateContext = ws::ModelParser::load(templateSource);
        if (!templateContext.metadata_json.empty()) {
            std::ofstream metadataOut(modelPath / "metadata.json");
            metadataOut << templateContext.metadata_json;
        }
        if (!templateContext.version_json.empty()) {
            std::ofstream versionOut(modelPath / "version.json");
            versionOut << templateContext.version_json;
        }
        if (!templateContext.model_json.empty()) {
            std::ofstream modelOut(modelPath / "model.json");
            modelOut << templateContext.model_json;
        }
        if (!templateContext.ir_logic_string.empty()) {
            std::ofstream logicOut(modelPath / "logic.ir");
            logicOut << templateContext.ir_logic_string;
        }
        return true;
    } catch (...) {
        return false;
    }
}

// Parses metadata.json into ModelInfo structure.
// @param info ModelInfo to populate
// @param metadataPath Path to metadata.json
void parseMetadataInto(ModelInfo& info, const fs::path& metadataPath) {
    if (!fs::exists(metadataPath)) {
        return;
    }

    try {
        std::ifstream in(metadataPath);
        if (!in) {
            return;
        }

        json meta = json::parse(in);
        if (meta.contains("name") && meta["name"].is_string() && !meta["name"].get<std::string>().empty()) {
            info.name = meta["name"].get<std::string>();
        }
        if (meta.contains("id") && meta["id"].is_string() && !meta["id"].get<std::string>().empty()) {
            info.model_id = meta["id"].get<std::string>();
        }
        if (meta.contains("author") && meta["author"].is_string()) {
            info.author = meta["author"].get<std::string>();
        }
        if (meta.contains("creation_date") && meta["creation_date"].is_string()) {
            info.creation_date = meta["creation_date"].get<std::string>();
        }
        if (meta.contains("description") && meta["description"].is_string()) {
            info.description = meta["description"].get<std::string>();
        }
        if (meta.contains("tags") && meta["tags"].is_array()) {
            info.tags.clear();
            for (const auto& tag : meta["tags"]) {
                if (tag.is_string()) {
                    info.tags.push_back(tag.get<std::string>());
                }
            }
        }
        if (meta.contains("version") && meta["version"].is_string() && info.version == "unknown") {
            info.version = meta["version"].get<std::string>();
        }
    } catch (...) {
        // leave metadata fields as-is
    }
}

// Parses version.json into ModelInfo structure.
// @param info ModelInfo to populate
// @param versionPath Path to version.json
void parseVersionInto(ModelInfo& info, const fs::path& versionPath) {
    if (!fs::exists(versionPath)) {
        return;
    }

    try {
        std::ifstream in(versionPath);
        if (!in) {
            return;
        }

        json version = json::parse(in);
        if (version.contains("format_version") && version["format_version"].is_string()) {
            info.format_version = version["format_version"].get<std::string>();
        }
        if (version.contains("model_version") && version["model_version"].is_string()) {
            info.version = version["model_version"].get<std::string>();
        } else if (info.version == "unknown" && info.format_version != "unknown") {
            info.version = info.format_version;
        }
        if (version.contains("minimum_engine_version") && version["minimum_engine_version"].is_string()) {
            info.minimum_engine_version = version["minimum_engine_version"].get<std::string>();
            info.compatibility = compatibilityLabel(info.minimum_engine_version);
        }
    } catch (...) {
        // leave version fields as-is
    }
}

// Parses model.json into ModelInfo structure (minimal fields).
// @param info ModelInfo to populate
// @param modelJsonPath Path to model.json
void parseModelJsonInto(ModelInfo& info, const fs::path& modelJsonPath) {
    if (!fs::exists(modelJsonPath)) {
        return;
    }

    try {
        std::ifstream in(modelJsonPath);
        if (!in) {
            return;
        }

        json model = json::parse(in);
        if (info.model_id.empty() && model.contains("id") && model["id"].is_string()) {
            info.model_id = model["id"].get<std::string>();
        }
        if (info.version == "unknown" && model.contains("version") && model["version"].is_string()) {
            info.version = model["version"].get<std::string>();
        }
    } catch (...) {
        // leave model fields as-is
    }
}

// Formats filesystem time as local datetime string.
// @param timePoint Filesystem time point
// @return Formatted datetime string "YYYY-MM-DD HH:MM:SS" or "n/a" on error
std::string formatFileTime(const fs::file_time_type& timePoint) {
    try {
        const auto now = std::chrono::system_clock::now();
        const auto fsNow = fs::file_time_type::clock::now();
        const auto systemTime = now + std::chrono::duration_cast<std::chrono::system_clock::duration>(timePoint - fsNow);
    const std::time_t tt = std::chrono::system_clock::to_time_t(systemTime);

        std::tm tm{};
#ifdef _WIN32
        localtime_s(&tm, &tt);
#else
        localtime_r(&tt, &tm);
#endif
        std::ostringstream out;
        out << std::put_time(&tm, "%Y-%m-%d %H:%M:%S");
        return out.str();
    } catch (...) {
        return "n/a";
    }
}

// Reads entire file into string.
// @param path Path to file
// @return File contents or empty string if not found
std::string readTextFile(const fs::path& path) {
    if (!fs::exists(path)) {
        return {};
    }
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        return {};
    }
    std::ostringstream out;
    out << in.rdbuf();
    return out.str();
}

// Computes FNV-1a 64-bit hash of string.
// @param text Input string to hash
// @return 64-bit hash value
std::uint64_t fnv1a64(const std::string& text) {
    std::uint64_t hash = 1469598103934665603ull;
    for (unsigned char c : text) {
        hash ^= static_cast<std::uint64_t>(c);
        hash *= 1099511628211ull;
    }
    return hash;
}

// Compares semantic version strings (e.g., "1.2.3" vs "2.0.0").
// @param lhs Left version string
// @param rhs Right version string
// @return -1 if lhs < rhs, 0 if equal, 1 if lhs > rhs
int compareVersion(const std::string& lhs, const std::string& rhs) {
    std::istringstream lss(lhs);
    std::istringstream rss(rhs);
    for (;;) {
        int lv = 0;
        int rv = 0;
        char dot = '.';
        const bool lOk = static_cast<bool>(lss >> lv);
        const bool rOk = static_cast<bool>(rss >> rv);
        if (!lOk && !rOk) {
            return 0;
        }
        if (lv != rv) {
            return lv < rv ? -1 : 1;
        }
        if (!(lss >> dot)) {
            lss.clear();
        }
        if (!(rss >> dot)) {
            rss.clear();
        }
    }
}

// Generates compatibility label based on minimum engine version.
// @param minimumEngineVersion Minimum engine version required
// @return "compatible" or "incompatible"
std::string compatibilityLabel(const std::string& minimumEngineVersion) {
    if (minimumEngineVersion.empty()) {
        return "unknown";
    }
    return compareVersion(minimumEngineVersion, kCurrentEngineVersion) <= 0 ? "compatible" : "incompatible";
}

// Computes identity hash for model by hashing all component files.
// @param modelDir Path to model directory or single file
// @return 16-digit hexadecimal hash string
std::string identityHashForModel(const fs::path& modelDir) {
    if (fs::is_regular_file(modelDir)) {
        const std::string payload = readTextFile(modelDir);
        std::ostringstream out;
        out << std::hex << std::setw(16) << std::setfill('0') << fnv1a64(payload);
        return out.str();
    }

    const std::string payload = readTextFile(modelDir / "metadata.json") + '\n' +
        readTextFile(modelDir / "version.json") + '\n' +
        readTextFile(modelDir / "model.json") + '\n' +
        readTextFile(modelDir / "logic.ir");
    std::ostringstream out;
    out << std::hex << std::setw(16) << std::setfill('0') << fnv1a64(payload);
    return out.str();
}

// Returns a decision-focused readiness label for launcher workflows.
// @param model Model metadata and package file presence
// @return Readiness label for fast model selection
const char* launchReadinessLabel(const ModelInfo& model) {
    if (model.compatibility == "incompatible") {
        return "Engine mismatch risk";
    }
    if (!model.has_model_file) {
        return "Package incomplete";
    }
    if (!(model.has_metadata_file && model.has_version_file && model.has_logic_file)) {
        return "Needs review";
    }
    return "Ready";
}

// Returns accent color for launch readiness label.
// @param model Model metadata and package state
// @return UI color corresponding to readiness level
ImVec4 launchReadinessColor(const ModelInfo& model) {
    const std::string status = launchReadinessLabel(model);
    if (status == "Ready") {
        return ImVec4(0.58f, 0.88f, 0.62f, 1.0f);
    }
    if (status == "Needs review") {
        return ImVec4(0.95f, 0.80f, 0.45f, 1.0f);
    }
    return ImVec4(0.95f, 0.55f, 0.45f, 1.0f);
}

LaunchConfidenceInfo buildFallbackLaunchConfidenceInfo(const ModelInfo& model) {
    LaunchConfidenceInfo info;
    info.readinessLabel = launchReadinessLabel(model);
    std::ostringstream compatibility;
    compatibility << "Compatibility: " << (model.compatibility.empty() ? "n/a" : model.compatibility);
    if (model.minimum_engine_version != "n/a" && model.minimum_engine_version != "unknown") {
        compatibility << " | Minimum engine: " << model.minimum_engine_version;
    }
    info.compatibilitySummary = compatibility.str();

    int packageWarnings = 0;
    if (!model.has_metadata_file) ++packageWarnings;
    if (!model.has_version_file) ++packageWarnings;
    if (!model.has_model_file) ++packageWarnings;
    if (!model.has_logic_file) ++packageWarnings;
    info.packageWarningCount = packageWarnings;

    std::ostringstream packageSummary;
    packageSummary << "Package integrity warnings: " << packageWarnings << "/4 file checks";
    if (packageWarnings == 0) {
        packageSummary << " (all expected files present)";
    }
    info.packageSummary = packageSummary.str();

    info.recommendedActionLabel = "Create new world";
    return info;
}

// Generates a unique destination path by appending _copy, _copy_2, ... to stem.
// @param destination Requested destination path
// @return First non-existing destination variant
fs::path makeUniqueImportDestination(const fs::path& destination) {
    if (!fs::exists(destination)) {
        return destination;
    }

    const fs::path parent = destination.parent_path();
    const std::string stem = destination.stem().string();
    const fs::path extension = destination.extension();

    int copyIndex = 1;
    for (;;) {
        const std::string suffix = copyIndex == 1 ? "_copy" : ("_copy_" + std::to_string(copyIndex));
        const fs::path candidate = parent / (stem + suffix + extension.string());
        if (!fs::exists(candidate)) {
            return candidate;
        }
        ++copyIndex;
    }
}

std::string makeMultilinePreview(
    const std::string& text,
    const int maxLines,
    const float approxWidth,
    bool* wasTruncated = nullptr) {
    if (wasTruncated != nullptr) {
        *wasTruncated = false;
    }
    if (text.empty() || maxLines <= 0) {
        return text;
    }

    const int charsPerLine = std::max(20, static_cast<int>(approxWidth / 7.0f));
    const int maxChars = std::max(charsPerLine, charsPerLine * maxLines);
    if (static_cast<int>(text.size()) <= maxChars) {
        return text;
    }

    if (wasTruncated != nullptr) {
        *wasTruncated = true;
    }

    std::string preview = text.substr(0, static_cast<std::size_t>(maxChars));
    const std::size_t naturalCut = preview.find_last_of(" \t\n");
    if (naturalCut != std::string::npos && naturalCut > static_cast<std::size_t>(maxChars / 2)) {
        preview.resize(naturalCut);
    }
    preview += "...";
    return preview;
}

} // namespace

// Constructs model selector and initializes UI state.
// Loads recent models and refreshes model list on construction.
ModelSelector::ModelSelector()
    : window_open(true),
      selected_model_index(-1),
      show_new_model_dialog(false),
            show_import_dialog(false),
        show_import_conflict_dialog(false),
            show_rename_dialog(false),
            show_export_dialog(false),
            show_delete_confirm_dialog(false),
            show_column_id(true),
            show_column_version(true),
            show_column_format_version(false),
            show_column_minimum_engine_version(false),
            show_column_author(true),
            show_column_creation_date(false),
            show_column_tags(false),
            show_column_description(true),
            show_column_compatibility(false),
            show_column_identity_hash(false),
            show_column_last_modified(true),
                filter_compatible_only(false),
                search_query{0},
            pending_rename_name{0},
            pending_export_path{0},
            import_source_path{0},
            import_target_name{0},
            pending_import_destination{0},
            import_replace_existing(false),
            pending_action_model_index(-1) {
            loadRecentModels();
    refreshModelList();
}

// Destructor; default.
ModelSelector::~ModelSelector() = default;

// Refreshes model list by scanning models directory.
// Preserves selection by identity hash after refresh.
void ModelSelector::refreshModelList() {
    const std::string selectedIdentity =
        (selected_model_index >= 0 && selected_model_index < static_cast<int>(models.size()))
            ? models[selected_model_index].identity_hash
            : std::string{};

    models.clear();

    const fs::path models_dir = modelsRoot();
    if (!fs::exists(models_dir)) {
        return;
    }

    for (const auto& entry : fs::directory_iterator(models_dir)) {
        if ((entry.is_directory() || entry.is_regular_file()) && entry.path().extension() == ".simmodel") {
            ModelInfo info;
            info.name = entry.path().stem().string();
            info.path = entry.path().string();
            std::error_code ec;
            info.last_modified = fs::last_write_time(entry.path(), ec);
            info.identity_hash = identityHashForModel(entry.path());
            info.has_model_file = entry.is_regular_file();

            if (entry.is_directory()) {
                info.has_metadata_file = fs::exists(entry.path() / "metadata.json");
                info.has_version_file = fs::exists(entry.path() / "version.json");
                info.has_model_file = fs::exists(entry.path() / "model.json");
                info.has_logic_file = fs::exists(entry.path() / "logic.ir");
                parseMetadataInto(info, entry.path() / "metadata.json");
                parseVersionInto(info, entry.path() / "version.json");
                parseModelJsonInto(info, entry.path() / "model.json");
            }

            if (info.model_id.empty()) {
                info.model_id = info.name;
            }
            if (info.author.empty()) {
                info.author = "n/a";
            }
            if (info.creation_date.empty()) {
                info.creation_date = "n/a";
            }
            if (info.description.empty()) {
                info.description = "n/a";
            }
            if (info.format_version == "unknown") {
                info.format_version = "n/a";
            }
            if (info.minimum_engine_version == "unknown") {
                info.minimum_engine_version = "n/a";
            }
            if (info.compatibility == "n/a" && info.minimum_engine_version != "n/a") {
                info.compatibility = compatibilityLabel(info.minimum_engine_version);
            }
            if (info.compatibility == "unknown") {
                info.compatibility = "n/a";
            }
            
            models.push_back(info);
        }
    }

    // Sort by last modified (newest first)
    std::sort(models.begin(), models.end(),
        [](const ModelInfo& a, const ModelInfo& b) {
            return a.last_modified > b.last_modified;
        });

    selected_model_index = -1;
    if (!selectedIdentity.empty()) {
        for (int i = 0; i < static_cast<int>(models.size()); ++i) {
            if (models[i].identity_hash == selectedIdentity) {
                selected_model_index = i;
                break;
            }
        }
    }
}

// Loads recent models from persistent storage file.
// Reads up to 16 paths from recent_models.txt.
void ModelSelector::loadRecentModels() {
    recent_model_paths.clear();
    const fs::path path = recentModelsPath();
    std::ifstream in(path);
    if (!in.is_open()) {
        return;
    }

    std::string line;
    while (std::getline(in, line)) {
        if (line.empty()) {
            continue;
        }
        recent_model_paths.push_back(line);
        if (recent_model_paths.size() >= 16u) {
            break;
        }
    }
}

// Saves recent models list to persistent storage.
// Writes each path on its own line.
void ModelSelector::saveRecentModels() const {
    const fs::path path = recentModelsPath();
    std::ofstream out(path, std::ios::trunc);
    if (!out.is_open()) {
        return;
    }
    for (const auto& modelPath : recent_model_paths) {
        out << modelPath << '\n';
    }
}

// Records model as recently used.
// Moves model to front of list, removes duplicates, limits to 8 entries.
// @param model Model info to record
void ModelSelector::recordRecentModel(const ModelInfo& model) {
    if (model.path.empty()) {
        return;
    }

    const auto normalized = fs::path(model.path).lexically_normal().string();
    recent_model_paths.erase(
        std::remove(recent_model_paths.begin(), recent_model_paths.end(), normalized),
        recent_model_paths.end());
    recent_model_paths.insert(recent_model_paths.begin(), normalized);
    if (recent_model_paths.size() > 8u) {
        recent_model_paths.resize(8u);
    }
    saveRecentModels();
}

// Renders model selector UI.
// Displays model table with filtering, sorting, and action buttons.
// @param available_size Window size for layout
void ModelSelector::render(ImVec2 available_size) {
    if (!window_open) return;

    ImGui::SetNextWindowPos(ImVec2(0, 0), ImGuiCond_Always);
    ImGui::SetNextWindowSize(available_size, ImGuiCond_Always);

    const ImGuiWindowFlags flags =
        ImGuiWindowFlags_NoMove |
        ImGuiWindowFlags_NoResize |
        ImGuiWindowFlags_NoCollapse |
        ImGuiWindowFlags_NoTitleBar;

    if (ImGui::Begin("Model Selector Full Page", nullptr, flags)) {
        // Top action bar
        bool hasRecentAvailable = false;
        int recentAvailableIndex = -1;
        if (!recent_model_paths.empty()) {
            for (const auto& recentPath : recent_model_paths) {
                auto it = std::find_if(models.begin(), models.end(), [&](const ModelInfo& m) {
                    return fs::path(m.path).lexically_normal().string() == recentPath;
                });
                if (it != models.end()) {
                    hasRecentAvailable = true;
                    recentAvailableIndex = static_cast<int>(std::distance(models.begin(), it));
                    break;
                }
            }
        }

        if (!hasRecentAvailable) {
            ImGui::BeginDisabled();
        }
        if (ImGui::Button("Continue last model", ImVec2(160, 0))) {
            if (recentAvailableIndex >= 0 && recentAvailableIndex < static_cast<int>(models.size()) && on_load_model) {
                selected_model_index = recentAvailableIndex;
                const auto& recent = models[static_cast<std::size_t>(recentAvailableIndex)];
                recordRecentModel(recent);
                on_load_model(recent);
            }
        }
        if (!hasRecentAvailable) {
            ImGui::EndDisabled();
        }
        ImGui::SameLine();
        if (ImGui::Button("New Model", ImVec2(100, 0))) {
            show_new_model_dialog = true;
            ImGui::OpenPopup("New Model");
        }
        ImGui::SameLine();
        if (ImGui::Button("Import", ImVec2(100, 0))) {
            show_import_dialog = true;
            ImGui::OpenPopup("Import Model");
        }
        ImGui::SameLine();
        if (ImGui::Button("Refresh", ImVec2(100, 0))) {
            refreshModelList();
        }

        ImGui::SameLine();
        ImGui::SetNextItemWidth(280.0f);
        ImGui::InputTextWithHint("##model_search", "Search name/id/author/description/tags", search_query, IM_ARRAYSIZE(search_query));
        ImGui::SameLine();
        ImGui::Checkbox("Compatible only", &filter_compatible_only);
        
        ImGui::Separator();
        ImGui::TextDisabled("Data origins: model fields from metadata.json / version.json / model.json; last modified from filesystem metadata; identity hash computed from metadata.json + version.json + model.json + logic.ir.");

        if (!gDataOperationReceipt.empty()) {
            ImGui::BeginChild("ModelDataOperationReceipt", ImVec2(0.0f, 74.0f), true);
            ImGui::TextWrapped("%s", gDataOperationReceipt.c_str());
            if (!gDataOperationTechnicalDetail.empty()) {
                ImGui::TextDisabled("Technical detail available. Hover for raw output.");
                if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort)) {
                    ImGui::SetTooltip("%s", gDataOperationTechnicalDetail.c_str());
                }
            }
            ImGui::SameLine();
            if (SecondaryButton("Clear##model_data_receipt", ImVec2(92.0f, 24.0f))) {
                gDataOperationReceipt.clear();
                gDataOperationRecoveryHint.clear();
                gDataOperationTechnicalDetail.clear();
            }
            ImGui::EndChild();
        }

        if (!recent_model_paths.empty()) {
            ImGui::TextDisabled("Recent models:");
            int shownRecent = 0;
            for (const auto& recentPath : recent_model_paths) {
                if (shownRecent >= 5) {
                    break;
                }
                auto it = std::find_if(models.begin(), models.end(), [&](const ModelInfo& m) {
                    return fs::path(m.path).lexically_normal().string() == recentPath;
                });
                if (it == models.end()) {
                    continue;
                }

                const int idx = static_cast<int>(std::distance(models.begin(), it));
                const auto& recent = *it;
                ImGui::SameLine();
                if (ImGui::SmallButton(recent.name.c_str())) {
                    selected_model_index = idx;
                    if (on_load_model) {
                        recordRecentModel(recent);
                        on_load_model(recent);
                    }
                }
                ++shownRecent;
            }
            ImGui::NewLine();
        }

        const bool hasSelected = selected_model_index >= 0 && selected_model_index < static_cast<int>(models.size());
        if (hasSelected) {
            const auto& selected = models[selected_model_index];
            LaunchConfidenceInfo launchInfo = buildFallbackLaunchConfidenceInfo(selected);
            if (on_get_launch_confidence) {
                LaunchConfidenceInfo provided = on_get_launch_confidence(selected);
                if (!provided.readinessLabel.empty()) {
                    launchInfo.readinessLabel = std::move(provided.readinessLabel);
                }
                if (!provided.compatibilitySummary.empty()) {
                    launchInfo.compatibilitySummary = std::move(provided.compatibilitySummary);
                }
                if (!provided.packageSummary.empty()) {
                    launchInfo.packageSummary = std::move(provided.packageSummary);
                }
                if (!provided.lastSuccessfulWorldName.empty()) {
                    launchInfo.lastSuccessfulWorldName = std::move(provided.lastSuccessfulWorldName);
                }
                if (!provided.lastSuccessfulWorldTimestamp.empty()) {
                    launchInfo.lastSuccessfulWorldTimestamp = std::move(provided.lastSuccessfulWorldTimestamp);
                }
                if (!provided.recommendedActionLabel.empty()) {
                    launchInfo.recommendedActionLabel = std::move(provided.recommendedActionLabel);
                }
                launchInfo.canOpenLastWorld = provided.canOpenLastWorld;
                launchInfo.packageWarningCount = provided.packageWarningCount;
                if (launchInfo.recommendedActionLabel.empty()) {
                    launchInfo.recommendedActionLabel = launchInfo.canOpenLastWorld ? "Open last world" : "Create new world";
                }
            }
            if (launchInfo.recommendedActionLabel.empty()) {
                launchInfo.recommendedActionLabel = launchInfo.canOpenLastWorld ? "Open last world" : "Create new world";
            }

            ImGui::Text("Selected: %s", selected.name.c_str());
            ImGui::SameLine();
            if (ImGui::Button("Load")) {
                if (on_load_model) {
                    recordRecentModel(selected);
                    on_load_model(selected);
                }
            }
            ImGui::SameLine();
            if (ImGui::Button("Edit")) {
                if (on_edit_model) {
                    recordRecentModel(selected);
                    on_edit_model(selected);
                }
            }
            ImGui::Spacing();
            ImGui::BeginChild("LaunchConfidenceCard", ImVec2(0.0f, 132.0f), true);
            ImGui::TextDisabled("Launch confidence");
            ImGui::TextColored(launchReadinessColor(selected), "%s", launchInfo.readinessLabel.c_str());
            ImGui::TextDisabled("%s", launchInfo.compatibilitySummary.c_str());
            ImGui::TextDisabled("%s", launchInfo.packageSummary.c_str());
            if (launchInfo.canOpenLastWorld && !launchInfo.lastSuccessfulWorldName.empty()) {
                ImGui::Text("Last successful world: %s", launchInfo.lastSuccessfulWorldName.c_str());
                if (!launchInfo.lastSuccessfulWorldTimestamp.empty()) {
                    ImGui::TextDisabled("Most recent save/open: %s", launchInfo.lastSuccessfulWorldTimestamp.c_str());
                }
            } else {
                ImGui::TextDisabled("No stored world is ready to resume for this model.");
            }
            ImGui::Spacing();
            if (PrimaryButton(launchInfo.recommendedActionLabel.c_str(), ImVec2(-1.0f, 30.0f))) {
                if (on_launch_recommended_entry) {
                    on_launch_recommended_entry(selected, launchInfo);
                } else if (on_load_model) {
                    on_load_model(selected);
                }
            }
            ImGui::EndChild();
            ImGui::SameLine();
            if (ImGui::Button("Duplicate")) {
                duplicateModel(selected);
            }
            ImGui::SameLine();
            if (ImGui::Button("Rename")) {
                pending_action_model_index = selected_model_index;
                std::snprintf(pending_rename_name, sizeof(pending_rename_name), "%s", selected.name.c_str());
                show_rename_dialog = true;
                ImGui::OpenPopup("Rename Model");
            }
            ImGui::SameLine();
            if (ImGui::Button("Export")) {
                pending_action_model_index = selected_model_index;
                std::snprintf(pending_export_path, sizeof(pending_export_path), "%s_export.simmodel", selected.name.c_str());
                show_export_dialog = true;
                ImGui::OpenPopup("Export Model");
            }
            ImGui::SameLine();
            if (ImGui::Button("Delete")) {
                pending_action_model_index = selected_model_index;
                show_delete_confirm_dialog = true;
                ImGui::OpenPopup("Delete Model");
            }
            ImGui::Separator();
        }
        
        // Model table
        const ImGuiTableFlags tableFlags =
            ImGuiTableFlags_Borders |
            ImGuiTableFlags_RowBg |
            ImGuiTableFlags_Sortable |
            ImGuiTableFlags_ScrollX |
            ImGuiTableFlags_ScrollY |
            ImGuiTableFlags_Resizable;

        const int visible_column_count =
            2 +
            (show_column_id ? 1 : 0) +
            (show_column_version ? 1 : 0) +
            (show_column_format_version ? 1 : 0) +
            (show_column_minimum_engine_version ? 1 : 0) +
            (show_column_author ? 1 : 0) +
            (show_column_creation_date ? 1 : 0) +
            (show_column_tags ? 1 : 0) +
            (show_column_description ? 1 : 0) +
            (show_column_compatibility ? 1 : 0) +
            (show_column_identity_hash ? 1 : 0) +
            (show_column_last_modified ? 1 : 0);

        std::vector<int> filtered_indices;
        filtered_indices.reserve(models.size());
        const std::string queryLower = toLowerCopy(std::string(search_query));
        for (int i = 0; i < static_cast<int>(models.size()); ++i) {
            const auto& model = models[static_cast<std::size_t>(i)];
            if (filter_compatible_only && model.compatibility != "compatible") {
                continue;
            }
            if (!modelMatchesSearch(model, queryLower)) {
                continue;
            }
            filtered_indices.push_back(i);
        }

        if (selected_model_index >= 0 &&
            std::find(filtered_indices.begin(), filtered_indices.end(), selected_model_index) == filtered_indices.end()) {
            selected_model_index = -1;
        }

        auto renderColumnMenu = [&]() {
            ImGui::TextDisabled("Toggle table columns");
            ImGui::Separator();
            ImGui::Checkbox("ID", &show_column_id);
            ImGui::Checkbox("Version", &show_column_version);
            ImGui::Checkbox("Format version", &show_column_format_version);
            ImGui::Checkbox("Minimum engine version", &show_column_minimum_engine_version);
            ImGui::Checkbox("Author", &show_column_author);
            ImGui::Checkbox("Creation date", &show_column_creation_date);
            ImGui::Checkbox("Tags", &show_column_tags);
            ImGui::Checkbox("Description", &show_column_description);
            ImGui::Checkbox("Compatibility", &show_column_compatibility);
            ImGui::Checkbox("Identity hash", &show_column_identity_hash);
            ImGui::Checkbox("Last modified", &show_column_last_modified);
            ImGui::Separator();
            if (ImGui::MenuItem("Show all")) {
                show_column_id = true;
                show_column_version = true;
                show_column_format_version = true;
                show_column_minimum_engine_version = true;
                show_column_author = true;
                show_column_creation_date = true;
                show_column_tags = true;
                show_column_description = true;
                show_column_compatibility = true;
                show_column_identity_hash = true;
                show_column_last_modified = true;
            }
            if (ImGui::MenuItem("Show compact")) {
                show_column_id = true;
                show_column_version = true;
                show_column_author = true;
                show_column_description = true;
                show_column_identity_hash = true;
                show_column_last_modified = true;
                show_column_format_version = false;
                show_column_minimum_engine_version = false;
                show_column_tags = false;
                show_column_compatibility = false;
            }
        };

        if (ImGui::BeginTable("ModelsTable", visible_column_count, tableFlags, ImVec2(0, ImGui::GetContentRegionAvail().y - 60.0f))) {
            ImGui::TableSetupScrollFreeze(0, 1);
            ImGui::TableSetupColumn("Name", ImGuiTableColumnFlags_WidthFixed, 220.0f, ColName);
            ImGui::TableSetupColumn("Launch status", ImGuiTableColumnFlags_WidthFixed, 130.0f, ColLaunchStatus);
            if (show_column_id) ImGui::TableSetupColumn("ID", ImGuiTableColumnFlags_WidthFixed, 180.0f, ColId);
            if (show_column_version) ImGui::TableSetupColumn("Version", ImGuiTableColumnFlags_WidthFixed | ImGuiTableColumnFlags_DefaultSort, 90.0f, ColVersion);
            if (show_column_format_version) ImGui::TableSetupColumn("Format", ImGuiTableColumnFlags_WidthFixed, 90.0f, ColFormatVersion);
            if (show_column_minimum_engine_version) ImGui::TableSetupColumn("Min Engine", ImGuiTableColumnFlags_WidthFixed, 120.0f, ColMinimumEngineVersion);
            if (show_column_author) ImGui::TableSetupColumn("Author", ImGuiTableColumnFlags_WidthFixed, 150.0f, ColAuthor);
            if (show_column_creation_date) ImGui::TableSetupColumn("Created", ImGuiTableColumnFlags_WidthFixed, 150.0f, ColCreationDate);
            if (show_column_tags) ImGui::TableSetupColumn("Tags", ImGuiTableColumnFlags_WidthFixed, 220.0f, ColTags);
            if (show_column_description) ImGui::TableSetupColumn("Description", ImGuiTableColumnFlags_WidthFixed, 360.0f, ColDescription);
            if (show_column_compatibility) ImGui::TableSetupColumn("Compatibility", ImGuiTableColumnFlags_WidthFixed, 120.0f, ColCompatibility);
            if (show_column_identity_hash) ImGui::TableSetupColumn("Identity Hash", ImGuiTableColumnFlags_WidthFixed, 170.0f, ColIdentityHash);
            if (show_column_last_modified) ImGui::TableSetupColumn("Last Modified", ImGuiTableColumnFlags_WidthFixed, 170.0f, ColLastModified);

            auto renderHeaderWithColumnMenu = [&](const char* label) {
                ImGui::TableNextColumn();
                ImGui::TableHeader(label);
                if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenBlockedByPopup) && ImGui::IsMouseReleased(ImGuiMouseButton_Right)) {
                    ImGui::OpenPopup("ModelSelectorColumnsPopup");
                }
            };

            ImGui::TableNextRow(ImGuiTableRowFlags_Headers);
            renderHeaderWithColumnMenu("Name");
            renderHeaderWithColumnMenu("Launch status");
            if (show_column_id) {
                renderHeaderWithColumnMenu("ID");
            }
            if (show_column_version) {
                renderHeaderWithColumnMenu("Version");
            }
            if (show_column_format_version) {
                renderHeaderWithColumnMenu("Format");
            }
            if (show_column_minimum_engine_version) {
                renderHeaderWithColumnMenu("Min Engine");
            }
            if (show_column_author) {
                renderHeaderWithColumnMenu("Author");
            }
            if (show_column_creation_date) {
                renderHeaderWithColumnMenu("Created");
            }
            if (show_column_tags) {
                renderHeaderWithColumnMenu("Tags");
            }
            if (show_column_description) {
                renderHeaderWithColumnMenu("Description");
            }
            if (show_column_compatibility) {
                renderHeaderWithColumnMenu("Compatibility");
            }
            if (show_column_identity_hash) {
                renderHeaderWithColumnMenu("Identity Hash");
            }
            if (show_column_last_modified) {
                renderHeaderWithColumnMenu("Last Modified");
            }

            if (ImGui::BeginPopup("ModelSelectorColumnsPopup")) {
                renderColumnMenu();
                ImGui::EndPopup();
            }

            if (ImGuiTableSortSpecs* sortSpecs = ImGui::TableGetSortSpecs()) {
                if (sortSpecs->SpecsCount > 0 && sortSpecs->SpecsDirty) {
                    const ImGuiTableColumnSortSpecs& spec = sortSpecs->Specs[0];
                    const std::string selectedBeforeSort =
                        (selected_model_index >= 0 && selected_model_index < static_cast<int>(models.size()))
                            ? models[selected_model_index].name
                            : std::string{};
                    std::sort(models.begin(), models.end(), [&](const ModelInfo& a, const ModelInfo& b) {
                        int cmp = 0;
                        switch (spec.ColumnUserID) {
                            case ColName: cmp = toLowerCopy(a.name).compare(toLowerCopy(b.name)); break;
                            case ColLaunchStatus: cmp = std::string(launchReadinessLabel(a)).compare(launchReadinessLabel(b)); break;
                            case ColId: cmp = toLowerCopy(a.model_id).compare(toLowerCopy(b.model_id)); break;
                            case ColVersion: cmp = compareVersion(a.version, b.version); break;
                            case ColFormatVersion: cmp = a.format_version.compare(b.format_version); break;
                            case ColMinimumEngineVersion: cmp = a.minimum_engine_version.compare(b.minimum_engine_version); break;
                            case ColAuthor: cmp = toLowerCopy(a.author).compare(toLowerCopy(b.author)); break;
                            case ColCreationDate: cmp = a.creation_date.compare(b.creation_date); break;
                            case ColTags: cmp = joinTags(a.tags).compare(joinTags(b.tags)); break;
                            case ColDescription: cmp = toLowerCopy(a.description).compare(toLowerCopy(b.description)); break;
                            case ColCompatibility: cmp = toLowerCopy(a.compatibility).compare(toLowerCopy(b.compatibility)); break;
                            case ColIdentityHash: cmp = a.identity_hash.compare(b.identity_hash); break;
                            case ColLastModified:
                                cmp = (a.last_modified < b.last_modified) ? -1 : (a.last_modified > b.last_modified ? 1 : 0);
                                break;
                            default:
                                cmp = 0;
                                break;
                        }
                        if (spec.SortDirection == ImGuiSortDirection_Descending) {
                            cmp = -cmp;
                        }
                        if (cmp == 0) {
                            return a.name < b.name;
                        }
                        return cmp < 0;
                    });

                    if (!selectedBeforeSort.empty()) {
                        for (int i = 0; i < static_cast<int>(models.size()); ++i) {
                            if (models[i].name == selectedBeforeSort) {
                                selected_model_index = i;
                                break;
                            }
                        }
                    }

                    sortSpecs->SpecsDirty = false;

                    filtered_indices.clear();
                    for (int i = 0; i < static_cast<int>(models.size()); ++i) {
                        const auto& model = models[static_cast<std::size_t>(i)];
                        if (filter_compatible_only && model.compatibility != "compatible") {
                            continue;
                        }
                        if (!modelMatchesSearch(model, queryLower)) {
                            continue;
                        }
                        filtered_indices.push_back(i);
                    }
                }
            }

            ImGuiListClipper clipper;
            clipper.Begin(static_cast<int>(filtered_indices.size()));
            while (clipper.Step()) {
                for (int row = clipper.DisplayStart; row < clipper.DisplayEnd; ++row) {
                    const int i = filtered_indices[static_cast<std::size_t>(row)];
                    const auto& model = models[static_cast<std::size_t>(i)];
                ImGui::TableNextRow();
                
                // Name column
                ImGui::TableNextColumn();
                const bool row_selected = selected_model_index == i;
                if (ImGui::Selectable(model.name.c_str(), row_selected, ImGuiSelectableFlags_SpanAllColumns | ImGuiSelectableFlags_AllowDoubleClick)) {
                    selected_model_index = i;
                    if (ImGui::IsMouseDoubleClicked(0) && on_edit_model) {
                        recordRecentModel(model);
                        on_edit_model(model);
                    }
                }
                if (row_selected || ImGui::IsItemHovered()) {
                    ImU32 bg = row_selected ? IM_COL32(64, 128, 255, 96) : IM_COL32(96, 96, 128, 64);
                    ImGui::TableSetBgColor(ImGuiTableBgTarget_RowBg0, bg);
                }

                // Launch status column
                ImGui::TableNextColumn();
                ImGui::TextColored(launchReadinessColor(model), "%s", launchReadinessLabel(model));

                // ID column
                if (show_column_id) { ImGui::TableNextColumn(); ImGui::TextDisabled("%s", model.model_id.c_str()); }
                
                // Version column
                if (show_column_version) { ImGui::TableNextColumn(); ImGui::TextDisabled("%s", model.version.c_str()); }

                // Format version column
                if (show_column_format_version) { ImGui::TableNextColumn(); ImGui::TextDisabled("%s", model.format_version.c_str()); }

                // Min engine column
                if (show_column_minimum_engine_version) { ImGui::TableNextColumn(); ImGui::TextDisabled("%s", model.minimum_engine_version.c_str()); }

                // Author column
                if (show_column_author) { ImGui::TableNextColumn(); ImGui::TextDisabled("%s", model.author.c_str()); }

                // Created column
                if (show_column_creation_date) { ImGui::TableNextColumn(); ImGui::TextDisabled("%s", model.creation_date.c_str()); }

                // Tags column
                if (show_column_tags) { ImGui::TableNextColumn(); ImGui::TextDisabled("%s", joinTags(model.tags).c_str()); }

                // Description column
                if (show_column_description) {
                    ImGui::TableNextColumn();
                    const float colWidth = std::max(120.0f, ImGui::GetColumnWidth());
                    bool descriptionTruncated = false;
                    const std::string descriptionPreview = makeMultilinePreview(model.description, 3, colWidth, &descriptionTruncated);
                    ImGui::PushTextWrapPos(ImGui::GetCursorScreenPos().x + colWidth - ImGui::GetStyle().CellPadding.x);
                    ImGui::TextUnformatted(descriptionPreview.c_str());
                    ImGui::PopTextWrapPos();
                    if (descriptionTruncated && ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort)) {
                        ImGui::SetTooltip("%s", model.description.c_str());
                    }
                }
                
                // Compatibility column
                if (show_column_compatibility) { ImGui::TableNextColumn(); ImGui::TextDisabled("%s", model.compatibility.c_str()); }
                
                // Identity hash column
                if (show_column_identity_hash) { ImGui::TableNextColumn(); ImGui::TextDisabled("%s", model.identity_hash.c_str()); }

                // Last modified column
                if (show_column_last_modified) { ImGui::TableNextColumn(); ImGui::TextDisabled("%s", formatFileTime(model.last_modified).c_str()); }
                }
            }
            
            ImGui::EndTable();
        }
        
        ImGui::Separator();
        
        // Model details (if selected)
        if (selected_model_index >= 0 && selected_model_index < static_cast<int>(models.size())) {
            ImGui::TextDisabled("Path: %s", models[selected_model_index].path.c_str());
        }
        
        // Dialogs
        if (show_new_model_dialog) {
            renderNewModelDialog();
        }
        if (show_import_dialog) {
            renderImportDialog();
        }
        if (show_rename_dialog) {
            renderRenameDialog();
        }
        if (show_export_dialog) {
            renderExportDialog();
        }
        if (show_delete_confirm_dialog) {
            renderDeleteConfirmDialog();
        }
        
        ImGui::End();
    }

}

void ModelSelector::renderNewModelDialog() {
    if (ImGui::BeginPopupModal("New Model", &show_new_model_dialog)) {
        static char model_name_buffer[256] = "MyModel";
        ImGui::InputText("Model Name", model_name_buffer, IM_ARRAYSIZE(model_name_buffer));
        
        ImGui::Separator();
        
        ImGui::Text("Choose template:");
        if (ImGui::Selectable("Blank (2D)", false)) {
            createModelFromTemplate("blank_2d", model_name_buffer);
            show_new_model_dialog = false;
            ImGui::CloseCurrentPopup();
        }
        if (ImGui::Selectable("Advection-Diffusion", false)) {
            createModelFromTemplate("advection_diffusion", model_name_buffer);
            show_new_model_dialog = false;
            ImGui::CloseCurrentPopup();
        }
        
        ImGui::Separator();
        
        if (ImGui::Button("Cancel")) {
            show_new_model_dialog = false;
            ImGui::CloseCurrentPopup();
        }
        
        ImGui::EndPopup();
    }
}

void ws::gui::ModelSelector::renderImportDialog() {
    ImGui::SetNextWindowSize(ImVec2(760.0f, 220.0f), ImGuiCond_Appearing);
    if (ImGui::BeginPopupModal("Import Model", &show_import_dialog)) {
        static int operationModeIndex = static_cast<int>(DataOperationMode::Copy);
        ImGui::TextWrapped("Import a .simmodel directory or archive into this workspace.");
        ImGui::InputText("Source path", import_source_path, IM_ARRAYSIZE(import_source_path));
        ImGui::InputText("Model name (optional)", import_target_name, IM_ARRAYSIZE(import_target_name));
        static constexpr const char* kModes[] = {"Copy", "Replace", "Merge"};
        operationModeIndex = std::clamp(operationModeIndex, 0, 2);
        ImGui::SetNextItemWidth(220.0f);
        ImGui::Combo("Operation mode", &operationModeIndex, kModes, static_cast<int>(std::size(kModes)));
        const DataOperationMode selectedMode = static_cast<DataOperationMode>(operationModeIndex);
        const bool mergeSupported = false;

        const fs::path sourcePath = import_source_path;
        const bool sourceProvided = !sourcePath.empty();
        const bool sourceExists = sourceProvided && fs::exists(sourcePath);
        std::string targetName = sanitizeModelName(import_target_name);
        fs::path destinationPath;
        if (!targetName.empty()) {
            destinationPath = modelsRoot() / (targetName + ".simmodel");
        } else if (sourceProvided) {
            destinationPath = modelsRoot() / sourcePath.filename();
            if (destinationPath.extension() != ".simmodel") {
                destinationPath += ".simmodel";
            }
        }

        const bool destinationExists = !destinationPath.empty() && fs::exists(destinationPath);
        const bool dryRunValid = sourceExists && !destinationPath.empty() && (selectedMode != DataOperationMode::Merge || mergeSupported);

        ImGui::Spacing();
        ImGui::TextDisabled("Preflight summary");
        ImGui::TextWrapped("Source: %s", sourceProvided ? sourcePath.string().c_str() : "<missing>");
        ImGui::TextWrapped("Target: %s", destinationPath.empty() ? "<not resolved>" : destinationPath.string().c_str());
        ImGui::TextWrapped("Impact: %s", dataOperationOverwriteImpact(selectedMode, destinationExists).c_str());
        ImGui::TextWrapped("Mode behavior: %s", dataOperationModeBehavior(selectedMode));

        if (!dryRunValid) {
            if (!sourceExists) {
                ImGui::TextColored(ImVec4(0.95f, 0.55f, 0.45f, 1.0f), "Dry-run validation: failed (source is missing or inaccessible).");
            } else if (destinationPath.empty()) {
                ImGui::TextColored(ImVec4(0.95f, 0.55f, 0.45f, 1.0f), "Dry-run validation: failed (target path could not be resolved).");
            } else {
                ImGui::TextColored(ImVec4(0.95f, 0.55f, 0.45f, 1.0f), "Dry-run validation: failed (merge mode is not supported for model import).");
            }
        } else {
            ImGui::TextColored(ImVec4(0.58f, 0.88f, 0.62f, 1.0f), "Dry-run validation: passed");
        }

        if (!dryRunValid) {
            ImGui::BeginDisabled();
        }
        if (ImGui::Button("Import")) {
            std::string importedPath;
            std::string error;
            const bool replaceExisting = selectedMode == DataOperationMode::Replace;
            if (runImportWithConflictHandling(sourcePath, destinationPath, replaceExisting, importedPath, error)) {
                refreshModelList();
                std::memset(import_source_path, 0, sizeof(import_source_path));
                std::memset(import_target_name, 0, sizeof(import_target_name));
                std::memset(pending_import_destination, 0, sizeof(pending_import_destination));
                show_import_conflict_dialog = false;
                show_import_dialog = false;
                recordDataOperationReceipt(
                    "Model import",
                    selectedMode,
                    "Import committed successfully.",
                    "If this is not the expected package, delete the imported model from the Model Selector.",
                    "destination=" + importedPath);
                ImGui::CloseCurrentPopup();
                if (on_model_created) {
                    on_model_created(importedPath);
                }
            } else {
                recordDataOperationReceipt(
                    "Model import",
                    selectedMode,
                    "Import failed during commit.",
                    "Re-run preflight and verify source and destination paths.",
                    error);
            }
        }
        if (!dryRunValid) {
            ImGui::EndDisabled();
        }

        if (show_import_conflict_dialog) {
            ImGui::SetNextWindowSize(ImVec2(760.0f, 180.0f), ImGuiCond_Appearing);
            if (ImGui::BeginPopupModal("Import Conflict", &show_import_conflict_dialog, ImGuiWindowFlags_AlwaysAutoResize)) {
                ImGui::TextWrapped("A model already exists at the destination path:");
                ImGui::TextDisabled("%s", pending_import_destination[0] != '\0' ? pending_import_destination : "<unknown>");
                ImGui::Spacing();
                ImGui::Checkbox("Replace existing destination", &import_replace_existing);
                ImGui::TextDisabled("When disabled, import will create a unique copy suffix (_copy, _copy_2, ...). ");

                if (ImGui::Button("Continue import")) {
                    const fs::path source = import_source_path;
                    const fs::path destination = pending_import_destination;
                    std::string importedPath;
                    std::string error;
                    if (runImportWithConflictHandling(source, destination, import_replace_existing, importedPath, error)) {
                    refreshModelList();
                    std::memset(import_source_path, 0, sizeof(import_source_path));
                    std::memset(import_target_name, 0, sizeof(import_target_name));
                    std::memset(pending_import_destination, 0, sizeof(pending_import_destination));
                    import_replace_existing = false;
                    show_import_conflict_dialog = false;
                    show_import_dialog = false;
                    ImGui::CloseCurrentPopup();
                    if (on_model_created) {
                        on_model_created(importedPath);
                    }
                    } else {
                        (void)error;
                    }
                }
                ImGui::SameLine();
                if (ImGui::Button("Cancel")) {
                    std::memset(pending_import_destination, 0, sizeof(pending_import_destination));
                    import_replace_existing = false;
                    show_import_conflict_dialog = false;
                    ImGui::CloseCurrentPopup();
                }

                ImGui::EndPopup();
            }
        }

        if (ImGui::Button("Cancel")) {
            std::memset(pending_import_destination, 0, sizeof(pending_import_destination));
            import_replace_existing = false;
            show_import_conflict_dialog = false;
            show_import_dialog = false;
            ImGui::CloseCurrentPopup();
        }
        
        ImGui::EndPopup();
    }
}

bool ws::gui::ModelSelector::runImportWithConflictHandling(
    const std::filesystem::path& source,
    std::filesystem::path destination,
    const bool replaceExisting,
    std::string& importedPathOut,
    std::string& errorOut) {
    importedPathOut.clear();
    errorOut.clear();

    if (source.empty() || !fs::exists(source)) {
        errorOut = "Import source path is missing or unavailable.";
        return false;
    }

    std::error_code ec;
    const bool destinationExists = fs::exists(destination, ec);
    if (ec) {
        errorOut = "Failed to evaluate destination path state.";
        return false;
    }

    if (destinationExists && !replaceExisting) {
        destination = makeUniqueImportDestination(destination);
    }

    if (destinationExists && replaceExisting) {
        if (fs::is_directory(destination, ec)) {
            fs::remove_all(destination, ec);
        } else {
            fs::remove(destination, ec);
        }
        if (ec) {
            errorOut = "Failed to replace existing destination.";
            return false;
        }
    }

    if (fs::is_directory(source)) {
        fs::copy(source, destination, fs::copy_options::recursive | fs::copy_options::overwrite_existing, ec);
    } else {
        fs::copy_file(source, destination, fs::copy_options::overwrite_existing, ec);
    }

    if (ec) {
        errorOut = "Import copy failed.";
        return false;
    }

    importedPathOut = destination.string();
    return true;
}

void ws::gui::ModelSelector::renderRenameDialog() {
    ImGui::SetNextWindowSize(ImVec2(760.0f, 190.0f), ImGuiCond_Appearing);
    if (ImGui::BeginPopupModal("Rename Model", &show_rename_dialog)) {
        ImGui::TextWrapped("Rename the selected model. The on-disk folder and embedded metadata will be updated.");
        ImGui::InputText("New name", pending_rename_name, IM_ARRAYSIZE(pending_rename_name));

        if (ImGui::Button("Rename")) {
            if (pending_action_model_index >= 0 && pending_action_model_index < static_cast<int>(models.size())) {
                renameModel(models[pending_action_model_index], pending_rename_name);
                show_rename_dialog = false;
                ImGui::CloseCurrentPopup();
            }
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel")) {
            show_rename_dialog = false;
            ImGui::CloseCurrentPopup();
        }

        ImGui::EndPopup();
    }
}

void ws::gui::ModelSelector::renderExportDialog() {
    ImGui::SetNextWindowSize(ImVec2(840.0f, 220.0f), ImGuiCond_Appearing);
    if (ImGui::BeginPopupModal("Export Model", &show_export_dialog)) {
        static int operationModeIndex = static_cast<int>(DataOperationMode::Copy);
        ImGui::TextWrapped("Export the selected model to another .simmodel path. The destination will be overwritten if it already exists.");
        ImGui::InputText("Destination", pending_export_path, IM_ARRAYSIZE(pending_export_path));

        static constexpr const char* kModes[] = {"Copy", "Replace", "Merge"};
        operationModeIndex = std::clamp(operationModeIndex, 0, 2);
        ImGui::SetNextItemWidth(220.0f);
        ImGui::Combo("Operation mode", &operationModeIndex, kModes, static_cast<int>(std::size(kModes)));
        const DataOperationMode selectedMode = static_cast<DataOperationMode>(operationModeIndex);
        const bool mergeSupported = false;

        fs::path destinationPath = pending_export_path;
        if (!destinationPath.empty() && destinationPath.extension() != ".simmodel") {
            destinationPath += ".simmodel";
        }
        const bool destinationProvided = !destinationPath.empty();
        const bool destinationExists = destinationProvided && fs::exists(destinationPath);
        const bool dryRunValid = destinationProvided && (selectedMode != DataOperationMode::Merge || mergeSupported);

        ImGui::Spacing();
        ImGui::TextDisabled("Preflight summary");
        ImGui::TextWrapped("Source: %s",
            (pending_action_model_index >= 0 && pending_action_model_index < static_cast<int>(models.size()))
                ? models[static_cast<std::size_t>(pending_action_model_index)].path.c_str()
                : "<none selected>");
        ImGui::TextWrapped("Target: %s", destinationProvided ? destinationPath.string().c_str() : "<missing>");
        ImGui::TextWrapped("Impact: %s", dataOperationOverwriteImpact(selectedMode, destinationExists).c_str());
        ImGui::TextWrapped("Mode behavior: %s", dataOperationModeBehavior(selectedMode));
        if (!dryRunValid) {
            if (!destinationProvided) {
                ImGui::TextColored(ImVec4(0.95f, 0.55f, 0.45f, 1.0f), "Dry-run validation: failed (destination is required).");
            } else {
                ImGui::TextColored(ImVec4(0.95f, 0.55f, 0.45f, 1.0f), "Dry-run validation: failed (merge mode is not supported for model export).");
            }
        } else {
            ImGui::TextColored(ImVec4(0.58f, 0.88f, 0.62f, 1.0f), "Dry-run validation: passed");
        }

        if (!dryRunValid) {
            ImGui::BeginDisabled();
        }
        if (ImGui::Button("Export")) {
            if (pending_action_model_index >= 0 && pending_action_model_index < static_cast<int>(models.size())) {
                fs::path effectiveDestination = destinationPath;
                if (selectedMode == DataOperationMode::Copy && destinationExists) {
                    effectiveDestination = makeUniqueImportDestination(destinationPath);
                }

                exportModel(models[pending_action_model_index], effectiveDestination);
                std::string technical = "destination=" + effectiveDestination.string();
                if (selectedMode == DataOperationMode::Copy && destinationExists) {
                    technical += " | copy_suffix_applied=true";
                }
                recordDataOperationReceipt(
                    "Model export",
                    selectedMode,
                    "Export committed successfully.",
                    "If output is incorrect, re-export to a new path using Copy mode.",
                    technical);
                show_export_dialog = false;
                ImGui::CloseCurrentPopup();
            }
        }
        if (!dryRunValid) {
            ImGui::EndDisabled();
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel")) {
            show_export_dialog = false;
            ImGui::CloseCurrentPopup();
        }

        ImGui::EndPopup();
    }
}

void ws::gui::ModelSelector::renderDeleteConfirmDialog() {
    ImGui::SetNextWindowSize(ImVec2(420.0f, 130.0f), ImGuiCond_Appearing);
    if (ImGui::BeginPopupModal("Delete Model", &show_delete_confirm_dialog)) {
        if (pending_action_model_index >= 0 && pending_action_model_index < static_cast<int>(models.size())) {
            ImGui::TextWrapped("Delete '%s'? This cannot be undone.", models[pending_action_model_index].name.c_str());
        } else {
            ImGui::TextWrapped("Delete selected model?");
        }

        if (ImGui::Button("Delete")) {
            if (pending_action_model_index >= 0 && pending_action_model_index < static_cast<int>(models.size())) {
                deleteModel(models[pending_action_model_index]);
                selected_model_index = -1;
            }
            show_delete_confirm_dialog = false;
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel")) {
            show_delete_confirm_dialog = false;
            ImGui::CloseCurrentPopup();
        }

        ImGui::EndPopup();
    }
}

void ws::gui::ModelSelector::createModelFromTemplate(const std::string& template_name, const std::string& model_name) {
    const std::string safeName = sanitizeModelName(model_name);
    if (safeName.empty()) {
        return;
    }

    const fs::path model_path = modelsRoot() / (safeName + ".simmodel");
    std::error_code ec;
    if (fs::exists(model_path, ec)) {
        fs::remove_all(model_path, ec);
    }
    fs::create_directories(model_path, ec);

    const auto templateSource = resolveTemplateSource(template_name);
    if (templateSource.has_value()) {
        materializeTemplateToModelPath(*templateSource, model_path);
    }

    // Preserve template metadata when present; only update identity fields.
    json metadata = json::object();
    try {
        std::ifstream inMeta(model_path / "metadata.json");
        if (inMeta) {
            metadata = json::parse(inMeta);
        }
    } catch (...) {
        metadata = json::object();
    }
    metadata["name"] = safeName;
    if (!metadata.contains("id")) {
        metadata["id"] = safeName;
    }
    if (!metadata.contains("description")) {
        metadata["description"] = "Created from template: " + template_name;
    }
    std::ofstream mf(model_path / "metadata.json");
    mf << metadata.dump(2);
    mf.close();

    json version = json::object();
    try {
        std::ifstream inVersion(model_path / "version.json");
        if (inVersion) {
            version = json::parse(inVersion);
        }
    } catch (...) {
        version = json::object();
    }
    if (!version.contains("format_version")) {
        version["format_version"] = "1.0.0";
    }
    if (!version.contains("model_version")) {
        if (metadata.contains("version") && metadata["version"].is_string()) {
            version["model_version"] = metadata["version"].get<std::string>();
        } else {
            version["model_version"] = "1.0.0";
        }
    }
    if (!version.contains("minimum_engine_version")) {
        version["minimum_engine_version"] = kCurrentEngineVersion;
    }
    std::ofstream vf(model_path / "version.json");
    vf << version.dump(2);
    vf.close();

    json model;
    model["id"] = safeName;
    model["version"] = "1.0.0";
    model["grid"] = json::object();
    model["variables"] = json::array();
    model["stages"] = json::array();

    try {
        std::ifstream tmf(model_path / "model.json");
        if (tmf) {
            model = json::parse(tmf);
            model["id"] = safeName;
            model["version"] = "1.0.0";
        }
    } catch (...) {
        // Keep minimal fallback
    }

    std::ofstream mdf(model_path / "model.json");
    mdf << model.dump(2);
    mdf.close();

    if (!fs::exists(model_path / "logic.ir")) {
        std::ofstream ir(model_path / "logic.ir");
        ir << "// template\n";
    }
    
    refreshModelList();
    if (on_model_created) {
        on_model_created(model_path.string());
    }
}

void ws::gui::ModelSelector::duplicateModel(const ws::gui::ModelInfo& model) {
    int counter = 1;
    fs::path dst_path;
    do {
        const std::string suffix = (counter == 1) ? "_copy" : ("_copy_" + std::to_string(counter));
        dst_path = modelsRoot() / (model.name + suffix + ".simmodel");
        ++counter;
    } while (fs::exists(dst_path));

    const fs::path src_path = model.path;
    std::error_code ec;
    if (fs::is_directory(src_path, ec)) {
        fs::copy(src_path, dst_path, fs::copy_options::recursive | fs::copy_options::overwrite_existing, ec);
    } else {
        fs::copy_file(src_path, dst_path, fs::copy_options::overwrite_existing, ec);
    }
    if (ec) {
        return;
    }

    const std::string new_name = dst_path.stem().string();
    try {
        const fs::path metadataPath = dst_path / "metadata.json";
        if (fs::exists(metadataPath)) {
            std::ifstream in(metadataPath);
            json metadata = json::parse(in);
            metadata["name"] = new_name;
            if (metadata.contains("id") && metadata["id"].is_string()) {
                metadata["id"] = new_name;
            }
            std::ofstream out(metadataPath, std::ios::trunc);
            out << metadata.dump(2);
        }
    } catch (...) {
        // keep duplicate best effort
    }
    
    refreshModelList();
}

void ws::gui::ModelSelector::renameModel(const ws::gui::ModelInfo& model, const std::string& new_name) {
    const std::string safeName = sanitizeModelName(new_name);
    if (safeName.empty()) {
        return;
    }

    const fs::path source = model.path;
    const fs::path destination = modelsRoot() / (safeName + ".simmodel");
    if (source == destination) {
        return;
    }

    std::error_code ec;
    if (fs::exists(destination, ec)) {
        return;
    }

    fs::rename(source, destination, ec);
    if (ec) {
        return;
    }

    try {
        const fs::path meta = destination / "metadata.json";
        if (fs::exists(meta)) {
            std::ifstream in(meta);
            json mj = json::parse(in);
            mj["name"] = safeName;
            std::ofstream out(meta, std::ios::trunc);
            out << mj.dump(2);
        }
    } catch (...) {
    }

    try {
        const fs::path modelJson = destination / "model.json";
        if (fs::exists(modelJson)) {
            std::ifstream in(modelJson);
            json mj = json::parse(in);
            mj["id"] = safeName;
            std::ofstream out(modelJson, std::ios::trunc);
            out << mj.dump(2);
        }
    } catch (...) {
    }

    refreshModelList();
}

void ws::gui::ModelSelector::exportModel(const ws::gui::ModelInfo& model, const std::filesystem::path& destination) {
    if (destination.empty()) {
        return;
    }

    fs::path target = destination;
    if (target.extension() != ".simmodel") {
        target += ".simmodel";
    }

    std::error_code ec;
    if (fs::exists(target, ec)) {
        if (fs::is_directory(target, ec)) {
            fs::remove_all(target, ec);
        } else {
            fs::remove(target, ec);
        }
    }

    if (fs::is_directory(model.path, ec)) {
        fs::copy(model.path, target, fs::copy_options::recursive | fs::copy_options::overwrite_existing, ec);
    } else {
        fs::copy_file(model.path, target, fs::copy_options::overwrite_existing, ec);
    }
}

void ws::gui::ModelSelector::deleteModel(const ws::gui::ModelInfo& model) {
    std::error_code ec;
    if (fs::is_directory(model.path, ec)) {
        fs::remove_all(model.path, ec);
    } else {
        fs::remove(model.path, ec);
    }
    refreshModelList();
}

} // namespace ws::gui
