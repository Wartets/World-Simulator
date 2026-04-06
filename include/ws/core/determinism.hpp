#pragma once

// Standard library
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <random>
#include <span>
#include <string>
#include <type_traits>

namespace ws {

// =============================================================================
// Deterministic Hash
// =============================================================================

// Provides FNV-1a hash functions for deterministic state hashing.
// Used for computing reproducible state hashes and run signatures.
class DeterministicHash {
public:
    // FNV-1a hash parameters.
    static constexpr std::uint64_t offsetBasis = 14695981039346656037ull;
    static constexpr std::uint64_t prime = 1099511628211ull;

    // Computes FNV-1a hash of a byte sequence.
    [[nodiscard]] static std::uint64_t fnv1a(std::span<const std::byte> bytes) noexcept {
        std::uint64_t value = offsetBasis;
        for (const auto byte : bytes) {
            value ^= static_cast<std::uint64_t>(byte);
            value *= prime;
        }
        return value;
    }

    // Combines two hash values using a mixing function.
    [[nodiscard]] static std::uint64_t combine(std::uint64_t current, std::uint64_t next) noexcept {
        constexpr std::uint64_t k = 0x9e3779b97f4a7c15ull;
        return current ^ (next + k + (current << 6u) + (current >> 2u));
    }

    // Hashes a string using FNV-1a.
    [[nodiscard]] static std::uint64_t hashString(const std::string& value) noexcept {
        const auto* raw = reinterpret_cast<const std::byte*>(value.data());
        return fnv1a(std::span<const std::byte>(raw, value.size()));
    }

    // Hashes a POD type by treating its memory as bytes.
    template <typename T>
    [[nodiscard]] static std::uint64_t hashPod(const T& value) noexcept {
        static_assert(std::is_trivially_copyable_v<T>, "hashPod expects trivially copyable type");
        const auto* raw = reinterpret_cast<const std::byte*>(&value);
        return fnv1a(std::span<const std::byte>(raw, sizeof(T)));
    }
};

// =============================================================================
// Deterministic RNG Factory
// =============================================================================

// Factory for creating deterministic random number generators.
// Each stream is derived from a root seed and a stream name, ensuring
// reproducible random number sequences across runs.
class DeterministicRngFactory {
public:
    // Constructs a factory with the given root seed.
    explicit DeterministicRngFactory(const std::uint64_t rootSeed) noexcept : rootSeed_(rootSeed) {}

    // Creates a deterministic random number generator for a named stream.
    // The stream name is incorporated into the seed derivation.
    [[nodiscard]] std::mt19937_64 createStream(const std::string& streamName) const noexcept {
        const std::uint64_t streamKey = DeterministicHash::hashString(streamName);
        const std::uint64_t mixed = splitmix64(rootSeed_ ^ streamKey);

        std::array<std::uint32_t, 4> seedMaterial{};
        seedMaterial[0] = static_cast<std::uint32_t>(mixed & std::numeric_limits<std::uint32_t>::max());
        seedMaterial[1] = static_cast<std::uint32_t>((mixed >> 32u) & std::numeric_limits<std::uint32_t>::max());
        seedMaterial[2] = static_cast<std::uint32_t>((rootSeed_ ^ streamKey) & std::numeric_limits<std::uint32_t>::max());
        seedMaterial[3] = static_cast<std::uint32_t>(((rootSeed_ ^ streamKey) >> 32u) & std::numeric_limits<std::uint32_t>::max());

        std::seed_seq seedSeq(seedMaterial.begin(), seedMaterial.end());
        return std::mt19937_64(seedSeq);
    }

private:
    // SplitMix64 hash function for seed mixing.
    [[nodiscard]] static std::uint64_t splitmix64(std::uint64_t value) noexcept {
        value += 0x9e3779b97f4a7c15ull;
        value = (value ^ (value >> 30u)) * 0xbf58476d1ce4e5b9ull;
        value = (value ^ (value >> 27u)) * 0x94d049bb133111ebull;
        return value ^ (value >> 31u);
    }

    std::uint64_t rootSeed_;
};

} // namespace ws
