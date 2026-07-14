#pragma once

#include <cstddef>

inline char dashAsciiLower(char value)
{
    return value >= 'A' && value <= 'Z'
               ? static_cast<char>(value - 'A' + 'a')
               : value;
}

inline bool dashBoolTokenEquals(const char *raw, const char *expected)
{
    if (!raw || !expected)
        return false;

    size_t index = 0;
    while (raw[index] && expected[index])
    {
        if (dashAsciiLower(raw[index]) != expected[index])
            return false;
        index++;
    }
    return raw[index] == '\0' && expected[index] == '\0';
}

inline bool dashParseStrictBool(const char *raw, bool &out)
{
    if (dashBoolTokenEquals(raw, "1") ||
        dashBoolTokenEquals(raw, "true") ||
        dashBoolTokenEquals(raw, "on") ||
        dashBoolTokenEquals(raw, "yes"))
    {
        out = true;
        return true;
    }

    if (dashBoolTokenEquals(raw, "0") ||
        dashBoolTokenEquals(raw, "false") ||
        dashBoolTokenEquals(raw, "off") ||
        dashBoolTokenEquals(raw, "no"))
    {
        out = false;
        return true;
    }

    return false;
}

struct DashPersistedBoolUpdate
{
    bool valid{false};
    bool persisted{false};
    bool value{false};
};

template <typename PersistFn>
DashPersistedBoolUpdate dashPreparePersistedBoolUpdate(
    const char *raw,
    bool currentValue,
    PersistFn persist)
{
    bool parsed = currentValue;
    if (!dashParseStrictBool(raw, parsed))
        return {false, false, currentValue};
    if (!persist(parsed))
        return {true, false, currentValue};
    return {true, true, parsed};
}
