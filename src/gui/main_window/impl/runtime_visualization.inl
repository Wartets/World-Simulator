#ifdef WS_MAIN_WINDOW_IMPL_CLASS_CONTEXT

    void destroyRasterTexture(RasterTexture& texture) {
        if (texture.id != 0) {
            glDeleteTextures(1, &texture.id);
            texture.id = 0;
        }
        texture.width = 0;
        texture.height = 0;
    }

    void destroyRasterResources() {
        for (auto& raster : viewportRasters_) {
            destroyRasterTexture(raster);
        }
        destroyRasterTexture(wizardPreviewTexture_);
        wizardPreviewPixels_.clear();
        wizardPreviewHash_ = 0;
        for (auto& cache : renderCaches_) {
            cache.valid = false;
        }
        snapshotDisplayCache_.clear();
        snapshotDisplayCacheGeneration_ = -1;
    }

    void uploadRasterTexture(RasterTexture& texture, const int width, const int height, const std::vector<std::uint8_t>& rgba) {
        if (width <= 0 || height <= 0 || rgba.empty()) {
            destroyRasterTexture(texture);
            return;
        }

        if (texture.id == 0) {
            glGenTextures(1, &texture.id);
        }
        texture.width = width;
        texture.height = height;

        glBindTexture(GL_TEXTURE_2D, texture.id);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, rgba.data());
        glBindTexture(GL_TEXTURE_2D, 0);
    }

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

    [[nodiscard]] ViewMapping makeViewMapping(const ImVec2 min, const ImVec2 max, const GridSpec grid) const {
        ViewMapping mapping;

        const float margin = 4.0f;
        const ImVec2 boundedMin(min.x + margin, min.y + margin);
        const ImVec2 boundedMax(max.x - margin, max.y - margin);
        const float boundedW = std::max(1.0f, boundedMax.x - boundedMin.x);
        const float boundedH = std::max(1.0f, boundedMax.y - boundedMin.y);
        const float targetAspect = static_cast<float>(grid.width) / static_cast<float>(grid.height);

        float viewW = boundedW;
        float viewH = boundedW / targetAspect;
        if (viewH > boundedH) {
            viewH = boundedH;
            viewW = boundedH * targetAspect;
        }

        const ImVec2 viewCenter((boundedMin.x + boundedMax.x) * 0.5f, (boundedMin.y + boundedMax.y) * 0.5f);
        mapping.viewportMin = ImVec2(viewCenter.x - viewW * 0.5f, viewCenter.y - viewH * 0.5f);
        mapping.viewportMax = ImVec2(viewCenter.x + viewW * 0.5f, viewCenter.y + viewH * 0.5f);

        const float zoom = std::max(0.05f, visuals_.zoom);
        const float contentW = viewW * zoom;
        const float contentH = viewH * zoom;
        mapping.contentMin = ImVec2(
            viewCenter.x - contentW * 0.5f + visuals_.panX,
            viewCenter.y - contentH * 0.5f + visuals_.panY);

        mapping.cellW = contentW / static_cast<float>(grid.width);
        mapping.cellH = contentH / static_cast<float>(grid.height);

        int stride = std::max(1, viz_.manualSamplingStride);
        if (viz_.adaptiveSampling) {
            const int pixelStrideX = static_cast<int>(std::ceil(std::max(1.0f, 1.0f / std::max(0.0001f, mapping.cellW))));
            const int pixelStrideY = static_cast<int>(std::ceil(std::max(1.0f, 1.0f / std::max(0.0001f, mapping.cellH))));
            stride = std::max(stride, std::max(pixelStrideX, pixelStrideY));
        }

        while ((static_cast<int>(grid.width) / stride) * (static_cast<int>(grid.height) / stride) > std::max(1000, viz_.maxRenderedCells)) {
            ++stride;
        }
        mapping.samplingStride = std::max(1, stride);

        return mapping;
    }

    [[nodiscard]] std::pair<float, float> resolveDisplayRange(
        ViewportConfig& vp,
        const std::string& fieldName,
        const float localMin,
        const float localMax) {
        if (vp.normalizationMode == NormalizationMode::FixedManual) {
            const float lo = std::min(vp.fixedRangeMin, vp.fixedRangeMax);
            const float hi = std::max(vp.fixedRangeMin, vp.fixedRangeMax);
            return {lo, std::max(lo + 1e-6f, hi)};
        }

        if (vp.normalizationMode == NormalizationMode::PerFrameAuto) {
            return {localMin, localMax};
        }

        auto it = vp.stickyRanges.find(fieldName);
        if (it == vp.stickyRanges.end()) {
            vp.stickyRanges.emplace(fieldName, std::make_pair(localMin, localMax));
            return {localMin, localMax};
        }

        auto& range = it->second;
        range.first = std::min(range.first, localMin);
        range.second = std::max(range.second, localMax);
        if (std::abs(range.second - range.first) < 1e-6f) {
            range.second = range.first + 1.0f;
        }
        return range;
    }

    [[nodiscard]] ImU32 mapColor(const float normalizedValue, const ColorMapMode mode) const {
        const float t = applyDisplayTransfer(normalizedValue, visuals_.brightness, visuals_.contrast, visuals_.gamma, visuals_.invertColors);
        switch (mode) {
            case ColorMapMode::Grayscale: return colormapGrayscale(t);
            case ColorMapMode::Diverging: return colormapDiverging(t);
            case ColorMapMode::Water:     return colormapWater(t);
            case ColorMapMode::Turbo:
            default:                      return colormapTurboLike(t);
        }
    }

    [[nodiscard]] ImU32 mapDisplayTypeColor(const float value, const DisplayType type, const ColorMapMode scalarPalette) const {
        if (type == DisplayType::ScalarField) {
            return mapColor(value, scalarPalette);
        }

        if (type == DisplayType::SurfaceCategory) {
            const int cls = static_cast<int>(std::round(value));
            switch (cls) {
                case 0: return IM_COL32(32, 84, 186, 255);   // water
                case 1: return IM_COL32(206, 186, 128, 255); // beach/lowland
                case 2: return IM_COL32(92, 162, 84, 255);   // inland
                case 3: return IM_COL32(126, 136, 110, 255); // upland
                default: return IM_COL32(236, 236, 236, 255); // mountain
            }
        }

        if (type == DisplayType::RelativeElevation) {
            const int cls = static_cast<int>(std::round(value));
            switch (cls) {
                case 0: return IM_COL32(28, 58, 135, 255);
                case 1: return IM_COL32(46, 88, 180, 255);
                case 2: return IM_COL32(73, 125, 196, 255);
                case 3: return IM_COL32(112, 170, 104, 255);
                case 4: return IM_COL32(162, 140, 98, 255);
                default: return IM_COL32(242, 242, 242, 255);
            }
        }

        const float depth = std::clamp(value, 0.0f, 1.0f);
        if (depth <= 0.001f) {
            return IM_COL32(0, 0, 0, 255);
        }
        return colormapWater(applyDisplayTransfer(depth, visuals_.brightness, visuals_.contrast, visuals_.gamma, visuals_.invertColors));
    }

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

    void drawViewport() {
        float base = 0.05f * visuals_.brightness;
        float b = std::clamp(base * visuals_.contrast, 0.0f, 1.0f);
        if (visuals_.invertColors) {
            b = 1.0f - b;
        }

        const float r = std::clamp(b * 0.90f, 0.0f, 1.0f);
        const float g = std::clamp(b * 0.95f, 0.0f, 1.0f);
        const float c = std::clamp(b * 1.15f, 0.0f, 1.0f);
        glClearColor(r, g, c, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);
    }

    void drawSimulationCanvas() {
        if (!viz_.hasCachedCheckpoint) {
            return;
        }

        const auto& snapshot = viz_.cachedCheckpoint.stateSnapshot;
        if (snapshot.grid.width == 0 || snapshot.grid.height == 0 || snapshot.fields.empty()) {
            return;
        }

        clampVisualizationIndices();

        ImDrawList* dl = ImGui::GetBackgroundDrawList();
        const ImVec2 display = ImGui::GetIO().DisplaySize;
        const ImVec2 canvasMin(0.0f, 0.0f);
        const ImVec2 canvasMax(display.x, display.y);
        if (canvasMax.x <= canvasMin.x + 8.0f || canvasMax.y <= canvasMin.y + 8.0f) {
            return;
        }

        dl->AddRectFilled(canvasMin, canvasMax, IM_COL32(8, 10, 18, 255), 6.0f);
        const auto& fields = snapshot.fields;

        int numViewports = 1;
        if (viz_.layout == ScreenLayout::SplitLeftRight || viz_.layout == ScreenLayout::SplitTopBottom) {
            numViewports = 2;
        } else if (viz_.layout == ScreenLayout::Quad) {
            numViewports = 4;
        }

        if (snapshotDisplayCacheGeneration_ != consumedSnapshotGeneration_) {
            snapshotDisplayCache_.clear();
            snapshotDisplayCacheGeneration_ = consumedSnapshotGeneration_;
        }

        using frame_clock = std::chrono::steady_clock;
        const auto rebuildWindowStart = frame_clock::now();
        const bool isContinuousRunning = viz_.autoRun && runtime_.isRunning() && !runtime_.isPaused();
        const bool uiInteracting = uiParameterInteractingThisFrame_ || (glfwGetTime() < uiInteractionHotUntilSec_);
        const int maxRebuildsPerFrame = isContinuousRunning ? (uiInteracting ? 2 : 1) : 4;
        const double rebuildBudgetMs = isContinuousRunning ? (uiInteracting ? 4.5 : 2.5) : 9.0;
        int rebuildCount = 0;
        const int viewportCount = std::max(1, numViewports);
        const int startIndex = uiInteracting
            ? std::clamp(viz_.activeViewportEditor, 0, viewportCount - 1)
            : (isContinuousRunning ? (nextViewportRebuildCursor_ % viewportCount) : 0);

        for (int pass = 0; pass < numViewports; ++pass) {
            const int i = (startIndex + pass) % viewportCount;
            auto& vp = viz_.viewports[i];
            
            const std::uint64_t renderHash = makeRenderConfigHash(vp);
            auto& cache = renderCaches_[i];
            
            const bool cacheDirty = !cache.valid || cache.snapshotGeneration != consumedSnapshotGeneration_ || cache.configHash != renderHash;
            
            if (cacheDirty) {
                if (rebuildCount >= maxRebuildsPerFrame) {
                    continue;
                }

                const double elapsedMs = std::chrono::duration<double, std::milli>(frame_clock::now() - rebuildWindowStart).count();
                if (rebuildCount > 0 && elapsedMs >= rebuildBudgetMs) {
                    continue;
                }

                std::uint64_t displayKey = 1469598103934665603ull;
                displayKey = hashCombine(displayKey, static_cast<std::uint64_t>(vp.primaryFieldIndex));
                displayKey = hashCombine(displayKey, static_cast<std::uint64_t>(vp.displayType));
                displayKey = hashCombine(displayKey, static_cast<std::uint64_t>(viz_.showSparseOverlay));
                displayKey = hashCombine(displayKey, static_cast<std::uint64_t>(viz_.displayManager.autoWaterLevel));
                displayKey = hashCombine(displayKey, hashFloat(viz_.displayManager.waterLevel));
                displayKey = hashCombine(displayKey, hashFloat(viz_.displayManager.autoWaterQuantile));
                displayKey = hashCombine(displayKey, hashFloat(viz_.displayManager.lowlandThreshold));
                displayKey = hashCombine(displayKey, hashFloat(viz_.displayManager.highlandThreshold));
                displayKey = hashCombine(displayKey, hashFloat(viz_.displayManager.waterPresenceThreshold));

                auto it = snapshotDisplayCache_.find(displayKey);
                if (it == snapshotDisplayCache_.end()) {
                    DisplayBuffer computed = buildDisplayBufferFromSnapshot(
                        snapshot,
                        vp.primaryFieldIndex,
                        vp.displayType,
                        viz_.showSparseOverlay,
                        viz_.displayManager);
                    it = snapshotDisplayCache_.emplace(displayKey, std::move(computed)).first;
                }
                const DisplayBuffer& display = it->second;
                float pMin = display.minValue;
                float pMax = display.maxValue;
                if (vp.displayType == DisplayType::ScalarField) {
                    const auto pRange = resolveDisplayRange(vp, display.label, pMin, pMax);
                    pMin = pRange.first;
                    pMax = pRange.second;
                }
                
                rebuildRasterTexture(i, snapshot.grid, display.values, pMin, pMax, vp);
                
                cache.snapshotGeneration = consumedSnapshotGeneration_;
                cache.configHash = renderHash;
                cache.valid = true;
                cache.primaryMin = pMin;
                cache.primaryMax = pMax;
                cache.primaryName = display.label;
                ++rebuildCount;
            }
        }

        if (isContinuousRunning) {
            nextViewportRebuildCursor_ = (startIndex + 1) % viewportCount;
        } else {
            nextViewportRebuildCursor_ = 0;
        }

        // Layout evaluation
        const float midX = (canvasMin.x + canvasMax.x) * 0.5f;
        const float midY = (canvasMin.y + canvasMax.y) * 0.5f;

        auto drawPanel = [&](const int i, const ImVec2 pMin, const ImVec2 pMax) {
            const auto& cache = renderCaches_[i];
            auto& vp = viz_.viewports[i];
            drawRasterPanel(*dl, pMin, pMax, snapshot.grid, cache.primaryName, viewportRasters_[i], cache.primaryMin, cache.primaryMax, vp);
            if (vp.showVectorField) {
                drawVectorOverlay(*dl, pMin, pMax, snapshot.grid, vp);
            }
        };

        if (viz_.layout == ScreenLayout::Single) {
            drawPanel(0, canvasMin, canvasMax);
        } else if (viz_.layout == ScreenLayout::SplitLeftRight) {
            drawPanel(0, canvasMin, ImVec2(midX - 3.0f, canvasMax.y));
            drawPanel(1, ImVec2(midX + 3.0f, canvasMin.y), canvasMax);
        } else if (viz_.layout == ScreenLayout::SplitTopBottom) {
            drawPanel(0, canvasMin, ImVec2(canvasMax.x, midY - 3.0f));
            drawPanel(1, ImVec2(canvasMin.x, midY + 3.0f), canvasMax);
        } else if (viz_.layout == ScreenLayout::Quad) {
            drawPanel(0, canvasMin, ImVec2(midX - 2.0f, midY - 2.0f));
            drawPanel(1, ImVec2(midX + 2.0f, canvasMin.y), ImVec2(canvasMax.x, midY - 2.0f));
            drawPanel(2, ImVec2(canvasMin.x, midY + 2.0f), ImVec2(midX - 2.0f, canvasMax.y));
            drawPanel(3, ImVec2(midX + 2.0f, midY + 2.0f), canvasMax);
        }
    }

    void rebuildRasterTexture(
        const int viewportIndex,
        const GridSpec grid,
        const std::vector<float>& primary,
        const float pMin,
        const float pMax,
        const ViewportConfig& vp) {
        
        auto& raster = viewportRasters_[viewportIndex];
        auto& buffer = rasterBuffers_[viewportIndex];

        if (grid.width == 0 || grid.height == 0) {
            destroyRasterTexture(raster);
            return;
        }

        const int width = static_cast<int>(grid.width);
        const int height = static_cast<int>(grid.height);
        const std::size_t pixelCount = static_cast<std::size_t>(width) * static_cast<std::size_t>(height);

        if (maxTextureSize_ <= 0) {
            GLint maxSize = 0;
            glGetIntegerv(GL_MAX_TEXTURE_SIZE, &maxSize);
            maxTextureSize_ = std::max(256, static_cast<int>(maxSize));
        }

        const int safeCellBudget = std::max(4096, viz_.maxRenderedCells);
        int sampleStride = 1;
        if (pixelCount > static_cast<std::size_t>(safeCellBudget)) {
            const double ratio = std::sqrt(static_cast<double>(pixelCount) / static_cast<double>(safeCellBudget));
            sampleStride = std::max(1, static_cast<int>(std::ceil(ratio)));
        }

        int sampledWidth = std::max(1, static_cast<int>((grid.width + static_cast<std::uint32_t>(sampleStride) - 1u) / static_cast<std::uint32_t>(sampleStride)));
        int sampledHeight = std::max(1, static_cast<int>((grid.height + static_cast<std::uint32_t>(sampleStride) - 1u) / static_cast<std::uint32_t>(sampleStride)));

        const float aspect = static_cast<float>(std::max(1, width)) / static_cast<float>(std::max(1, height));
        if (sampledWidth > maxTextureSize_ || sampledHeight > maxTextureSize_) {
            const float widthScale = static_cast<float>(sampledWidth) / static_cast<float>(maxTextureSize_);
            const float heightScale = static_cast<float>(sampledHeight) / static_cast<float>(maxTextureSize_);
            const float scale = std::max(widthScale, heightScale);
            sampledWidth = std::max(1, static_cast<int>(std::floor(static_cast<float>(sampledWidth) / scale)));
            sampledHeight = std::max(1, static_cast<int>(std::floor(static_cast<float>(sampledHeight) / scale)));

            if (aspect >= 1.0f) {
                sampledHeight = std::max(1, static_cast<int>(std::round(static_cast<float>(sampledWidth) / aspect)));
            } else {
                sampledWidth = std::max(1, static_cast<int>(std::round(static_cast<float>(sampledHeight) * aspect)));
            }
        }
        
        const std::size_t sampledPixelCount = static_cast<std::size_t>(sampledWidth) * static_cast<std::size_t>(sampledHeight);
        buffer.assign(sampledPixelCount * 4u, 0u);
        for (int sy = 0; sy < sampledHeight; ++sy) {
            const int gy = std::clamp(
                static_cast<int>((static_cast<double>(sy) / static_cast<double>(std::max(1, sampledHeight - 1))) * static_cast<double>(std::max(0, height - 1))),
                0,
                std::max(0, height - 1));
            for (int sx = 0; sx < sampledWidth; ++sx) {
                const int gx = std::clamp(
                    static_cast<int>((static_cast<double>(sx) / static_cast<double>(std::max(1, sampledWidth - 1))) * static_cast<double>(std::max(0, width - 1))),
                    0,
                    std::max(0, width - 1));

                const std::size_t srcIdx = static_cast<std::size_t>(gy) * static_cast<std::size_t>(width) + static_cast<std::size_t>(gx);
                const float value = srcIdx < primary.size() ? primary[srcIdx] : std::numeric_limits<float>::quiet_NaN();
                ImU32 color = IM_COL32(18, 18, 28, 255);
                if (std::isfinite(value)) {
                    const float t = std::clamp((value - pMin) / (std::max(0.0001f, pMax - pMin)), 0.0f, 1.0f);
                    color = mapDisplayTypeColor(
                        (vp.displayType == DisplayType::ScalarField || vp.displayType == DisplayType::WaterDepth) ? t : value,
                        vp.displayType,
                        vp.colorMapMode);
                }
                std::uint8_t r = 0, g = 0, b = 0, a = 0;
                unpackColor(color, r, g, b, a);
                const std::size_t dstIdx = static_cast<std::size_t>(sy) * static_cast<std::size_t>(sampledWidth) + static_cast<std::size_t>(sx);
                const std::size_t o = dstIdx * 4u;
                buffer[o + 0] = r;
                buffer[o + 1] = g;
                buffer[o + 2] = b;
                buffer[o + 3] = a;
            }
        }
        uploadRasterTexture(raster, sampledWidth, sampledHeight, buffer);
    }

    void drawRasterPanel(
        ImDrawList& dl,
        const ImVec2 min,
        const ImVec2 max,
        const GridSpec grid,
        const std::string& title,
        const RasterTexture& texture,
        const float minV,
        const float maxV,
        const ViewportConfig& vp) const {
        const ViewMapping mapping = makeViewMapping(min, max, grid);
        const float width = mapping.viewportMax.x - mapping.viewportMin.x;
        const float height = mapping.viewportMax.y - mapping.viewportMin.y;
        if (width <= 4.0f || height <= 4.0f || grid.width == 0 || grid.height == 0) {
            return;
        }

        dl.AddRectFilled(mapping.viewportMin, mapping.viewportMax, IM_COL32(12, 14, 22, 255), 2.0f);
        dl.PushClipRect(mapping.viewportMin, mapping.viewportMax, true);

        if (texture.id != 0) {
            const ImVec2 contentMax(
                mapping.contentMin.x + mapping.cellW * static_cast<float>(grid.width),
                mapping.contentMin.y + mapping.cellH * static_cast<float>(grid.height));
            dl.AddImage(
                static_cast<ImTextureID>(texture.id),
                mapping.contentMin,
                contentMax,
                ImVec2(0.0f, 0.0f),
                ImVec2(1.0f, 1.0f));
        }

        const int stride = std::max(1, mapping.samplingStride);
        if (visuals_.showGrid && viz_.showCellGrid && mapping.cellW * static_cast<float>(stride) >= 4.0f && mapping.cellH * static_cast<float>(stride) >= 4.0f) {
            const int alpha = static_cast<int>(std::clamp(visuals_.gridOpacity, 0.0f, 1.0f) * 220.0f);
            const float thickness = std::max(0.5f, visuals_.gridLineThickness);
            for (std::uint32_t x = 0; x <= grid.width; x += static_cast<std::uint32_t>(stride)) {
                const float px = mapping.contentMin.x + mapping.cellW * static_cast<float>(x);
                dl.AddLine(ImVec2(px, mapping.viewportMin.y), ImVec2(px, mapping.viewportMax.y), IM_COL32(26, 30, 40, alpha), thickness);
            }
            for (std::uint32_t y = 0; y <= grid.height; y += static_cast<std::uint32_t>(stride)) {
                const float py = mapping.contentMin.y + mapping.cellH * static_cast<float>(y);
                dl.AddLine(ImVec2(mapping.viewportMin.x, py), ImVec2(mapping.viewportMax.x, py), IM_COL32(26, 30, 40, alpha), thickness);
            }
        }

        dl.PopClipRect();

        if (visuals_.showBoundary) {
            const int alpha = static_cast<int>(std::clamp(visuals_.boundaryOpacity, 0.0f, 1.0f) * 255.0f);       
            const float thickness = std::max(0.5f, visuals_.boundaryThickness);
            dl.AddRect(mapping.viewportMin, mapping.viewportMax, IM_COL32(90, 105, 140, alpha), 2.0f, 0, thickness);
        }
        dl.AddText(ImVec2(min.x + 8.0f, min.y + 8.0f), IM_COL32(240, 245, 255, 255), title.c_str());

        if (vp.showLegend) {
            const std::string legend = "min=" + std::to_string(minV) + "  max=" + std::to_string(maxV);
            dl.AddText(ImVec2(min.x + 8.0f, min.y + 26.0f), IM_COL32(210, 220, 245, 255), legend.c_str());
            if (vp.showRangeDetails) {
                const char* modeText =
                    (vp.normalizationMode == NormalizationMode::PerFrameAuto) ? "range=frame" :
                    (vp.normalizationMode == NormalizationMode::StickyPerField) ? "range=sticky" :
                    "range=fixed";
                dl.AddText(ImVec2(min.x + 8.0f, min.y + 44.0f), IM_COL32(180, 200, 240, 230), modeText);
            }
        }
    }

    void drawVectorOverlay(ImDrawList& dl, const ImVec2 min, const ImVec2 max, const GridSpec grid, const ViewportConfig& vp) const {      
        const auto& fields = viz_.cachedCheckpoint.stateSnapshot.fields;
        if (fields.empty()) {
            return;
        }

        const auto& xField = fields[static_cast<std::size_t>(vp.vectorXFieldIndex)];
        const auto& yField = fields[static_cast<std::size_t>(vp.vectorYFieldIndex)];
        std::vector<float> xValues = mergedFieldValues(xField, viz_.showSparseOverlay);
        std::vector<float> yValues = mergedFieldValues(yField, viz_.showSparseOverlay);

        float xMin = 0.0f;
        float xMax = 1.0f;
        float yMin = 0.0f;
        float yMax = 1.0f;
        minMaxFinite(xValues, xMin, xMax);
        minMaxFinite(yValues, yMin, yMax);

        const ViewMapping mapping = makeViewMapping(min, max, grid);
        const int stride = std::max(std::max(1, vp.vectorStride), mapping.samplingStride);

        dl.PushClipRect(mapping.viewportMin, mapping.viewportMax, true);

        for (std::uint32_t y = 0; y < grid.height; y += static_cast<std::uint32_t>(stride)) {
            for (std::uint32_t x = 0; x < grid.width; x += static_cast<std::uint32_t>(stride)) {
                const std::size_t idx = static_cast<std::size_t>(y) * static_cast<std::size_t>(grid.width) + static_cast<std::size_t>(x);
                if (idx >= xValues.size() || idx >= yValues.size()) {
                    continue;
                }
                if (!std::isfinite(xValues[idx]) || !std::isfinite(yValues[idx])) {
                    continue;
                }

                const float nx = ((xValues[idx] - xMin) / std::max(0.0001f, xMax - xMin) - 0.5f) * 2.0f;
                const float ny = ((yValues[idx] - yMin) / std::max(0.0001f, yMax - yMin) - 0.5f) * 2.0f;

                const ImVec2 center(
                    mapping.contentMin.x + (static_cast<float>(x) + 0.5f) * mapping.cellW,
                    mapping.contentMin.y + (static_cast<float>(y) + 0.5f) * mapping.cellH);
                const float len = std::min(mapping.cellW, mapping.cellH) * static_cast<float>(stride) * vp.vectorScale;
                const ImVec2 tip(center.x + nx * len, center.y + ny * len);
                dl.AddLine(center, tip, IM_COL32(235, 235, 120, 220), 1.4f);
            }
        }

        dl.PopClipRect();
    }

    void clampVisualizationIndices() {
        if (viz_.fieldNames.empty()) {
            return;
        }
        const int maxIndex = static_cast<int>(viz_.fieldNames.size()) - 1;
        for (auto& vp : viz_.viewports) {
            vp.primaryFieldIndex = std::clamp(vp.primaryFieldIndex, 0, maxIndex);
            vp.vectorXFieldIndex = std::clamp(vp.vectorXFieldIndex, 0, maxIndex);
            vp.vectorYFieldIndex = std::clamp(vp.vectorYFieldIndex, 0, maxIndex);
        }
    }

    void refreshFieldNames() {
        std::string message;
        std::vector<std::string> names;
        if (runtime_.fieldNames(names, message) && !names.empty()) {
            viz_.fieldNames = std::move(names);
            for (auto& vp : viz_.viewports) {
                vp.stickyRanges.clear();
            }
            clampVisualizationIndices();
        } else if (!message.empty()) {
            std::snprintf(viz_.lastRuntimeError, sizeof(viz_.lastRuntimeError), "%s", message.c_str());
        }
    }

    void requestSnapshotRefresh() {
        snapshotRequestPending_.store(true);
        viz_.snapshotDirty = true;
    }

    void startSnapshotWorker() {
        stopSnapshotWorker();

        snapshotRefreshHzAtomic_.store(std::max(1.0f, viz_.snapshotRefreshHz));
        snapshotWorkerRunning_.store(true);
        snapshotRequestPending_.store(true);
        snapshotWorker_ = std::thread([this]() { snapshotWorkerLoop(); });
    }

    void stopSnapshotWorker() {
        snapshotWorkerRunning_.store(false);
        if (snapshotWorker_.joinable()) {
            snapshotWorker_.join();
        }
    }

    void snapshotWorkerLoop() {
        using clock = std::chrono::steady_clock;
        auto nextCapture = clock::now();

        while (snapshotWorkerRunning_.load()) {
            const float hz = std::max(1.0f, snapshotRefreshHzAtomic_.load());
            const auto period = std::chrono::duration_cast<clock::duration>(
                std::chrono::duration<double>(1.0 / static_cast<double>(hz)));

            const bool continuous = snapshotContinuousMode_.load();
            const auto now = clock::now();
            const bool forced = snapshotRequestPending_.exchange(false);
            if (!forced && !continuous) {
                std::this_thread::sleep_for(std::chrono::milliseconds(8));
                continue;
            }
            if (!forced && now < nextCapture) {
                std::this_thread::sleep_for(std::chrono::milliseconds(2));
                continue;
            }

            RuntimeCheckpoint checkpoint;
            std::string message;
            const auto captureStart = clock::now();
            const bool ok = runtime_.captureCheckpoint(checkpoint, message);
            const float durationMs = static_cast<float>(std::chrono::duration<double, std::milli>(clock::now() - captureStart).count());

            if (ok) {
                const int currentFront = snapshotFrontIndex_.load();
                const int back = 1 - currentFront;
                {
                    const std::lock_guard<std::mutex> lock(snapshotBufferMutex_);
                    snapshotBuffers_[static_cast<std::size_t>(back)] = std::move(checkpoint);
                    snapshotBufferValid_[static_cast<std::size_t>(back)] = true;
                }
                snapshotFrontIndex_.store(back);
                snapshotGeneration_.fetch_add(1);
                snapshotDurationMsAtomic_.store(durationMs);
                {
                    const std::lock_guard<std::mutex> lock(snapshotErrorMutex_);
                    snapshotWorkerError_.clear();
                }
            } else if (!message.empty()) {
                const std::lock_guard<std::mutex> lock(snapshotErrorMutex_);
                snapshotWorkerError_ = message;
            }

            nextCapture = clock::now() + period;
        }
    }

    void consumeSnapshotFromWorker() {
        snapshotRefreshHzAtomic_.store(std::max(1.0f, viz_.snapshotRefreshHz));

        const int generation = snapshotGeneration_.load();
        if (generation == consumedSnapshotGeneration_) {
            ++viz_.framesSinceSnapshot;
        } else {
            const int front = snapshotFrontIndex_.load();
            RuntimeCheckpoint checkpoint;
            bool hasFrame = false;
            {
                const std::lock_guard<std::mutex> lock(snapshotBufferMutex_);
                if (snapshotBufferValid_[static_cast<std::size_t>(front)]) {
                    checkpoint = snapshotBuffers_[static_cast<std::size_t>(front)];
                    hasFrame = true;
                }
            }

            if (hasFrame) {
                viz_.cachedCheckpoint = std::move(checkpoint);
                viz_.hasCachedCheckpoint = true;
                viz_.snapshotDirty = false;
                viz_.lastSnapshotTimeSec = glfwGetTime();
                viz_.lastSnapshotDurationMs = snapshotDurationMsAtomic_.load();
                viz_.framesSinceSnapshot = 0;
            }

            consumedSnapshotGeneration_ = generation;
        }

        std::string error;
        {
            const std::lock_guard<std::mutex> lock(snapshotErrorMutex_);
            error = snapshotWorkerError_;
        }
        if (!error.empty()) {
            std::snprintf(viz_.lastRuntimeError, sizeof(viz_.lastRuntimeError), "%s", error.c_str());
        } else {
            viz_.lastRuntimeError[0] = '\0';
        }
    }

    void tickAutoRun(const float frameDt) {
        if (appState_ != AppState::Simulation) {
            return;
        }
        {
            const std::lock_guard<std::mutex> lock(asyncStateMutex_);
            if (asyncErrorPending_) {
                viz_.autoRun = false;
                appendLog(asyncErrorMessage_.empty() ? "auto_run_failed" : asyncErrorMessage_);
                std::snprintf(viz_.lastRuntimeError, sizeof(viz_.lastRuntimeError), "%s", asyncErrorMessage_.c_str());
                asyncErrorPending_ = false;
            }
        }

        snapshotContinuousMode_.store(viz_.autoRun && runtime_.isRunning());

        if (!viz_.autoRun || !runtime_.isRunning() || runtime_.isPaused()) {
            return;
        }

        if (autoRunFuture_.valid() && autoRunFuture_.wait_for(std::chrono::seconds(0)) != std::future_status::ready) {
            return;
        }

        if (uiParameterChangedThisFrame_) {
            requestSnapshotRefresh();
        }

        const bool prioritizeUi = glfwGetTime() < uiInteractionHotUntilSec_;

        autoRunStepBudget_ += static_cast<double>(std::max(1, viz_.simulationTickHz)) *
            static_cast<double>(std::max(0.0f, frameDt));
        const double budgetCap = std::max(16.0, static_cast<double>(std::max(1, viz_.simulationTickHz)) * 2.0);
        autoRunStepBudget_ = std::min(autoRunStepBudget_, budgetCap);
        const int maxBatch = prioritizeUi ? 2 : 8;
        const int batch = std::min(maxBatch, static_cast<int>(std::floor(autoRunStepBudget_)));
        if (batch <= 0) {
            return;
        }

        autoRunStepBudget_ -= static_cast<double>(batch);

        const int stepsPerTick = std::clamp(viz_.autoStepsPerFrame, 1, 512);
        const int maxDispatchSteps = prioritizeUi ? 2048 : 8192;
        const int stepsToRun = std::min(maxDispatchSteps, std::max(1, stepsPerTick * batch));
        autoRunFuture_ = std::async(std::launch::async, [this, stepsToRun]() {
            std::string message;
            if (!runtime_.step(static_cast<std::uint32_t>(stepsToRun), message)) {
                const std::lock_guard<std::mutex> lock(asyncStateMutex_);
                asyncErrorMessage_ = message;
                asyncErrorPending_ = true;
            }
            requestSnapshotRefresh();
        });
    }

#endif // WS_MAIN_WINDOW_IMPL_CLASS_CONTEXT
