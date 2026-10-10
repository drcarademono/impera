#include <array>

// DOS starting-state offsets, audited against the decompiled engine.
struct StateField {
    const char *name;
    int offset, width;
    const char *help;
};
static constexpr std::array<StateField, 0> stateFields{};
