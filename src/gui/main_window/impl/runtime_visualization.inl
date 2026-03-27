#ifdef WS_MAIN_WINDOW_IMPL_CLASS_CONTEXT

// Field history for sparklines (rolling average per named field)
struct FieldHistoryEntry {
    float avg = 0.0f;
    float minVal = 0.0f;
    float maxVal = 0.0f;
    std::uint64_t stepIndex = 0;
};

static constexpr std::size_t kHistoryCapacity = 512;

std::unordered_map<std::string, std::vector<FieldHistoryEntry>> fieldHistory_;
std::uint64_t lastHistoryStep_ = std::numeric_limits<std::uint64_t>::max();

void updateFieldHistory(const ws::StateStoreSnapshot& snapshot) {
    if (snapshot.header.stepIndex == lastHistoryStep_) return;
    lastHistoryStep_ = snapshot.header.stepIndex;

    for (const auto& field : snapshot.fields) {
        auto& hist = fieldHistory_[field.spec.name];
        const auto sum = app::summarizeField(field);

        FieldHistoryEntry entry;
        entry.avg       = static_cast<float>(sum.average);
        entry.minVal    = sum.minValue;
        entry.maxVal    = sum.maxValue;
        entry.stepIndex = snapshot.header.stepIndex;

        hist.push_back(entry);
        if (hist.size() > kHistoryCapacity)
            hist.erase(hist.begin());
    }
}

// Draw a compact sparkline using ImDrawList
static void drawSparkline(ImDrawList* dl, const ImVec2 pos, const float w, const float h,
                          const std::vector<FieldHistoryEntry>& hist,
                          const ImU32 lineColor, const ImU32 rangeColor) {
    if (hist.size() < 2) return;

    // Find global min/max for normalization
    float gMin =  std::numeric_limits<float>::infinity();
    float gMax = -std::numeric_limits<float>::infinity();
    for (const auto& e : hist) {
        gMin = std::min(gMin, e.minVal);
        gMax = std::max(gMax, e.maxVal);
    }
    if (!std::isfinite(gMin) || !std::isfinite(gMax) || gMax - gMin < 1e-9f) return;
    const float range = gMax - gMin;

    // Range band (fill between min and max)
    for (std::size_t i = 0; i + 1 < hist.size(); ++i) {
        const float x0 = pos.x + w * static_cast<float>(i)     / static_cast<float>(hist.size() - 1);
        const float x1 = pos.x + w * static_cast<float>(i + 1) / static_cast<float>(hist.size() - 1);
        const float lo0 = pos.y + h * (1.0f - (hist[i].minVal     - gMin) / range);
        const float hi0 = pos.y + h * (1.0f - (hist[i].maxVal     - gMin) / range);
        const float lo1 = pos.y + h * (1.0f - (hist[i+1].minVal   - gMin) / range);
        const float hi1 = pos.y + h * (1.0f - (hist[i+1].maxVal   - gMin) / range);
        dl->AddQuadFilled(
            ImVec2(x0, std::clamp(hi0, pos.y, pos.y+h)),
            ImVec2(x1, std::clamp(hi1, pos.y, pos.y+h)),
            ImVec2(x1, std::clamp(lo1, pos.y, pos.y+h)),
            ImVec2(x0, std::clamp(lo0, pos.y, pos.y+h)),
            rangeColor);
    }

    // Average line
    for (std::size_t i = 0; i + 1 < hist.size(); ++i) {
        const float x0 = pos.x + w * static_cast<float>(i)     / static_cast<float>(hist.size() - 1);
        const float x1 = pos.x + w * static_cast<float>(i + 1) / static_cast<float>(hist.size() - 1);
        const float y0 = pos.y + h * (1.0f - (hist[i].avg     - gMin) / range);
        const float y1 = pos.y + h * (1.0f - (hist[i+1].avg   - gMin) / range);
        dl->AddLine(ImVec2(x0, std::clamp(y0, pos.y, pos.y+h)),
                    ImVec2(x1, std::clamp(y1, pos.y, pos.y+h)),
                    lineColor, 1.5f);
    }
}

// Field history panel - shown as a floating window when requested
bool showFieldHistoryWindow_ = false;

void drawFieldHistoryWindow() {
    if (!showFieldHistoryWindow_ || fieldHistory_.empty()) return;

    ImGui::SetNextWindowSize(ImVec2(520.0f, 420.0f), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowPos(ImVec2(530.0f, 10.0f), ImGuiCond_FirstUseEver);
    if (!ImGui::Begin("Field History##hist", &showFieldHistoryWindow_)) {
        ImGui::End(); return;
    }

    ImGui::TextDisabled("Rolling averages for the last %zu recorded steps.", kHistoryCapacity);
    ImGui::SameLine();
    if (SecondaryButton("Clear history", ImVec2(110.0f, 20.0f)))
        fieldHistory_.clear();

    ImGui::Separator();

    const float lineW = ImGui::GetContentRegionAvail().x;
    constexpr float kSparkH = 34.0f;
    constexpr float kSparkW = 160.0f;

    ImDrawList* dl = ImGui::GetWindowDrawList();

    for (auto& [name, hist] : fieldHistory_) {
        if (hist.empty()) continue;
        const auto& last = hist.back();

        ImGui::PushID(name.c_str());
        ImGui::BeginGroup();

        // Name + latest stats
        ImGui::TextColored(ImVec4(0.7f, 0.9f, 1.0f, 1.0f), "%-22s", name.c_str());
        ImGui::SameLine(0, 8);
        ImGui::TextDisabled("avg=%.4f  min=%.4f  max=%.4f  step=%llu",
            last.avg, last.minVal, last.maxVal, (unsigned long long)last.stepIndex);

        // Sparkline
        const ImVec2 sparkPos = ImGui::GetCursorScreenPos();
        dl->AddRectFilled(sparkPos,
            ImVec2(sparkPos.x + kSparkW, sparkPos.y + kSparkH),
            IM_COL32(14, 16, 28, 200), 2.0f);
        dl->AddRect(sparkPos,
            ImVec2(sparkPos.x + kSparkW, sparkPos.y + kSparkH),
            IM_COL32(55, 65, 90, 255), 2.0f);
        drawSparkline(dl, sparkPos, kSparkW, kSparkH, hist,
            IM_COL32(100, 200, 255, 230),
            IM_COL32(40, 80, 120, 80));
        ImGui::Dummy(ImVec2(kSparkW, kSparkH));
        if (ImGui::IsItemHovered()) {
            // Show value at hover position
            const float mx = ImGui::GetIO().MousePos.x - sparkPos.x;
            const float frac = std::clamp(mx / kSparkW, 0.0f, 1.0f);
            const std::size_t idx = static_cast<std::size_t>(frac * static_cast<float>(hist.size() - 1));
            if (idx < hist.size()) {
                ImGui::SetTooltip("Step %llu\navg=%.5f\nmin=%.5f\nmax=%.5f",
                    (unsigned long long)hist[idx].stepIndex,
                    hist[idx].avg, hist[idx].minVal, hist[idx].maxVal);
            }
        }

        ImGui::EndGroup();
        ImGui::Separator();
        ImGui::PopID();
    }

    ImGui::End();
}

// Texture management
void destroyRasterTexture(RasterTexture& texture) {
    if (texture.id != 0) { glDeleteTextures(1, &texture.id); texture.id = 0; }
    texture.width = texture.height = 0;
}

void destroyRasterResources() {
    for (auto& r : viewportRasters_) destroyRasterTexture(r);
    destroyRasterTexture(wizardPreviewTexture_);
    wizardPreviewPixels_.clear();
    wizardPreviewHash_ = 0;
    for (auto& c : renderCaches_) c.valid = false;
    snapshotDisplayCache_.clear();
    snapshotDisplayCacheGeneration_ = -1;
    fieldHistory_.clear();
}

void uploadRasterTexture(RasterTexture& tex, int w, int h,
                         const std::vector<std::uint8_t>& rgba) {
    if (w <= 0 || h <= 0 || rgba.empty()) { destroyRasterTexture(tex); return; }
    if (tex.id == 0) glGenTextures(1, &tex.id);
    tex.width = w; tex.height = h;
    glBindTexture(GL_TEXTURE_2D, tex.id);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, rgba.data());
    glBindTexture(GL_TEXTURE_2D, 0);
}

// Render config hashing
[[nodiscard]] std::uint64_t makeRenderConfigHash(const ViewportConfig& vp) const {
    std::uint64_t h = 1469598103934665603ull;
    h = hashCombine(h, static_cast<std::uint64_t>(vp.primaryFieldIndex));
    h = hashCombine(h, static_cast<std::uint64_t>(vp.displayType));
    h = hashCombine(h, static_cast<std::uint64_t>(vp.normalizationMode));
    h = hashCombine(h, static_cast<std::uint64_t>(vp.colorMapMode));
    h = hashCombine(h, static_cast<std::uint64_t>(viz_.showSparseOverlay));
    h = hashCombine(h, hashFloat(vp.fixedRangeMin));
    h = hashCombine(h, hashFloat(vp.fixedRangeMax));
    h = hashCombine(h, static_cast<std::uint64_t>(viz_.displayManager.autoWaterLevel));
    h = hashCombine(h, hashFloat(viz_.displayManager.waterLevel));
    h = hashCombine(h, hashFloat(viz_.displayManager.autoWaterQuantile));
    h = hashCombine(h, hashFloat(viz_.displayManager.lowlandThreshold));
    h = hashCombine(h, hashFloat(viz_.displayManager.highlandThreshold));
    h = hashCombine(h, hashFloat(viz_.displayManager.waterPresenceThreshold));
    h = hashCombine(h, hashFloat(visuals_.brightness));
    h = hashCombine(h, hashFloat(visuals_.contrast));
    h = hashCombine(h, hashFloat(visuals_.gamma));
    h = hashCombine(h, static_cast<std::uint64_t>(visuals_.invertColors));
    return h;
}

// View mapping
[[nodiscard]] ViewMapping makeViewMapping(const ImVec2 min, const ImVec2 max,
                                          const GridSpec grid) const {
    ViewMapping m;
    const float margin = 4.0f;
    const ImVec2 bMin(min.x + margin, min.y + margin);
    const ImVec2 bMax(max.x - margin, max.y - margin);
    const float bW = std::max(1.0f, bMax.x - bMin.x);
    const float bH = std::max(1.0f, bMax.y - bMin.y);
    const float aspect = static_cast<float>(grid.width) / static_cast<float>(std::max(1u, grid.height));

    float vW = bW, vH = bW / aspect;
    if (vH > bH) { vH = bH; vW = bH * aspect; }

    const ImVec2 ctr((bMin.x + bMax.x) * 0.5f, (bMin.y + bMax.y) * 0.5f);
    m.viewportMin = ImVec2(ctr.x - vW * 0.5f, ctr.y - vH * 0.5f);
    m.viewportMax = ImVec2(ctr.x + vW * 0.5f, ctr.y + vH * 0.5f);

    const float zoom = std::max(0.05f, visuals_.zoom);
    const float cW = vW * zoom, cH = vH * zoom;
    m.contentMin = ImVec2(ctr.x - cW * 0.5f + visuals_.panX,
                          ctr.y - cH * 0.5f + visuals_.panY);
    m.cellW = cW / static_cast<float>(std::max(1u, grid.width));
    m.cellH = cH / static_cast<float>(std::max(1u, grid.height));

    int stride = std::max(1, viz_.manualSamplingStride);
    if (viz_.adaptiveSampling) {
        const int sx = static_cast<int>(std::ceil(std::max(1.0f, 1.0f / std::max(0.0001f, m.cellW))));
        const int sy = static_cast<int>(std::ceil(std::max(1.0f, 1.0f / std::max(0.0001f, m.cellH))));
        stride = std::max(stride, std::max(sx, sy));
    }
    while ((static_cast<int>(grid.width) / stride) *
           (static_cast<int>(grid.height) / stride) > std::max(1000, viz_.maxRenderedCells))
        ++stride;
    m.samplingStride = std::max(1, stride);
    return m;
}

// Display range resolution
[[nodiscard]] std::pair<float,float> resolveDisplayRange(
    ViewportConfig& vp, const std::string& fieldName,
    const float localMin, const float localMax) {
    if (vp.normalizationMode == NormalizationMode::FixedManual) {
        const float lo = std::min(vp.fixedRangeMin, vp.fixedRangeMax);
        const float hi = std::max(vp.fixedRangeMin, vp.fixedRangeMax);
        return {lo, std::max(lo + 1e-6f, hi)};
    }
    if (vp.normalizationMode == NormalizationMode::PerFrameAuto) return {localMin, localMax};

    auto& rng = vp.stickyRanges[fieldName];
    rng.first  = std::min(rng.first,  localMin);
    rng.second = std::max(rng.second, localMax);
    if (std::abs(rng.second - rng.first) < 1e-6f) rng.second = rng.first + 1.0f;
    return rng;
}

// Color mapping
[[nodiscard]] ImU32 mapColor(const float t, const ColorMapMode mode) const {
    const float v = applyDisplayTransfer(t, visuals_.brightness, visuals_.contrast,
                                          visuals_.gamma, visuals_.invertColors);
    switch (mode) {
        case ColorMapMode::Grayscale: return colormapGrayscale(v);
        case ColorMapMode::Diverging: return colormapDiverging(v);
        case ColorMapMode::Water:     return colormapWater(v);
        default:                      return colormapTurboLike(v);
    }
}

[[nodiscard]] ImU32 mapDisplayTypeColor(const float value, const DisplayType type,
                                         const ColorMapMode palette) const {
    if (type == DisplayType::ScalarField) return mapColor(value, palette);

    if (type == DisplayType::SurfaceCategory) {
        switch (static_cast<int>(std::round(value))) {
            case 0: return IM_COL32(28,  75, 196, 255);  // deep water
            case 1: return IM_COL32(210,185, 118, 255);  // beach
            case 2: return IM_COL32( 78,155,  72, 255);  // inland
            case 3: return IM_COL32(118,128,  96, 255);  // upland
            case 4: return IM_COL32(235,235,235, 255);  // mountain
            default:return IM_COL32( 42, 92, 210, 255);
        }
    }
    if (type == DisplayType::RelativeElevation) {
        switch (static_cast<int>(std::round(value))) {
            case 0: return IM_COL32( 24, 50,130, 255);
            case 1: return IM_COL32( 40, 80,172, 255);
            case 2: return IM_COL32( 65,118,192, 255);
            case 3: return IM_COL32(105,162, 98, 255);
            case 4: return IM_COL32(152,132, 88, 255);
            default:return IM_COL32(238,238,238, 255);
        }
    }
    // WaterDepth
    const float d = std::clamp(value, 0.0f, 1.0f);
    if (d <= 0.001f) return IM_COL32(0,0,0,255);
    return colormapWater(applyDisplayTransfer(d, visuals_.brightness, visuals_.contrast,
                                               visuals_.gamma, visuals_.invertColors));
}

// Theme application
void applyTheme(const bool rebuildFonts) {
    ImGuiStyle& style = ImGui::GetStyle();
    ThemeBootstrap::applyBaseTheme(style, accessibility_.uiScale);
    ThemeBootstrap::applyAccessibility(ImGui::GetIO(), style, accessibility_);
    if (rebuildFonts) {
        ThemeBootstrap::configureFont(ImGui::GetIO(), accessibility_.fontSizePx);
        ImGui_ImplOpenGL3_DestroyFontsTexture();
        ImGui::GetIO().Fonts->Build();
        ImGui_ImplOpenGL3_CreateFontsTexture();
    }
}

// Viewport clear + background
void drawViewport() {
    float base = 0.05f * visuals_.brightness;
    float b = std::clamp(base * visuals_.contrast, 0.0f, 1.0f);
    if (visuals_.invertColors) b = 1.0f - b;
    glClearColor(std::clamp(b * 0.90f, 0.0f, 1.0f),
                 std::clamp(b * 0.95f, 0.0f, 1.0f),
                 std::clamp(b * 1.15f, 0.0f, 1.0f), 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);
}

// Main simulation canvas
void drawSimulationCanvas() {
    if (!viz_.hasCachedCheckpoint) return;

    const auto& snapshot = viz_.cachedCheckpoint.stateSnapshot;
    if (snapshot.grid.width == 0 || snapshot.grid.height == 0 || snapshot.fields.empty()) return;

    // Update field history for sparklines
    updateFieldHistory(snapshot);

    clampVisualizationIndices();

    ImDrawList* dl    = ImGui::GetBackgroundDrawList();
    const ImVec2 disp = ImGui::GetIO().DisplaySize;
    const ImVec2 cMin(0.0f, 0.0f), cMax(disp.x, disp.y);
    if (cMax.x <= cMin.x + 8.0f || cMax.y <= cMin.y + 8.0f) return;

    dl->AddRectFilled(cMin, cMax, IM_COL32(8, 10, 18, 255));

    int numVP = 1;
    if (viz_.layout == ScreenLayout::SplitLeftRight ||
        viz_.layout == ScreenLayout::SplitTopBottom) numVP = 2;
    else if (viz_.layout == ScreenLayout::Quad)      numVP = 4;

    // Display cache invalidation
    if (snapshotDisplayCacheGeneration_ != consumedSnapshotGeneration_) {
        snapshotDisplayCache_.clear();
        snapshotDisplayCacheGeneration_ = consumedSnapshotGeneration_;
    }

    using FC = std::chrono::steady_clock;
    const auto rebuildStart = FC::now();
    const bool autoRunning = viz_.autoRun && runtime_.isRunning() && !runtime_.isPaused();
    const bool uiHot = uiParameterInteractingThisFrame_ || (glfwGetTime() < uiInteractionHotUntilSec_);
    const int maxRebuilds = autoRunning ? (uiHot ? 2 : 1) : 4;
    const double budgetMs = autoRunning ? (uiHot ? 5.0 : 3.0) : 10.0;
    int rebuiltCount = 0;
    const int vpCount = std::max(1, numVP);
    const int startIdx = uiHot
        ? std::clamp(viz_.activeViewportEditor, 0, vpCount-1)
        : (autoRunning ? (nextViewportRebuildCursor_ % vpCount) : 0);

    for (int pass = 0; pass < numVP; ++pass) {
        const int i = (startIdx + pass) % vpCount;
        auto& vp    = viz_.viewports[i];
        auto& cache = renderCaches_[i];
        const std::uint64_t rHash = makeRenderConfigHash(vp);
        const bool dirty = !cache.valid ||
                           cache.snapshotGeneration != consumedSnapshotGeneration_ ||
                           cache.configHash != rHash;
        if (!dirty) continue;
        if (rebuiltCount >= maxRebuilds) continue;
        const double elapsed = std::chrono::duration<double,std::milli>(
            FC::now() - rebuildStart).count();
        if (rebuiltCount > 0 && elapsed >= budgetMs) continue;

        // Build display key for caching
        std::uint64_t dKey = 1469598103934665603ull;
        dKey = hashCombine(dKey, static_cast<std::uint64_t>(vp.primaryFieldIndex));
        dKey = hashCombine(dKey, static_cast<std::uint64_t>(vp.displayType));
        dKey = hashCombine(dKey, static_cast<std::uint64_t>(viz_.showSparseOverlay));
        dKey = hashCombine(dKey, static_cast<std::uint64_t>(viz_.displayManager.autoWaterLevel));
        dKey = hashCombine(dKey, hashFloat(viz_.displayManager.waterLevel));
        dKey = hashCombine(dKey, hashFloat(viz_.displayManager.autoWaterQuantile));
        dKey = hashCombine(dKey, hashFloat(viz_.displayManager.lowlandThreshold));
        dKey = hashCombine(dKey, hashFloat(viz_.displayManager.highlandThreshold));
        dKey = hashCombine(dKey, hashFloat(viz_.displayManager.waterPresenceThreshold));

        auto it = snapshotDisplayCache_.find(dKey);
        if (it == snapshotDisplayCache_.end()) {
            DisplayBuffer buf = buildDisplayBufferFromSnapshot(
                snapshot, vp.primaryFieldIndex, vp.displayType,
                viz_.showSparseOverlay, viz_.displayManager);
            it = snapshotDisplayCache_.emplace(dKey, std::move(buf)).first;
        }
        const DisplayBuffer& db = it->second;
        float pMin = db.minValue, pMax = db.maxValue;
        if (vp.displayType == DisplayType::ScalarField) {
            const auto pr = resolveDisplayRange(vp, db.label, pMin, pMax);
            pMin = pr.first; pMax = pr.second;
        }
        rebuildRasterTexture(i, snapshot.grid, db.values, pMin, pMax, vp);
        cache.snapshotGeneration = consumedSnapshotGeneration_;
        cache.configHash   = rHash;
        cache.valid        = true;
        cache.primaryMin   = pMin;
        cache.primaryMax   = pMax;
        cache.primaryName  = db.label;
        ++rebuiltCount;
    }

    if (autoRunning) nextViewportRebuildCursor_ = (startIdx + 1) % vpCount;
    else             nextViewportRebuildCursor_ = 0;

    // Layout drawing
    const float midX = (cMin.x + cMax.x) * 0.5f;
    const float midY = (cMin.y + cMax.y) * 0.5f;

    auto drawPanel = [&](int i, ImVec2 pMin, ImVec2 pMax) {
        auto& vp = viz_.viewports[i];
        auto& c  = renderCaches_[i];
        drawRasterPanel(*dl, pMin, pMax, snapshot.grid,
            c.primaryName, viewportRasters_[i], c.primaryMin, c.primaryMax, vp);
        if (vp.showVectorField) drawVectorOverlay(*dl, pMin, pMax, snapshot.grid, vp);
    };

    if      (viz_.layout == ScreenLayout::Single)
        drawPanel(0, cMin, cMax);
    else if (viz_.layout == ScreenLayout::SplitLeftRight) {
        drawPanel(0, cMin, ImVec2(midX-3.0f, cMax.y));
        drawPanel(1, ImVec2(midX+3.0f, cMin.y), cMax);
    } else if (viz_.layout == ScreenLayout::SplitTopBottom) {
        drawPanel(0, cMin, ImVec2(cMax.x, midY-3.0f));
        drawPanel(1, ImVec2(cMin.x, midY+3.0f), cMax);
    } else {
        drawPanel(0, cMin,                      ImVec2(midX-2.0f, midY-2.0f));
        drawPanel(1, ImVec2(midX+2.0f, cMin.y), ImVec2(cMax.x, midY-2.0f));
        drawPanel(2, ImVec2(cMin.x, midY+2.0f), ImVec2(midX-2.0f, cMax.y));
        drawPanel(3, ImVec2(midX+2.0f, midY+2.0f), cMax);
    }

    // Field history window (floating)
    drawFieldHistoryWindow();

    // Keyboard shortcut to toggle field history window
    if (!ImGui::GetIO().WantCaptureKeyboard && ImGui::IsKeyPressed(ImGuiKey_H, false))
        showFieldHistoryWindow_ = !showFieldHistoryWindow_;
}

// Raster rebuild
void rebuildRasterTexture(int vi, const GridSpec grid,
                           const std::vector<float>& primary,
                           const float pMin, const float pMax,
                           const ViewportConfig& vp) {
    auto& raster = viewportRasters_[vi];
    auto& buf    = rasterBuffers_[vi];

    if (grid.width == 0 || grid.height == 0) { destroyRasterTexture(raster); return; }

    const int W = static_cast<int>(grid.width);
    const int H = static_cast<int>(grid.height);

    if (maxTextureSize_ <= 0) {
        GLint ms = 0; glGetIntegerv(GL_MAX_TEXTURE_SIZE, &ms);
        maxTextureSize_ = std::max(256, static_cast<int>(ms));
    }

    const std::size_t pixCount = static_cast<std::size_t>(W) * static_cast<std::size_t>(H);
    const int budget = std::max(4096, viz_.maxRenderedCells);
    int stride = 1;
    if ((int)pixCount > budget) {
        stride = static_cast<int>(std::ceil(
            std::sqrt(static_cast<double>(pixCount) / static_cast<double>(budget))));
    }

    int sW = std::max(1, (W + stride - 1) / stride);
    int sH = std::max(1, (H + stride - 1) / stride);
    const float aspect = static_cast<float>(W) / static_cast<float>(H);

    if (sW > maxTextureSize_ || sH > maxTextureSize_) {
        const float sc = std::max(
            static_cast<float>(sW) / maxTextureSize_,
            static_cast<float>(sH) / maxTextureSize_);
        sW = std::max(1, static_cast<int>(static_cast<float>(sW) / sc));
        sH = std::max(1, static_cast<int>(static_cast<float>(sH) / sc));
    }

    buf.assign(static_cast<std::size_t>(sW) * static_cast<std::size_t>(sH) * 4u, 0u);
    for (int sy = 0; sy < sH; ++sy) {
        const int gy = std::clamp(
            static_cast<int>((static_cast<double>(sy) / std::max(1, sH-1)) * (H-1)), 0, H-1);
        for (int sx = 0; sx < sW; ++sx) {
            const int gx = std::clamp(
                static_cast<int>((static_cast<double>(sx) / std::max(1, sW-1)) * (W-1)), 0, W-1);
            const std::size_t src = static_cast<std::size_t>(gy) * W + gx;
            const float val = src < primary.size() ? primary[src] : std::numeric_limits<float>::quiet_NaN();
            ImU32 color = IM_COL32(18,18,28,255);
            if (std::isfinite(val)) {
                const float t = std::clamp((val - pMin) / std::max(0.0001f, pMax - pMin), 0.0f, 1.0f);
                color = mapDisplayTypeColor(
                    (vp.displayType == DisplayType::ScalarField ||
                     vp.displayType == DisplayType::WaterDepth) ? t : val,
                    vp.displayType, vp.colorMapMode);
            }
            std::uint8_t r,g,b,a;
            unpackColor(color, r, g, b, a);
            const std::size_t dst = (static_cast<std::size_t>(sy) * sW + sx) * 4u;
            buf[dst+0]=r; buf[dst+1]=g; buf[dst+2]=b; buf[dst+3]=a;
        }
    }
    uploadRasterTexture(raster, sW, sH, buf);
}

// Raster panel drawing
void drawRasterPanel(ImDrawList& dl, const ImVec2 min, const ImVec2 max,
                     const GridSpec grid, const std::string& title,
                     const RasterTexture& tex, const float minV, const float maxV,
                     const ViewportConfig& vp) const {
    const ViewMapping m = makeViewMapping(min, max, grid);
    const float pw = m.viewportMax.x - m.viewportMin.x;
    const float ph = m.viewportMax.y - m.viewportMin.y;
    if (pw <= 4.0f || ph <= 4.0f) return;

    dl.AddRectFilled(m.viewportMin, m.viewportMax, IM_COL32(12,14,22,255), 2.0f);
    dl.PushClipRect(m.viewportMin, m.viewportMax, true);

    if (tex.id != 0) {
        const ImVec2 cMax(
            m.contentMin.x + m.cellW * static_cast<float>(grid.width),
            m.contentMin.y + m.cellH * static_cast<float>(grid.height));
        dl.AddImage(static_cast<ImTextureID>(tex.id),
            m.contentMin, cMax, ImVec2(0,0), ImVec2(1,1));
    }

    // Grid lines
    if (visuals_.showGrid && viz_.showCellGrid &&
        m.cellW * m.samplingStride >= 4.0f && m.cellH * m.samplingStride >= 4.0f) {
        const int alpha = static_cast<int>(std::clamp(visuals_.gridOpacity, 0.0f, 1.0f) * 180.0f);
        const float th  = std::max(0.5f, visuals_.gridLineThickness);
        const int strd  = std::max(1, m.samplingStride);
        for (std::uint32_t x = 0; x <= grid.width; x += strd) {
            const float px = m.contentMin.x + m.cellW * x;
            dl.AddLine({px, m.viewportMin.y}, {px, m.viewportMax.y},
                IM_COL32(26,30,40,alpha), th);
        }
        for (std::uint32_t y = 0; y <= grid.height; y += strd) {
            const float py = m.contentMin.y + m.cellH * y;
            dl.AddLine({m.viewportMin.x, py}, {m.viewportMax.x, py},
                IM_COL32(26,30,40,alpha), th);
        }
    }

    dl.PopClipRect();

    // Boundary
    if (visuals_.showBoundary) {
        const int alpha = static_cast<int>(std::clamp(visuals_.boundaryOpacity, 0.0f, 1.0f) * 255.0f);
        dl.AddRect(m.viewportMin, m.viewportMax,
            IM_COL32(90,105,140,alpha), 2.0f, 0, std::max(0.5f, visuals_.boundaryThickness));
    }

    // Title
    dl.AddText(ImVec2(min.x+8.0f, min.y+8.0f), IM_COL32(240,245,255,255), title.c_str());

    if (vp.showLegend) {
        char buf[128];
        std::snprintf(buf, sizeof(buf), "min=%.4g  max=%.4g", minV, maxV);
        dl.AddText(ImVec2(min.x+8.0f, min.y+26.0f), IM_COL32(200,215,240,230), buf);

        if (vp.showRangeDetails) {
            const char* mode =
                (vp.normalizationMode == NormalizationMode::PerFrameAuto)  ? "auto" :
                (vp.normalizationMode == NormalizationMode::StickyPerField) ? "sticky" : "fixed";
            dl.AddText(ImVec2(min.x+8.0f, min.y+44.0f), IM_COL32(165,185,225,200), mode);
        }

        // Sparkline overlay (bottom-right corner of viewport)
        const auto histIt = fieldHistory_.find(title);
        if (histIt != fieldHistory_.end() && histIt->second.size() >= 4) {
            const float sW = std::min(120.0f, pw * 0.25f);
            const float sH = 24.0f;
            const ImVec2 sPos(m.viewportMax.x - sW - 6.0f, m.viewportMax.y - sH - 6.0f);
            if (sPos.x > m.viewportMin.x + sW * 2 && sPos.y > m.viewportMin.y + sH * 2) {
                dl.AddRectFilled(sPos, ImVec2(sPos.x+sW, sPos.y+sH),
                    IM_COL32(12,14,24,180), 2.0f);
                drawSparkline(&dl, sPos, sW, sH, histIt->second,
                    IM_COL32(100,200,255,220), IM_COL32(40,80,120,60));
            }
        }
    }
}

// Vector overlay
void drawVectorOverlay(ImDrawList& dl, const ImVec2 min, const ImVec2 max,
                       const GridSpec grid, const ViewportConfig& vp) const {
    const auto& fields = viz_.cachedCheckpoint.stateSnapshot.fields;
    if (fields.empty()) return;
    if (vp.vectorXFieldIndex >= (int)fields.size() ||
        vp.vectorYFieldIndex >= (int)fields.size()) return;

    const auto xVals = mergedFieldValues(fields[vp.vectorXFieldIndex], viz_.showSparseOverlay);
    const auto yVals = mergedFieldValues(fields[vp.vectorYFieldIndex], viz_.showSparseOverlay);

    float xMin,xMax,yMin,yMax;
    minMaxFinite(xVals, xMin, xMax);
    minMaxFinite(yVals, yMin, yMax);

    const ViewMapping m = makeViewMapping(min, max, grid);
    const int stride = std::max(std::max(1, vp.vectorStride), m.samplingStride);

    dl.PushClipRect(m.viewportMin, m.viewportMax, true);

    for (std::uint32_t cy = 0; cy < grid.height; cy += stride) {
        for (std::uint32_t cx = 0; cx < grid.width; cx += stride) {
            const std::size_t idx = static_cast<std::size_t>(cy) * grid.width + cx;
            if (idx >= xVals.size() || idx >= yVals.size()) continue;
            if (!std::isfinite(xVals[idx]) || !std::isfinite(yVals[idx])) continue;

            const float nx = ((xVals[idx] - xMin) / std::max(0.0001f, xMax-xMin) - 0.5f) * 2.0f;
            const float ny = ((yVals[idx] - yMin) / std::max(0.0001f, yMax-yMin) - 0.5f) * 2.0f;
            const float mag = std::sqrt(nx*nx + ny*ny);

            const ImVec2 ctr(
                m.contentMin.x + (static_cast<float>(cx) + 0.5f) * m.cellW,
                m.contentMin.y + (static_cast<float>(cy) + 0.5f) * m.cellH);
            const float len = std::min(m.cellW, m.cellH) * stride * vp.vectorScale;
            const ImVec2 tip(ctr.x + nx * len, ctr.y + ny * len);

            // Color arrows by magnitude
            const int r = static_cast<int>(std::clamp(80.0f + 155.0f * mag, 80.0f, 235.0f));
            const int g = static_cast<int>(std::clamp(235.0f - 100.0f * mag, 100.0f, 235.0f));
            const ImU32 arrowColor = IM_COL32(r, g, 80, 210);

            dl.AddLine(ctr, tip, arrowColor, 1.5f);

            // Arrowhead
            if (mag > 0.05f && len > 4.0f) {
                const float ax = nx / std::max(0.001f, mag);
                const float ay = ny / std::max(0.001f, mag);
                const float headLen = std::min(len * 0.35f, 8.0f);
                const ImVec2 h1(tip.x - ax*headLen + ay*headLen*0.45f,
                                tip.y - ay*headLen - ax*headLen*0.45f);
                const ImVec2 h2(tip.x - ax*headLen - ay*headLen*0.45f,
                                tip.y - ay*headLen + ax*headLen*0.45f);
                dl.AddTriangleFilled(tip, h1, h2, arrowColor);
            }
        }
    }
    dl.PopClipRect();
}

// Index clamping
void clampVisualizationIndices() {
    if (viz_.fieldNames.empty()) return;
    const int maxIdx = static_cast<int>(viz_.fieldNames.size()) - 1;
    for (auto& vp : viz_.viewports) {
        vp.primaryFieldIndex  = std::clamp(vp.primaryFieldIndex,  0, maxIdx);
        vp.vectorXFieldIndex  = std::clamp(vp.vectorXFieldIndex,  0, maxIdx);
        vp.vectorYFieldIndex  = std::clamp(vp.vectorYFieldIndex,  0, maxIdx);
    }
}

// Field name refresh
void refreshFieldNames() {
    std::string msg;
    std::vector<std::string> names;
    if (runtime_.fieldNames(names, msg) && !names.empty()) {
        viz_.fieldNames = std::move(names);
        for (auto& vp : viz_.viewports) vp.stickyRanges.clear();
        clampVisualizationIndices();
    } else if (!msg.empty()) {
        std::snprintf(viz_.lastRuntimeError, sizeof(viz_.lastRuntimeError), "%s", msg.c_str());
    }
}

//
// Snapshot worker
//
void requestSnapshotRefresh() {
    snapshotRequestPending_.store(true);
    viz_.snapshotDirty = true;
}

void startSnapshotWorker() {
    stopSnapshotWorker();
    snapshotRefreshHzAtomic_.store(std::max(1.0f, viz_.snapshotRefreshHz));
    snapshotWorkerRunning_.store(true);
    snapshotRequestPending_.store(true);
    snapshotWorker_ = std::thread([this]{ snapshotWorkerLoop(); });
}

void stopSnapshotWorker() {
    snapshotWorkerRunning_.store(false);
    if (snapshotWorker_.joinable()) snapshotWorker_.join();
}

void snapshotWorkerLoop() {
    using Clk = std::chrono::steady_clock;
    auto nextCapture = Clk::now();

    while (snapshotWorkerRunning_.load()) {
        const float hz = std::max(1.0f, snapshotRefreshHzAtomic_.load());
        const auto period = std::chrono::duration_cast<Clk::duration>(
            std::chrono::duration<double>(1.0 / hz));

        const bool continuous = snapshotContinuousMode_.load();
        const auto now = Clk::now();
        const bool forced = snapshotRequestPending_.exchange(false);

        if (!forced && !continuous) {
            std::this_thread::sleep_for(std::chrono::milliseconds(8)); continue;
        }
        if (!forced && now < nextCapture) {
            std::this_thread::sleep_for(std::chrono::milliseconds(2)); continue;
        }

        RuntimeCheckpoint cp;
        std::string msg;
        const auto t0 = Clk::now();
        const bool ok = runtime_.captureCheckpoint(cp, msg);
        const float dur = static_cast<float>(
            std::chrono::duration<double,std::milli>(Clk::now()-t0).count());

        if (ok) {
            const int front = snapshotFrontIndex_.load();
            const int back  = 1 - front;
            { std::lock_guard<std::mutex> lk(snapshotBufferMutex_);
              snapshotBuffers_[back] = std::move(cp);
              snapshotBufferValid_[back] = true; }
            snapshotFrontIndex_.store(back);
            snapshotGeneration_.fetch_add(1);
            snapshotDurationMsAtomic_.store(dur);
            { std::lock_guard<std::mutex> lk(snapshotErrorMutex_); snapshotWorkerError_.clear(); }
        } else if (!msg.empty()) {
            std::lock_guard<std::mutex> lk(snapshotErrorMutex_);
            snapshotWorkerError_ = msg;
        }

        nextCapture = Clk::now() + period;
    }
}

void consumeSnapshotFromWorker() {
    snapshotRefreshHzAtomic_.store(std::max(1.0f, viz_.snapshotRefreshHz));

    const int gen = snapshotGeneration_.load();
    if (gen != consumedSnapshotGeneration_) {
        const int front = snapshotFrontIndex_.load();
        RuntimeCheckpoint cp;
        bool hasFrame = false;
        { std::lock_guard<std::mutex> lk(snapshotBufferMutex_);
          if (snapshotBufferValid_[front]) { cp = snapshotBuffers_[front]; hasFrame = true; } }
        if (hasFrame) {
            viz_.cachedCheckpoint     = std::move(cp);
            viz_.hasCachedCheckpoint  = true;
            viz_.snapshotDirty        = false;
            viz_.lastSnapshotTimeSec  = glfwGetTime();
            viz_.lastSnapshotDurationMs = snapshotDurationMsAtomic_.load();
            viz_.framesSinceSnapshot  = 0;
        }
        consumedSnapshotGeneration_ = gen;
    } else {
        ++viz_.framesSinceSnapshot;
    }

    std::string err;
    { std::lock_guard<std::mutex> lk(snapshotErrorMutex_); err = snapshotWorkerError_; }
    if (!err.empty())
        std::snprintf(viz_.lastRuntimeError, sizeof(viz_.lastRuntimeError), "%s", err.c_str());
    else
        viz_.lastRuntimeError[0] = '\0';
}

// Auto-run tick
void tickAutoRun(const float frameDt) {
    if (appState_ != AppState::Simulation) return;

    { std::lock_guard<std::mutex> lk(asyncStateMutex_);
      if (asyncErrorPending_) {
          viz_.autoRun = false;
          appendLog(asyncErrorMessage_.empty() ? "auto_run_failed" : asyncErrorMessage_);
          std::snprintf(viz_.lastRuntimeError, sizeof(viz_.lastRuntimeError),
              "%s", asyncErrorMessage_.c_str());
          asyncErrorPending_ = false;
      } }

    snapshotContinuousMode_.store(viz_.autoRun && runtime_.isRunning());

    if (!viz_.autoRun || !runtime_.isRunning() || runtime_.isPaused()) return;

    if (autoRunFuture_.valid() &&
        autoRunFuture_.wait_for(std::chrono::seconds(0)) != std::future_status::ready) return;

    if (uiParameterChangedThisFrame_) requestSnapshotRefresh();

    const bool uiHot = glfwGetTime() < uiInteractionHotUntilSec_;
    autoRunStepBudget_ += static_cast<double>(std::max(1, viz_.simulationTickHz)) * frameDt;
    const double cap = std::max(16.0, static_cast<double>(std::max(1, viz_.simulationTickHz)) * 2.0);
    autoRunStepBudget_ = std::min(autoRunStepBudget_, cap);
    const int maxBatch = uiHot ? 2 : 8;
    const int batch = std::min(maxBatch, static_cast<int>(std::floor(autoRunStepBudget_)));
    if (batch <= 0) return;
    autoRunStepBudget_ -= batch;

    const int stepsPerTick  = std::clamp(viz_.autoStepsPerFrame, 1, 512);
    const int maxDispatch   = uiHot ? 2048 : 16384;
    const int stepsToRun    = std::min(maxDispatch, stepsPerTick * batch);

    autoRunFuture_ = std::async(std::launch::async, [this, stepsToRun]() {
        std::string msg;
        if (!runtime_.step(static_cast<std::uint32_t>(stepsToRun), msg)) {
            std::lock_guard<std::mutex> lk(asyncStateMutex_);
            asyncErrorMessage_ = msg;
            asyncErrorPending_ = true;
        }
        requestSnapshotRefresh();
    });
}

#endif // WS_MAIN_WINDOW_IMPL_CLASS_CONTEXT
