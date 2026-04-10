#pragma once

#include <sstream>
#include <string>
#include <string_view>
#include <vector>

namespace ws::gui {

// Lightweight hierarchical error-context stack for operation-level failures.
// Usage:
//   ErrorContext ctx("open_world");
//   auto scope = ctx.push("restore_checkpoint");
//   message = ctx.formatFailure("world_open_failed", exception.what());
class ErrorContext {
public:
    class Scope {
    public:
        Scope(ErrorContext& owner, const bool active) : owner_(&owner), active_(active) {}
        Scope(const Scope&) = delete;
        Scope& operator=(const Scope&) = delete;

        Scope(Scope&& other) noexcept : owner_(other.owner_), active_(other.active_) {
            other.owner_ = nullptr;
            other.active_ = false;
        }

        Scope& operator=(Scope&& other) noexcept {
            if (this == &other) {
                return *this;
            }
            release();
            owner_ = other.owner_;
            active_ = other.active_;
            other.owner_ = nullptr;
            other.active_ = false;
            return *this;
        }

        ~Scope() {
            release();
        }

    private:
        void release() {
            if (active_ && owner_ != nullptr) {
                owner_->pop();
                active_ = false;
            }
        }

        ErrorContext* owner_ = nullptr;
        bool active_ = false;
    };

    explicit ErrorContext(std::string rootFrame = {}) {
        if (!rootFrame.empty()) {
            frames_.push_back(std::move(rootFrame));
        }
    }

    [[nodiscard]] Scope push(std::string frame) {
        if (frame.empty()) {
            return Scope(*this, false);
        }
        frames_.push_back(std::move(frame));
        return Scope(*this, true);
    }

    [[nodiscard]] std::string chain() const {
        if (frames_.empty()) {
            return {};
        }

        std::ostringstream out;
        for (std::size_t i = 0; i < frames_.size(); ++i) {
            if (i > 0u) {
                out << '>';
            }
            out << frames_[i];
        }
        return out.str();
    }

    [[nodiscard]] std::string formatFailure(const std::string_view failureCode, const std::string_view detail) const {
        std::ostringstream out;
        out << failureCode;

        const std::string chainValue = chain();
        if (!chainValue.empty()) {
            out << " context=" << chainValue;
        }
        if (!detail.empty()) {
            out << " error=" << detail;
        }
        return out.str();
    }

private:
    void pop() {
        if (!frames_.empty()) {
            frames_.pop_back();
        }
    }

    std::vector<std::string> frames_;
};

} // namespace ws::gui
