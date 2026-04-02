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

struct HistoryBounds {
    float minValue = 0.0f;
    float maxValue = 1.0f;
};

static std::string toLowerCopy(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });
    return value;
}

static std::uint64_t fnv1a64(const std::string& value) {
    std::uint64_t hash = 1469598103934665603ull;
    for (unsigned char ch : value) {
        hash ^= static_cast<std::uint64_t>(ch);
        hash *= 1099511628211ull;
    }
    return hash;
}

static std::uint64_t displayTagSignature(
    const std::unordered_map<std::string, std::vector<std::string>>& fieldDisplayTags) {
    std::uint64_t hash = 1469598103934665603ull;
    const auto combine = [](std::uint64_t lhs, std::uint64_t rhs) {
        return lhs ^ (rhs + 0x9e3779b97f4a7c15ull + (lhs << 6u) + (lhs >> 2u));
    };
    std::vector<std::string> fieldNames;
    fieldNames.reserve(fieldDisplayTags.size());
    for (const auto& [fieldName, _] : fieldDisplayTags) {
        fieldNames.push_back(fieldName);
    }
    std::sort(fieldNames.begin(), fieldNames.end());
    for (const auto& fieldName : fieldNames) {
        hash = combine(hash, fnv1a64(fieldName));
        const auto it = fieldDisplayTags.find(fieldName);
        if (it == fieldDisplayTags.end()) {
            continue;
        }
        for (const auto& tag : it->second) {
            hash = combine(hash, fnv1a64(tag));
        }
    }
    return hash;
}

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

[[nodiscard]] static bool computeHistoryBounds(const std::vector<FieldHistoryEntry>& hist, HistoryBounds& bounds) {
    if (hist.size() < 2) return false;

    float gMin =  std::numeric_limits<float>::infinity();
    float gMax = -std::numeric_limits<float>::infinity();
    for (const auto& e : hist) {
        gMin = std::min(gMin, e.minVal);
        gMax = std::max(gMax, e.maxVal);
    }
    if (!std::isfinite(gMin) || !std::isfinite(gMax) || gMax - gMin < 1e-9f) return false;
    bounds.minValue = gMin;
    bounds.maxValue = gMax;
    return true;
}

// Draw a compact sparkline using ImDrawList
static void drawSparkline(ImDrawList* dl, const ImVec2 pos, const float w, const float h,
                          const std::vector<FieldHistoryEntry>& hist,
                          const ImU32 lineColor, const ImU32 rangeColor) {
    HistoryBounds bounds;
    if (!computeHistoryBounds(hist, bounds)) return;
    const float range = bounds.maxValue - bounds.minValue;

    // Range band (fill between min and max)
    for (std::size_t i = 0; i + 1 < hist.size(); ++i) {
        const float x0 = pos.x + w * static_cast<float>(i)     / static_cast<float>(hist.size() - 1);
        const float x1 = pos.x + w * static_cast<float>(i + 1) / static_cast<float>(hist.size() - 1);
        const float lo0 = pos.y + h * (1.0f - (hist[i].minVal     - bounds.minValue) / range);
        const float hi0 = pos.y + h * (1.0f - (hist[i].maxVal     - bounds.minValue) / range);
        const float lo1 = pos.y + h * (1.0f - (hist[i+1].minVal   - bounds.minValue) / range);
        const float hi1 = pos.y + h * (1.0f - (hist[i+1].maxVal   - bounds.minValue) / range);
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
        const float y0 = pos.y + h * (1.0f - (hist[i].avg     - bounds.minValue) / range);
        const float y1 = pos.y + h * (1.0f - (hist[i+1].avg   - bounds.minValue) / range);
        dl->AddLine(ImVec2(x0, std::clamp(y0, pos.y, pos.y+h)),
                    ImVec2(x1, std::clamp(y1, pos.y, pos.y+h)),
                    lineColor, 1.5f);
    }

    const FieldHistoryEntry& last = hist.back();
    const float dotY = pos.y + h * (1.0f - (last.avg - bounds.minValue) / range);
    dl->AddCircleFilled(ImVec2(pos.x + w, std::clamp(dotY, pos.y, pos.y + h)), 2.5f, lineColor);
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
    ImGui::TextDisabled("[range band] min/max envelope   [line] average   [dot] latest step");
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
        HistoryBounds bounds;
        const bool haveBounds = computeHistoryBounds(hist, bounds);
        drawSparkline(dl, sparkPos, kSparkW, kSparkH, hist,
            IM_COL32(100, 200, 255, 230),
            IM_COL32(40, 80, 120, 80));
        if (haveBounds) {
            char buf[32];
            std::snprintf(buf, sizeof(buf), "%.4g", bounds.maxValue);
            dl->AddText(ImVec2(sparkPos.x + kSparkW + 6.0f, sparkPos.y), IM_COL32(140,160,200,220), buf);
            std::snprintf(buf, sizeof(buf), "%.4g", bounds.minValue);
            dl->AddText(ImVec2(sparkPos.x + kSparkW + 6.0f, sparkPos.y + kSparkH - 12.0f), IM_COL32(140,160,200,220), buf);
            std::snprintf(buf, sizeof(buf), "avg %.4g", last.avg);
            dl->AddText(ImVec2(sparkPos.x + kSparkW + 6.0f, sparkPos.y + kSparkH * 0.45f - 6.0f), IM_COL32(100,200,255,220), buf);
        }
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

bool saveViewportScreenshot(const int viewportIndex, const std::string& outputPath) const {
    if (viewportIndex < 0 || viewportIndex >= static_cast<int>(rasterBuffers_.size())) {
        return false;
    }
    const auto& tex = viewportRasters_[static_cast<std::size_t>(viewportIndex)];
    const auto& rgba = rasterBuffers_[static_cast<std::size_t>(viewportIndex)];
    if (tex.width <= 0 || tex.height <= 0 || rgba.size() < static_cast<std::size_t>(tex.width * tex.height * 4)) {
        return false;
    }

    std::ofstream out(outputPath, std::ios::binary | std::ios::trunc);
    if (!out.is_open()) {
        return false;
    }

    out << "P6\n" << tex.width << " " << tex.height << "\n255\n";
    for (int y = 0; y < tex.height; ++y) {
        const int srcY = tex.height - 1 - y;
        for (int x = 0; x < tex.width; ++x) {
            const std::size_t idx = (static_cast<std::size_t>(srcY) * static_cast<std::size_t>(tex.width) + static_cast<std::size_t>(x)) * 4u;
            out.put(static_cast<char>(rgba[idx + 0u]));
            out.put(static_cast<char>(rgba[idx + 1u]));
            out.put(static_cast<char>(rgba[idx + 2u]));
        }
    }
    return static_cast<bool>(out);
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
    h = hashCombine(h, hashFloat(viz_.displayManager.shallowWaterDepth));
    h = hashCombine(h, hashFloat(viz_.displayManager.highMoistureThreshold));
    h = hashCombine(h, static_cast<std::uint64_t>(vp.showWindMagnitudeBackground));
    h = hashCombine(h, hashFloat(visuals_.brightness));
    h = hashCombine(h, hashFloat(visuals_.contrast));
    h = hashCombine(h, hashFloat(visuals_.gamma));
    h = hashCombine(h, static_cast<std::uint64_t>(visuals_.invertColors));
    return h;
}

// View mapping
[[nodiscard]] ViewMapping makeViewMapping(const ImVec2 min, const ImVec2 max,
                                          const GridSpec grid,
                                          const int viewportIndex) const {
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

    const auto& camera = viewportManager_.camera(static_cast<std::size_t>(std::clamp(viewportIndex, 0, 3)));
    const float zoom = std::max(0.05f, camera.zoom);
    const float cW = vW * zoom, cH = vH * zoom;
    m.contentMin = ImVec2(ctr.x - cW * 0.5f + camera.panX,
                          ctr.y - cH * 0.5f + camera.panY);
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
            case 0: return IM_COL32( 28,  75, 196, 255); // deep water
            case 1: return IM_COL32( 58, 122, 200, 255); // shallow water
            case 2: return IM_COL32(212, 192, 144, 255); // shoreline / wet
            case 3: return IM_COL32( 78, 155,  72, 255); // lowland
            case 4: return IM_COL32(122, 135,  96, 255); // inland
            case 5: return IM_COL32(136, 112,  96, 255); // highland
            case 6: return IM_COL32(235, 235, 235, 255); // mountain
            default:return IM_COL32( 42,  92, 210, 255);
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
    if (type == DisplayType::MoistureMap) {
        if (value < 0.2f) return IM_COL32(217, 194, 122, 255); // arid
        if (value < 0.4f) return IM_COL32(168, 184,  96, 255); // semi-arid
        if (value < 0.6f) return IM_COL32( 96, 160,  96, 255); // moderate
        if (value < 0.8f) return IM_COL32( 64, 144, 122, 255); // humid
        return IM_COL32( 40, 120,  88, 255); // wet / tropical
    }
    if (type == DisplayType::WindField) {
        const float t = std::clamp(value, 0.0f, 1.0f);
        const int r = static_cast<int>(228.0f + (34.0f - 228.0f) * t);
        const int g = static_cast<int>(234.0f + (76.0f - 234.0f) * t);
        const int b = static_cast<int>(242.0f + (138.0f - 242.0f) * t);
        return IM_COL32(r, g, b, 255);
    }
    // WaterDepth
    const float d = std::clamp(value, 0.0f, 1.0f);
    if (d <= 0.001f) return IM_COL32(0,0,0,255);
    return colormapWater(applyDisplayTransfer(d, visuals_.brightness, visuals_.contrast,
                                               visuals_.gamma, visuals_.invertColors));
}

void drawLegendBar(ImDrawList& dl, const ImVec2 pos, const float w, const float h,
                   const DisplayType type, const ColorMapMode palette,
                   const float minV, const float maxV, bool horizontal = true) const {
    if (!horizontal) {
        // Original vertical mode (for backward compatibility if needed)
        dl.AddRectFilled(pos, ImVec2(pos.x + w, pos.y + h), IM_COL32(10, 12, 20, 220), 3.0f);
        dl.AddRect(pos, ImVec2(pos.x + w, pos.y + h), IM_COL32(60, 70, 96, 255), 3.0f);
        auto drawLabel = [&](const float y, const char* text) {
            dl.AddText(ImVec2(pos.x - 4.0f, y), IM_COL32(205,215,235,230), text);
        };
        if (type == DisplayType::ScalarField || type == DisplayType::WaterDepth || type == DisplayType::WindField) {
            constexpr int kSteps = 24;
            for (int i = 0; i < kSteps; ++i) {
                const float t0 = static_cast<float>(i) / static_cast<float>(kSteps);
                const float t1 = static_cast<float>(i + 1) / static_cast<float>(kSteps);
                const float y0 = pos.y + h * (1.0f - t1);
                const float y1 = pos.y + h * (1.0f - t0);
                const ImU32 color = mapDisplayTypeColor(t0, type, palette);
                dl.AddRectFilled(ImVec2(pos.x + 1.0f, y0), ImVec2(pos.x + w - 1.0f, y1), color);
            }
            char buf[32];
            std::snprintf(buf, sizeof(buf), "%.3g", maxV);
            drawLabel(pos.y - 2.0f, buf);
            std::snprintf(buf, sizeof(buf), "%.3g", minV);
            drawLabel(pos.y + h - 12.0f, buf);
        }
        return;
    }

    // Horizontal mode - optimized for top display
    dl.AddRectFilled(pos, ImVec2(pos.x + w, pos.y + h), IM_COL32(10, 12, 20, 220), 2.0f);
    dl.AddRect(pos, ImVec2(pos.x + w, pos.y + h), IM_COL32(60, 70, 96, 200), 1.5f);

    if (type == DisplayType::ScalarField || type == DisplayType::WaterDepth || type == DisplayType::WindField) {
        constexpr int kSteps = 48;  // More steps for horizontal gives smoother gradient
        for (int i = 0; i < kSteps; ++i) {
            const float t0 = static_cast<float>(i) / static_cast<float>(kSteps);
            const float t1 = static_cast<float>(i + 1) / static_cast<float>(kSteps);
            const float x0 = pos.x + w * t0;
            const float x1 = pos.x + w * t1;
            const ImU32 color = mapDisplayTypeColor(t0, type, palette);
            dl.AddRectFilled(ImVec2(x0 + 1.0f, pos.y + 1.0f), ImVec2(x1 - 0.5f, pos.y + h - 1.0f), color);
        }
        return;
    }

    if (type == DisplayType::SurfaceCategory) {
        static constexpr std::array<const char*, 7> labels = {
            "Mountain", "Highland", "Inland", "Lowland", "Wet", "Shallow", "Deep"
        };
        const float boxW = w / static_cast<float>(labels.size());
        for (std::size_t i = 0; i < labels.size(); ++i) {
            const float x = pos.x + boxW * static_cast<float>(i);
            const int category = static_cast<int>(i);
            dl.AddRectFilled(ImVec2(x + 1.0f, pos.y + 1.0f), ImVec2(x + boxW - 1.0f, pos.y + h - 1.0f),
                             mapDisplayTypeColor(static_cast<float>(category), type, palette));
        }
        return;
    }

    if (type == DisplayType::MoistureMap) {
        static constexpr std::array<const char*, 5> labels = {
            "Wet", "Humid", "Moderate", "Semi-arid", "Arid"
        };
        static constexpr std::array<float, 5> values = {0.9f, 0.7f, 0.5f, 0.3f, 0.1f};
        const float boxW = w / static_cast<float>(labels.size());
        for (std::size_t i = 0; i < labels.size(); ++i) {
            const float x = pos.x + boxW * static_cast<float>(i);
            dl.AddRectFilled(ImVec2(x + 1.0f, pos.y + 1.0f), ImVec2(x + boxW - 1.0f, pos.y + h - 1.0f),
                             mapDisplayTypeColor(values[i], type, palette));
        }
        return;
    }

    if (type == DisplayType::RelativeElevation) {
        static constexpr std::array<const char*, 6> labels = {
            "High", "Upland", "Coast", "Shelf", "Shallow", "Deep"
        };
        const float boxW = w / static_cast<float>(labels.size());
        for (std::size_t i = 0; i < labels.size(); ++i) {
            const float x = pos.x + boxW * static_cast<float>(i);
            const int category = static_cast<int>(i);
            dl.AddRectFilled(ImVec2(x + 1.0f, pos.y + 1.0f), ImVec2(x + boxW - 1.0f, pos.y + h - 1.0f),
                             mapDisplayTypeColor(static_cast<float>(category), type, palette));
        }
    }
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
        dKey = hashCombine(dKey, hashFloat(viz_.displayManager.shallowWaterDepth));
        dKey = hashCombine(dKey, hashFloat(viz_.displayManager.highMoistureThreshold));
        dKey = hashCombine(dKey, static_cast<std::uint64_t>(vp.showWindMagnitudeBackground));
        dKey = hashCombine(dKey, displayTagSignature(fieldDisplayTags_));

        auto it = snapshotDisplayCache_.find(dKey);
        if (it == snapshotDisplayCache_.end()) {
            DisplayBuffer buf = buildDisplayBufferFromSnapshot(
                snapshot, vp.primaryFieldIndex, vp.displayType,
                viz_.showSparseOverlay, viz_.displayManager, fieldDisplayTags_);
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

    auto drawPanel = [&](int i, ImDrawList& windowDrawList, ImVec2 pMin, ImVec2 pMax) {
        auto& vp = viz_.viewports[i];
        auto& c  = renderCaches_[i];
        drawRasterPanel(i, windowDrawList, pMin, pMax, snapshot.grid,
            c.primaryName, viewportRasters_[i], c.primaryMin, c.primaryMax, vp);
        if (vp.displayType == DisplayType::WindField) drawWindFieldOverlay(windowDrawList, pMin, pMax, snapshot.grid, vp, i);
        else if (vp.showVectorField || vp.renderMode == ViewportRenderMode::Vector) drawVectorOverlay(windowDrawList, pMin, pMax, snapshot.grid, vp, i);
        if (vp.showContours || vp.renderMode == ViewportRenderMode::Contour) drawContourOverlay(windowDrawList, pMin, pMax, snapshot.grid, vp, i, c.primaryMin, c.primaryMax);
    };

    constexpr std::array<const char*, 4> kViewportWindowNames = {
        "Runtime View 1", "Runtime View 2", "Runtime View 3", "Runtime View 4"
    };
    for (int i = 0; i < 4; ++i) {
        ImGui::PushID(i);
        ImGui::Begin(kViewportWindowNames[static_cast<std::size_t>(i)]);
        const ImVec2 avail = ImGui::GetContentRegionAvail();
        const ImVec2 pMin = ImGui::GetCursorScreenPos();
        const ImVec2 pMax(pMin.x + std::max(1.0f, avail.x), pMin.y + std::max(1.0f, avail.y));
        if (i < numVP) {
            ImDrawList* windowDrawList = ImGui::GetWindowDrawList();
            drawPanel(i, *windowDrawList, pMin, pMax);
        } else {
            ImGui::TextDisabled("Viewport disabled by current layout setting.");
        }
        ImGui::End();
        ImGui::PopID();
    }

    for (int i = 0; i < numVP; ++i) {
        const auto req = viewportManager_.consumeScreenshotRequest(static_cast<std::size_t>(i));
        if (!req.pending || req.outputPath.empty()) {
            continue;
        }
        if (saveViewportScreenshot(i, req.outputPath)) {
            appendLog("viewport_screenshot_saved path=" + req.outputPath);
        } else {
            appendLog("viewport_screenshot_failed path=" + req.outputPath);
        }
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

    std::vector<float> sampledValues;
    sampledValues.assign(static_cast<std::size_t>(sW) * static_cast<std::size_t>(sH), std::numeric_limits<float>::quiet_NaN());
    for (int sy = 0; sy < sH; ++sy) {
        const int gy = std::clamp(
            static_cast<int>((static_cast<double>(sy) / std::max(1, sH-1)) * (H-1)), 0, H-1);
        for (int sx = 0; sx < sW; ++sx) {
            const int gx = std::clamp(
                static_cast<int>((static_cast<double>(sx) / std::max(1, sW-1)) * (W-1)), 0, W-1);
            const std::size_t src = static_cast<std::size_t>(gy) * W + gx;
            sampledValues[static_cast<std::size_t>(sy) * static_cast<std::size_t>(sW) + static_cast<std::size_t>(sx)] =
                (src < primary.size()) ? primary[src] : std::numeric_limits<float>::quiet_NaN();
        }
    }

    HeatmapRenderParams params{};
    params.minValue = pMin;
    params.maxValue = pMax;
    params.normalization = vp.heatmapNormalization;
    params.colorMap = vp.heatmapColorMap;
    params.powerExponent = vp.heatmapPowerExponent;
    params.quantileLow = vp.heatmapQuantileLow;
    params.quantileHigh = vp.heatmapQuantileHigh;

    buf = heatmapRenderer_.buildRgba(sampledValues,
                                     static_cast<std::uint32_t>(sW),
                                     static_cast<std::uint32_t>(sH),
                                     params);

    if (vp.displayType != DisplayType::ScalarField) {
        for (std::size_t i = 0; i < sampledValues.size(); ++i) {
            const float val = sampledValues[i];
            ImU32 color = IM_COL32(18,18,28,255);
            if (std::isfinite(val)) {
                if (vp.displayType == DisplayType::WindField && !vp.showWindMagnitudeBackground) {
                    color = IM_COL32(18,18,28,255);
                } else {
                    const float t = std::clamp((val - pMin) / std::max(0.0001f, pMax - pMin), 0.0f, 1.0f);
                    color = mapDisplayTypeColor(
                        (vp.displayType == DisplayType::WaterDepth) ? t : val,
                        vp.displayType, vp.colorMapMode);
                }
            }
            std::uint8_t r,g,b,a;
            unpackColor(color, r, g, b, a);
            const std::size_t dst = i * 4u;
            buf[dst+0]=r; buf[dst+1]=g; buf[dst+2]=b; buf[dst+3]=a;
        }
    }

    if (vp.customRuleEnabled && vp.renderMode == ViewportRenderMode::CustomRule) {
        const auto& rules = viewportRenderRules_[static_cast<std::size_t>(std::clamp(vi, 0, 3))];
        for (std::size_t i = 0; i < sampledValues.size(); ++i) {
            const float sample = sampledValues[i];
            if (!std::isfinite(sample)) {
                continue;
            }
            std::array<float, 4> base{
                static_cast<float>(buf[i * 4u + 0u]) / 255.0f,
                static_cast<float>(buf[i * 4u + 1u]) / 255.0f,
                static_cast<float>(buf[i * 4u + 2u]) / 255.0f,
                static_cast<float>(buf[i * 4u + 3u]) / 255.0f
            };
            const auto blended = evaluateRules(rules, sample, base);
            buf[i * 4u + 0u] = static_cast<std::uint8_t>(std::clamp(blended[0], 0.0f, 1.0f) * 255.0f);
            buf[i * 4u + 1u] = static_cast<std::uint8_t>(std::clamp(blended[1], 0.0f, 1.0f) * 255.0f);
            buf[i * 4u + 2u] = static_cast<std::uint8_t>(std::clamp(blended[2], 0.0f, 1.0f) * 255.0f);
            buf[i * 4u + 3u] = static_cast<std::uint8_t>(std::clamp(blended[3], 0.0f, 1.0f) * 255.0f);
        }
    }

    uploadRasterTexture(raster, sW, sH, buf);
}

// Raster panel drawing
void drawRasterPanel(const int viewportIndex, ImDrawList& dl, const ImVec2 min, const ImVec2 max,
                     const GridSpec grid, const std::string& title,
                     const RasterTexture& tex, const float minV, const float maxV,
                     const ViewportConfig& vp) const {
    const ViewMapping m = makeViewMapping(min, max, grid, viewportIndex);
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
        const char* mode =
            (vp.normalizationMode == NormalizationMode::PerFrameAuto)  ? "auto" :
            (vp.normalizationMode == NormalizationMode::StickyPerField) ? "sticky" : "fixed";
        
        // Info area layout: mode text on first line
        const float infoLeft = min.x + 8.0f;
        const float infoY = min.y + 26.0f;
        
        if (vp.showRangeDetails) {
            dl.AddText(ImVec2(infoLeft, infoY), IM_COL32(165,185,225,200), mode);
        }

        // Horizontal colorbar below the info text
        const float colorbarY = infoY + (vp.showRangeDetails ? 20.0f : 0.0f);
        const float colorbarW = std::min(200.0f, pw * 0.35f);
        const float colorbarH = 16.0f;
        const ImVec2 colorbarPos(infoLeft, colorbarY);
        
        drawLegendBar(dl, colorbarPos, colorbarW, colorbarH, vp.displayType, vp.colorMapMode, minV, maxV, true);

        // Min/max labels below colorbar, left and right aligned
        char minBuf[24], maxBuf[24];
        std::snprintf(minBuf, sizeof(minBuf), "%.3g", minV);
        std::snprintf(maxBuf, sizeof(maxBuf), "%.3g", maxV);
        const float labelY = colorbarY + colorbarH + 2.0f;
        dl.AddText(ImVec2(colorbarPos.x, labelY), IM_COL32(150,170,210,210), minBuf);
        dl.AddText(ImVec2(colorbarPos.x + colorbarW - 32.0f, labelY), IM_COL32(150,170,210,210), maxBuf);

        // History sparkline below colorbar and labels
        const auto histIt = fieldHistory_.find(title);
        if (histIt != fieldHistory_.end() && histIt->second.size() >= 4) {
            const float sW = std::min(200.0f, pw * 0.35f);
            const float sH = 18.0f;
            const ImVec2 sPos(infoLeft, labelY + 18.0f);
            if (sPos.x + sW < m.viewportMax.x - 8.0f) {
                dl.AddRectFilled(sPos, ImVec2(sPos.x+sW, sPos.y+sH), IM_COL32(12,14,24,185), 2.0f);
                drawSparkline(&dl, sPos, sW, sH, histIt->second,
                    IM_COL32(100,200,255,220), IM_COL32(40,80,120,60));
                dl.AddText(ImVec2(sPos.x + 4.0f, sPos.y + sH + 2.0f), IM_COL32(150,170,210,180), "history");
            }
        }
    }
}

[[nodiscard]] std::pair<int, int> findWindFieldIndices() const {
    const auto& fields = viz_.cachedCheckpoint.stateSnapshot.fields;
    int windUIdx = -1;
    int windVIdx = -1;
    for (int i = 0; i < static_cast<int>(fields.size()); ++i) {
        const std::string& name = fields[static_cast<std::size_t>(i)].spec.name;
        const auto tagIt = fieldDisplayTags_.find(name);
        if (tagIt != fieldDisplayTags_.end()) {
            for (const auto& tag : tagIt->second) {
                const std::string tagLower = toLowerCopy(tag);
                if (windUIdx < 0 && (tagLower == "vector_x" || tagLower == "wind_u" || tagLower == "wind_x")) {
                    windUIdx = i;
                }
                if (windVIdx < 0 && (tagLower == "vector_y" || tagLower == "wind_v" || tagLower == "wind_y")) {
                    windVIdx = i;
                }
            }
        }
    }
    return {windUIdx, windVIdx};
}

// Vector overlay
void drawVectorOverlay(ImDrawList& dl, const ImVec2 min, const ImVec2 max,
                       const GridSpec grid, const ViewportConfig& vp, const int viewportIndex) const {
    const auto& fields = viz_.cachedCheckpoint.stateSnapshot.fields;
    if (fields.empty()) return;
    if (vp.vectorXFieldIndex >= (int)fields.size() ||
        vp.vectorYFieldIndex >= (int)fields.size()) return;

    const auto xVals = mergedFieldValues(fields[vp.vectorXFieldIndex], viz_.showSparseOverlay);
    const auto yVals = mergedFieldValues(fields[vp.vectorYFieldIndex], viz_.showSparseOverlay);

    float xMin,xMax,yMin,yMax;
    minMaxFinite(xVals, xMin, xMax);
    minMaxFinite(yVals, yMin, yMax);

    const ViewMapping m = makeViewMapping(min, max, grid, viewportIndex);
    VectorRenderConfig config{};
    config.stride = std::max(std::max(1, vp.vectorStride), m.samplingStride);
    config.scale = vp.vectorScale;
    vectorRenderer_.draw(dl, m.viewportMin, m.viewportMax, m.contentMin, m.cellW, m.cellH,
                         grid, xVals, yVals, config);
}

void drawWindFieldOverlay(ImDrawList& dl, const ImVec2 min, const ImVec2 max,
                          const GridSpec grid, const ViewportConfig& vp, const int viewportIndex) const {
    const auto [windUIdx, windVIdx] = findWindFieldIndices();
    const auto& fields = viz_.cachedCheckpoint.stateSnapshot.fields;
    if (windUIdx < 0 || windVIdx < 0 ||
        windUIdx >= static_cast<int>(fields.size()) ||
        windVIdx >= static_cast<int>(fields.size())) return;

    const auto xVals = mergedFieldValues(fields[static_cast<std::size_t>(windUIdx)], viz_.showSparseOverlay);
    const auto yVals = mergedFieldValues(fields[static_cast<std::size_t>(windVIdx)], viz_.showSparseOverlay);
    const ViewMapping m = makeViewMapping(min, max, grid, viewportIndex);
    VectorRenderConfig config{};
    config.stride = std::max(std::max(1, vp.vectorStride), m.samplingStride);
    config.scale = vp.vectorScale;
    vectorRenderer_.draw(dl, m.viewportMin, m.viewportMax, m.contentMin, m.cellW, m.cellH,
                         grid, xVals, yVals, config);
}

void drawContourOverlay(ImDrawList& dl, const ImVec2 min, const ImVec2 max,
                        const GridSpec grid, const ViewportConfig& vp, const int viewportIndex,
                        const float minValue, const float maxValue) const {
    const auto& fields = viz_.cachedCheckpoint.stateSnapshot.fields;
    if (fields.empty()) {
        return;
    }
    if (vp.primaryFieldIndex < 0 || vp.primaryFieldIndex >= static_cast<int>(fields.size())) {
        return;
    }

    const auto values = mergedFieldValues(fields[static_cast<std::size_t>(vp.primaryFieldIndex)], viz_.showSparseOverlay);
    const ViewMapping m = makeViewMapping(min, max, grid, viewportIndex);
    ContourRenderConfig cfg{};
    cfg.interval = std::max(1e-4f, vp.contourInterval);
    cfg.maxLevels = std::max(2, vp.contourMaxLevels);
    contourRenderer_.draw(dl, m.viewportMin, m.viewportMax, m.contentMin, m.cellW, m.cellH,
                          grid, values, minValue, maxValue, cfg);
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
        std::string tagsMessage;
        if (!runtime_.fieldDisplayTags(fieldDisplayTags_, tagsMessage)) {
            fieldDisplayTags_.clear();
        }
        for (auto& vp : viz_.viewports) vp.stickyRanges.clear();
        clampVisualizationIndices();
    } else if (!msg.empty()) {
        std::snprintf(viz_.lastRuntimeError, sizeof(viz_.lastRuntimeError), "%s", msg.c_str());
    }
}

[[nodiscard]] float estimatedSimulationStepsPerSecond() const {
    const float batchMs = simulationLastBatchDurationMs_.load();
    const int batchSteps = simulationLastBatchSteps_.load();
    if (batchMs <= 0.01f || batchSteps <= 0) return 0.0f;
    return (static_cast<float>(batchSteps) * 1000.0f) / batchMs;
}

[[nodiscard]] static float displayRefreshFloorHzFor(const bool unlimitedSimSpeed) {
    return unlimitedSimSpeed ? 20.0f : 30.0f;
}

[[nodiscard]] float displayRefreshFloorHz() const {
    return displayRefreshFloorHzFor(viz_.unlimitedSimSpeed);
}

[[nodiscard]] static std::uint64_t displayRefreshLatencyCapMsFor(const bool unlimitedSimSpeed) {
    return static_cast<std::uint64_t>(std::llround(1000.0 / displayRefreshFloorHzFor(unlimitedSimSpeed)));
}

[[nodiscard]] float displayRefreshLatencyCapMs() const {
    return static_cast<float>(displayRefreshLatencyCapMsFor(viz_.unlimitedSimSpeed));
}

[[nodiscard]] static std::uint64_t monotonicNowMs() {
    return static_cast<std::uint64_t>(
        std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now().time_since_epoch()).count());
}

[[nodiscard]] float estimatedDisplayRefreshesPerSecond() const {
    const float stepsPerSecond = estimatedSimulationStepsPerSecond();
    if (stepsPerSecond <= 0.0f) return 0.0f;
    const float stepDrivenRefreshes =
        stepsPerSecond / static_cast<float>(std::max(1, viz_.displayRefreshEveryNSteps));
    return std::min(stepsPerSecond, std::max(stepDrivenRefreshes, displayRefreshFloorHz()));
}

// Simulation worker
void cancelPendingSimulationSteps() {
    simulationAutoRunEnabled_.store(false);
    simulationWakeCV_.notify_all();
    simulationWorkerBusy_.store(false);
}

void startSimulationWorker() {
    stopSimulationWorker();
    simulationAutoRunEnabled_.store(false);
    simulationWorkerBusy_.store(false);
    simulationDisplayRefreshEveryNSteps_.store(std::max(1, viz_.displayRefreshEveryNSteps));
    simulationUnlimitedSpeed_.store(viz_.unlimitedSimSpeed);
    simulationLastDisplayRequestMs_.store(0);
    simulationLastBatchDurationMs_.store(0.0f);
    simulationLastBatchSteps_.store(0);
    simulationThreadRunning_.store(true);
    simulationThread_ = std::thread([this] { simulationWorkerLoop(); });
}

void stopSimulationWorker() {
    simulationAutoRunEnabled_.store(false);
    simulationThreadRunning_.store(false);
    simulationWakeCV_.notify_all();
    if (simulationThread_.joinable()) simulationThread_.join();
    simulationWorkerBusy_.store(false);
}

void simulationWorkerLoop() {
    int adaptiveBatchSize = 1;
    int stepsUntilDisplayRefresh = std::max(1, simulationDisplayRefreshEveryNSteps_.load());

        while (true) {
            {
                std::unique_lock<std::mutex> lock(simulationWakeMutex_);
                simulationWakeCV_.wait(lock, [this] {
                    return !simulationThreadRunning_.load() || simulationAutoRunEnabled_.load();
                });
                if (!simulationThreadRunning_.load()) break;
            }

            adaptiveBatchSize = 1;
            stepsUntilDisplayRefresh = std::max(1, simulationDisplayRefreshEveryNSteps_.load());

            while (simulationThreadRunning_.load() && simulationAutoRunEnabled_.load()) {
                    if (!runtime_.isRunning() || runtime_.isPaused()) {
                        simulationAutoRunEnabled_.store(false);
                        break;
                    }

                    const int refreshInterval = std::max(1, simulationDisplayRefreshEveryNSteps_.load());
                    const bool unlimitedSpeed = simulationUnlimitedSpeed_.load();
                    const int maxBatch = unlimitedSpeed ? 128 : 16;
                    const int batchCap = std::max(1, std::min(refreshInterval, maxBatch));
                    const int stepsToRun = std::clamp(adaptiveBatchSize, 1, batchCap);

                    simulationWorkerBusy_.store(true);
                    const auto t0 = std::chrono::steady_clock::now();
                    std::string msg;
                    const bool ok = runtime_.step(static_cast<std::uint32_t>(stepsToRun), msg);
                    const float batchMs = static_cast<float>(
                        std::chrono::duration<double, std::milli>(
                            std::chrono::steady_clock::now() - t0).count());
                    simulationLastBatchDurationMs_.store(batchMs);
                    simulationLastBatchSteps_.store(stepsToRun);

                    if (!ok) {
                        std::lock_guard<std::mutex> lock(asyncStateMutex_);
                        asyncErrorMessage_ = msg;
                        asyncErrorPending_ = true;
                        simulationAutoRunEnabled_.store(false);
                        simulationWorkerBusy_.store(false);
                        break;
                    }

                    stepsUntilDisplayRefresh -= stepsToRun;
                    const std::uint64_t lastDisplayRequestMs = simulationLastDisplayRequestMs_.load();
                    const std::uint64_t elapsedSinceDisplayMs =
                        (lastDisplayRequestMs == 0) ? std::numeric_limits<std::uint64_t>::max()
                                                    : (monotonicNowMs() - lastDisplayRequestMs);
                    const bool dueBySteps = stepsUntilDisplayRefresh <= 0;
                    const bool dueByLatency =
                        !snapshotRequestPending_.load() &&
                        elapsedSinceDisplayMs >= displayRefreshLatencyCapMsFor(unlimitedSpeed);
                    if (dueBySteps || dueByLatency) {
                        requestSnapshotRefresh();
                        stepsUntilDisplayRefresh = refreshInterval;
                    }

                    if (batchMs < 2.0f && adaptiveBatchSize < batchCap) {
                        adaptiveBatchSize = std::min(batchCap, adaptiveBatchSize + 1);
                    } else if (batchMs > 8.0f && adaptiveBatchSize > 1) {
                        adaptiveBatchSize = std::max(1, adaptiveBatchSize - 1);
                    } else {
                        adaptiveBatchSize = std::clamp(adaptiveBatchSize, 1, batchCap);
                    }

                    simulationWorkerBusy_.store(false);

                    if (!simulationUnlimitedSpeed_.load()) {
                        std::this_thread::sleep_for(std::chrono::milliseconds(1));
                    }
            }

            simulationWorkerBusy_.store(false);
        }
}

// Snapshot worker
void requestSnapshotRefresh() {
    simulationLastDisplayRequestMs_.store(monotonicNowMs());
    snapshotRequestPending_.store(true);
    viz_.snapshotDirty = true;
    snapshotWakeCV_.notify_one();
}

void startSnapshotWorker() {
    stopSnapshotWorker();
    snapshotWorkerRunning_.store(true);
    snapshotRequestPending_.store(true);
    snapshotWakeCV_.notify_all();
    snapshotWorker_ = std::thread([this]{ snapshotWorkerLoop(); });
}

void stopSnapshotWorker() {
    snapshotWorkerRunning_.store(false);
    snapshotWakeCV_.notify_all();
    if (snapshotWorker_.joinable()) snapshotWorker_.join();
}

void snapshotWorkerLoop() {
    while (snapshotWorkerRunning_.load()) {
        {
            std::unique_lock<std::mutex> lk(snapshotWakeMutex_);
            snapshotWakeCV_.wait(lk, [this] {
                return !snapshotWorkerRunning_.load() || snapshotRequestPending_.load();
            });
            if (!snapshotWorkerRunning_.load()) {
                break;
            }
        }

        if (!snapshotRequestPending_.exchange(false)) {
            continue;
        }

        RuntimeCheckpoint cp;
        std::string msg;
        const auto t0 = std::chrono::steady_clock::now();
        const bool ok = runtime_.captureCheckpoint(cp, msg, false /* computeHash */);
        const float durMs = static_cast<float>(
            std::chrono::duration<double, std::milli>(
                std::chrono::steady_clock::now() - t0).count());

        if (ok) {
            const int front = snapshotFrontIndex_.load();
            const int back = 1 - front;
            {
                std::lock_guard<std::mutex> lk(snapshotBufferMutex_);
                snapshotBuffers_[back] = std::move(cp);
                snapshotBufferValid_[back] = true;
            }
            snapshotFrontIndex_.store(back);
            snapshotGeneration_.fetch_add(1);
            snapshotDurationMsAtomic_.store(durMs);
            std::lock_guard<std::mutex> lk(snapshotErrorMutex_);
            snapshotWorkerError_.clear();
        } else {
            std::lock_guard<std::mutex> lk(snapshotErrorMutex_);
            snapshotWorkerError_ = msg;
        }
    }
}

void consumeSnapshotFromWorker() {
    const int gen = snapshotGeneration_.load();
    if (gen != consumedSnapshotGeneration_) {
        const int front = snapshotFrontIndex_.load();
        RuntimeCheckpoint cp;
        bool hasFrame = false;
        {
            std::lock_guard<std::mutex> lk(snapshotBufferMutex_);
            if (snapshotBufferValid_[front]) {
                cp = snapshotBuffers_[front];
                hasFrame = true;
            }
        }

        if (hasFrame) {
            viz_.cachedCheckpoint = std::move(cp);
            viz_.hasCachedCheckpoint = true;
            viz_.snapshotDirty = false;
            viz_.lastSnapshotTimeSec = glfwGetTime();
            viz_.lastSnapshotDurationMs = snapshotDurationMsAtomic_.load();
            viz_.framesSinceSnapshot = 0;
        }
        consumedSnapshotGeneration_ = gen;
    } else {
        ++viz_.framesSinceSnapshot;
    }

    std::string err;
    {
        std::lock_guard<std::mutex> lk(snapshotErrorMutex_);
        err = snapshotWorkerError_;
    }
    if (!err.empty()) {
        std::snprintf(viz_.lastRuntimeError, sizeof(viz_.lastRuntimeError), "%s", err.c_str());
    } else {
        viz_.lastRuntimeError[0] = '\0';
    }
}

// Auto-run tick
void tickAutoRun() {
    if (appState_ != AppState::Simulation) {
        cancelPendingSimulationSteps();
        return;
    }

    { std::lock_guard<std::mutex> lk(asyncStateMutex_);
      if (asyncErrorPending_) {
          viz_.autoRun = false;
          appendLog(asyncErrorMessage_.empty() ? "auto_run_failed" : asyncErrorMessage_);
          std::snprintf(viz_.lastRuntimeError, sizeof(viz_.lastRuntimeError),
              "%s", asyncErrorMessage_.c_str());
          asyncErrorPending_ = false;
      } }

    simulationDisplayRefreshEveryNSteps_.store(std::max(1, viz_.displayRefreshEveryNSteps));
    simulationUnlimitedSpeed_.store(viz_.unlimitedSimSpeed);

    if (!viz_.autoRun || !runtime_.isRunning() || runtime_.isPaused()) {
        cancelPendingSimulationSteps();
        return;
    }

    if (uiParameterChangedThisFrame_) requestSnapshotRefresh();
    if (!simulationAutoRunEnabled_.exchange(true)) {
        simulationWakeCV_.notify_one();
    }
}

#endif // WS_MAIN_WINDOW_IMPL_CLASS_CONTEXT
