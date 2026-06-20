#pragma once

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdint>
#include <cstring>
#include <string>
#include <utility>
#include <vector>

#include "can_frame_types.h"
#include "drivers/can_driver.h"
#include "can_helpers.h"
#include <ArduinoJson.h>

static constexpr uint8_t kDashPluginMaxPlugins = 8;
static constexpr uint8_t kDashPluginMaxRules = 16;
static constexpr uint8_t kDashPluginMaxOps = 16;
static constexpr uint8_t kDashPluginMaxFilterIds = 32;
static constexpr size_t kDashPluginMaxJsonBytes = 8192;
static constexpr uint8_t kDashPluginReplayMin = 1;
static constexpr uint8_t kDashPluginReplayMax = 5;

struct DashPluginResult
{
    bool ok = false;
    std::string message;
    static DashPluginResult success(const char *msg = "ok") { return {true, msg ? msg : "ok"}; }
    static DashPluginResult fail(const char *msg) { return {false, msg ? msg : "error"}; }
};

struct DashPluginContext
{
    bool canActive = false;
    bool otaAllowed = false;
    bool apGateAllowed = false;
    bool fsdMasterEnabled = false;
    uint8_t defaultBus = CAN_BUS_DEFAULT;
};

struct DashPluginApplyResult
{
    bool matched = false;
    bool modified = false;
    bool sent = false;
    uint8_t sendCount = 0;
    std::string blockedBy;
};

enum class DashPluginOpType : uint8_t
{
    SetBit,
    SetByte,
    OrByte,
    AndByte,
    Counter,
    Checksum,
    EmitPeriodic,
};

struct DashPluginOp
{
    DashPluginOpType type = DashPluginOpType::Checksum;
    uint8_t byte = 0;
    uint8_t bit = 0;
    uint8_t val = 0;
    uint8_t mask = 0xFF;
    uint8_t step = 1;
    uint16_t intervalMs = 100;
    bool gtwSilent = false;
};

struct DashPluginRule
{
    uint32_t id = 0;
    bool hasBus = false;
    uint8_t busMask = 0;
    int16_t mux = -1;
    uint16_t muxMask = 0;
    bool hasMatch = false;
    uint8_t matchByte = 0;
    uint8_t matchMask = 0;
    uint8_t matchVal = 0;
    bool send = true;
    std::vector<DashPluginOp> ops;
};

struct DashPlugin
{
    std::string name;
    std::string version = "1.0";
    std::string author;
    bool enabled = false;
    uint8_t priority = 1;
    bool compatible = true;
    bool gtwSilentRequested = false;
    bool gtwSilentAvailable = true;
    std::string lastError;
    std::vector<DashPluginRule> rules;
    std::vector<std::string> conflicts;
    std::vector<std::string> warnings;
    // Original install source so the plugin can be re-installed verbatim from
    // the persisted config file. Captured in installJson().
    std::string sourceJson;
};

class DashPluginStatusString : public std::string
{
public:
    using std::string::string;
    DashPluginStatusString(const std::string &value) : std::string(value) {}

    int indexOf(const char *needle) const
    {
        const size_t pos = find(needle ? needle : "");
        return pos == npos ? -1 : static_cast<int>(pos);
    }
};

static inline std::string dashPluginString(JsonVariantConst v, const char *fallback = "")
{
    if (v.isNull())
        return fallback ? fallback : "";
    if (v.is<const char *>())
        return v.as<const char *>();
    if (v.is<std::string>())
        return v.as<std::string>();
    return fallback ? fallback : "";
}

static inline std::string dashPluginLowerTrim(const std::string &input)
{
    size_t start = 0;
    while (start < input.size() && std::isspace(static_cast<unsigned char>(input[start])))
        ++start;
    size_t end = input.size();
    while (end > start && std::isspace(static_cast<unsigned char>(input[end - 1])))
        --end;

    std::string out = input.substr(start, end - start);
    std::transform(out.begin(), out.end(), out.begin(), [](unsigned char c) { return static_cast<char>(std::toupper(c)); });
    return out;
}

static inline uint8_t dashPluginBusBit(const std::string &name)
{
    const std::string normalized = dashPluginLowerTrim(name);
    if (normalized == "CH" || normalized == "CAN_CH" || normalized == "CAN_BUS_CH")
        return CAN_BUS_CH;
    if (normalized == "VEH" || normalized == "VEHICLE" || normalized == "CAN_VEH" || normalized == "CAN_BUS_VEH")
        return CAN_BUS_VEH;
    if (normalized == "PARTY" || normalized == "CAN_PARTY" || normalized == "CAN_BUS_PARTY")
        return CAN_BUS_PARTY;
    return 0;
}

static inline uint8_t dashPluginDefaultBusMask(uint8_t defaultBus)
{
    if (defaultBus == CAN_BUS_CH)
        return CAN_BUS_CH;
    if (defaultBus == CAN_BUS_VEH)
        return CAN_BUS_VEH;
    if (defaultBus == CAN_BUS_PARTY)
        return CAN_BUS_PARTY;
    return CAN_BUS_CH;
}

static inline bool dashPluginReadInt(JsonVariantConst v, int64_t minValue, int64_t maxValue, int64_t &out)
{
    if (!v.is<int>())
        return false;
    const int64_t parsed = v.as<int64_t>();
    if (parsed < minValue || parsed > maxValue)
        return false;
    out = parsed;
    return true;
}

static inline bool dashPluginReadOptionalInt(JsonVariantConst v, int64_t minValue, int64_t maxValue, int64_t &out, int64_t fallback)
{
    if (v.isNull())
    {
        out = fallback;
        return true;
    }
    return dashPluginReadInt(v, minValue, maxValue, out);
}

static inline bool dashPluginReadBool(JsonVariantConst v, bool &out)
{
    if (!v.is<bool>())
        return false;
    out = v.as<bool>();
    return true;
}

static inline bool dashPluginReadOptionalBool(JsonVariantConst v, bool &out, bool fallback)
{
    if (v.isNull())
    {
        out = fallback;
        return true;
    }
    return dashPluginReadBool(v, out);
}

static inline uint8_t dashPluginParseBus(JsonVariantConst v)
{
    if (v.isNull())
        return 0;
    if (v.is<int>())
    {
        const int bus = v.as<int>();
        if (bus < 0 || bus > static_cast<int>(CAN_BUS_CH | CAN_BUS_VEH | CAN_BUS_PARTY))
            return 0;
        return static_cast<uint8_t>(bus);
    }
    if (v.is<const char *>())
    {
        uint8_t mask = 0;
        std::string text = v.as<const char *>();
        size_t begin = 0;
        while (begin <= text.size())
        {
            const size_t comma = text.find(',', begin);
            const std::string token = text.substr(begin, comma == std::string::npos ? std::string::npos : comma - begin);
            const uint8_t bit = dashPluginBusBit(token);
            if (bit == 0)
                return 0;
            mask |= bit;
            if (comma == std::string::npos)
                break;
            begin = comma + 1;
        }
        return mask;
    }
    if (v.is<JsonArrayConst>())
    {
        uint8_t mask = 0;
        for (JsonVariantConst item : v.as<JsonArrayConst>())
        {
            const uint8_t parsed = dashPluginParseBus(item);
            if (parsed == 0)
                return 0;
            mask |= parsed;
        }
        return mask;
    }
    return 0;
}

class DashPluginEngine
{
public:
    DashPluginResult installJson(const char *json, bool enabledAfterInstall)
    {
        if (json == nullptr || json[0] == '\0')
            return DashPluginResult::fail("empty json");
        if (std::strlen(json) > kDashPluginMaxJsonBytes)
            return DashPluginResult::fail("json too large");

        JsonDocument doc;
        const DeserializationError err = deserializeJson(doc, json);
        if (err)
            return DashPluginResult::fail("invalid json");
        if (doc.overflowed())
            return DashPluginResult::fail("json too large");
        if (!doc.is<JsonObjectConst>())
            return DashPluginResult::fail("plugin must be object");

        DashPlugin plugin;
        DashPluginResult parsed = parsePlugin(doc.as<JsonObjectConst>(), plugin);
        if (!parsed.ok)
            return parsed;
        plugin.enabled = enabledAfterInstall;
        finalizeGtwSilentAvailability(plugin);
        // Capture the verbatim install source so export/import can re-install it.
        plugin.sourceJson = json;

        const auto existing = std::find_if(plugins_.begin(), plugins_.end(), [&](const DashPlugin &p) {
            return p.name == plugin.name;
        });
        if (existing != plugins_.end())
        {
            clearPeriodicCache();
            plugin.priority = existing->priority;
            *existing = plugin;
            normalizePriorities(plugin.name.c_str());
            return DashPluginResult::success("replaced");
        }

        if (plugins_.size() >= kDashPluginMaxPlugins)
            return DashPluginResult::fail("max plugins reached");

        clearPeriodicCache();
        plugin.priority = static_cast<uint8_t>(plugins_.size() + 1);
        plugins_.push_back(plugin);
        normalizePriorities(plugin.name.c_str());
        return DashPluginResult::success("installed");
    }

    size_t pluginCount() const { return plugins_.size(); }

    // Persist the engine configuration to a compact JSON document containing
    // replayCount and the verbatim install source of every plugin (plus its
    // enabled flag and priority). The output is meant to be written to SPIFFS
    // and round-tripped back through importConfigJson().
    std::string exportConfigJson() const
    {
        JsonDocument doc;
        doc["replayCount"] = replayCount_;
        JsonArray plugins = doc["plugins"].to<JsonArray>();
        for (const DashPlugin &p : plugins_)
        {
            JsonObject item = plugins.add<JsonObject>();
            item["name"] = p.name;
            item["enabled"] = p.enabled;
            item["priority"] = p.priority;
            item["source"] = p.sourceJson;
        }
        std::string out;
        serializeJson(doc, out);
        return out;
    }

    // Rebuild the engine from a document produced by exportConfigJson(). An
    // empty/null payload is treated as a successful no-op ("empty") so a
    // missing or empty state file does not block boot. replayCount is clamped
    // to the legal 1..5 range; each plugin is reinstalled from its captured
    // source and its priority restored. Invalid plugin sources are skipped.
    DashPluginResult importConfigJson(const char *json)
    {
        if (!json || !*json)
            return DashPluginResult::success("empty");
        JsonDocument doc;
        if (deserializeJson(doc, json))
            return DashPluginResult::fail("invalid plugin config");
        plugins_.clear();
        replayCount_ = doc["replayCount"] | 1;
        if (replayCount_ < 1)
            replayCount_ = 1;
        if (replayCount_ > 5)
            replayCount_ = 5;
        for (JsonObjectConst item : doc["plugins"].as<JsonArrayConst>())
        {
            std::string source = dashPluginString(item["source"]);
            DashPluginResult r = installJson(source.c_str(), item["enabled"] | false);
            if (!r.ok)
                continue;
            setPriority(dashPluginString(item["name"]).c_str(), item["priority"] | 1);
        }
        return DashPluginResult::success("imported");
    }

    // Returns true if any enabled+compatible plugin has a rule matching canId.
    // Used to detect when a plugin owns a frame (e.g. FSD activation 1006/1021
    // / 2047) so the built-in injection can be suppressed.
    bool hasEnabledRuleFor(uint32_t canId) const
    {
        for (const DashPlugin &p : plugins_)
            if (p.enabled && p.compatible)
                for (const DashPluginRule &r : p.rules)
                    if (r.id == canId)
                        return true;
        return false;
    }

    const DashPlugin &pluginAt(size_t index) const { return plugins_.at(index); }

    uint8_t replayCount() const { return replayCount_; }

    DashPluginResult setEnabled(const char *name, bool enabled)
    {
        DashPlugin *plugin = findPlugin(name);
        if (plugin == nullptr)
            return DashPluginResult::fail("plugin not found");
        clearPeriodicCache();
        plugin->enabled = enabled;
        return DashPluginResult::success();
    }

    DashPluginResult setPriority(const char *name, uint8_t priority)
    {
        DashPlugin *plugin = findPlugin(name);
        if (plugin == nullptr)
            return DashPluginResult::fail("plugin not found");
        const size_t count = plugins_.size();
        const uint8_t clamped = static_cast<uint8_t>(std::max<size_t>(1, std::min<size_t>(count, priority)));
        clearPeriodicCache();
        plugin->priority = clamped;
        normalizePriorities(name);
        return DashPluginResult::success();
    }

    DashPluginResult setReplayCount(int count)
    {
        if (count < kDashPluginReplayMin || count > kDashPluginReplayMax)
            return DashPluginResult::fail("replay count out of range");
        replayCount_ = static_cast<uint8_t>(count);
        return DashPluginResult::success();
    }

    DashPluginResult remove(const char *name)
    {
        if (!name)
            return DashPluginResult::fail("plugin not found");
        const size_t before = plugins_.size();
        plugins_.erase(std::remove_if(plugins_.begin(), plugins_.end(),
                                      [&](const DashPlugin &p) { return p.name == name; }),
                       plugins_.end());
        if (plugins_.size() == before)
            return DashPluginResult::fail("plugin not found");
        // No preferred plugin after a removal: re-pack priorities 1..N in the
        // surviving order. normalizePriorities() handles nullptr preferredName.
        normalizePriorities(nullptr);
        return DashPluginResult::success("removed");
    }

    DashPluginStatusString statusJson() const
    {
        JsonDocument doc;
        doc["engineAvailable"] = true;
        doc["engineEnabled"] = true;
        doc["storage"] = "spiffs";
        doc["replayCount"] = replayCount_;

        JsonObject limits = doc["limits"].to<JsonObject>();
        limits["maxPlugins"] = kDashPluginMaxPlugins;
        limits["maxRulesPerPlugin"] = kDashPluginMaxRules;
        limits["maxOpsPerRule"] = kDashPluginMaxOps;
        limits["maxFilterIdsPerPlugin"] = kDashPluginMaxFilterIds;
        limits["maxJsonBytes"] = kDashPluginMaxJsonBytes;

#ifndef PLUGIN_GTW_UDS_CUSTOM_KEY
        if (hasUnavailableGtwSilentPlugin())
            doc["warning"] = "custom key build required";
#endif

        JsonArray installed = doc["installed"].to<JsonArray>();
        for (const DashPlugin &plugin : plugins_)
        {
            JsonObject item = installed.add<JsonObject>();
            item["name"] = plugin.name;
            item["version"] = plugin.version;
            item["author"] = plugin.author;
            item["enabled"] = plugin.enabled;
            item["priority"] = plugin.priority;
            item["ruleCount"] = plugin.rules.size();
            item["compatible"] = plugin.compatible;
            item["gtwSilentRequested"] = plugin.gtwSilentRequested;
            item["gtwSilentAvailable"] = plugin.gtwSilentAvailable;
            item["lastError"] = plugin.lastError;
            JsonArray warnings = item["warnings"].to<JsonArray>();
            for (const std::string &warning : plugin.warnings)
                warnings.add(warning);
            JsonArray targetIds = item["targetIds"].to<JsonArray>();
            for (const DashPluginRule &rule : plugin.rules)
                targetIds.add(rule.id);
            JsonArray conflicts = item["conflicts"].to<JsonArray>();
            for (const std::string &conflict : plugin.conflicts)
                conflicts.add(conflict);
        }

        std::string out;
        serializeJson(doc, out);
        return DashPluginStatusString(out);
    }

    DashPluginApplyResult applyToFrame(const CanFrame &frame, const DashPluginContext &ctx, CanDriver &driver)
    {
        DashPluginApplyResult result{};
        if (!ctx.canActive)
        {
            result.blockedBy = "can_active";
            clearPeriodicCache();
            return result;
        }
        if (!ctx.otaAllowed)
        {
            result.blockedBy = "ota";
            clearPeriodicCache();
            return result;
        }
        if (!ctx.apGateAllowed)
        {
            result.blockedBy = "ap_gate";
            clearPeriodicCache();
            return result;
        }
        if (!ctx.fsdMasterEnabled)
        {
            result.blockedBy = "fsd_master";
            clearPeriodicCache();
            return result;
        }

        std::vector<DashPlugin *> ordered;
        ordered.reserve(plugins_.size());
        for (DashPlugin &plugin : plugins_)
        {
            plugin.conflicts.clear();
            if (plugin.enabled && plugin.compatible)
                ordered.push_back(&plugin);
        }
        std::sort(ordered.begin(), ordered.end(), [](const DashPlugin *a, const DashPlugin *b) {
            return a->priority < b->priority;
        });

        CanFrame out = frame;
        if (out.bus == CAN_BUS_ANY)
            out.bus = ctx.defaultBus;

        std::array<DashPluginOwnership, 64> owned{};
        bool shouldSend = false;
        DashPluginSendPlan sendPlan;
        bool periodicRequested = false;
        DashPluginOp periodicOp{};
        std::string periodicPluginName;

        for (DashPlugin *plugin : ordered)
        {
            for (const DashPluginRule &rule : plugin->rules)
            {
                if (!ruleMatches(rule, frame, ctx.defaultBus))
                    continue;
                result.matched = true;
                if (!rule.send)
                    continue;

                for (const DashPluginOp &op : rule.ops)
                    executeOp(*plugin, out, frame, op, owned, result, sendPlan, periodicRequested, periodicOp, periodicPluginName);
                shouldSend = true;
            }
        }

        if (sendPlan.checksumRequested)
        {
            const uint8_t previous = out.data[7];
            out.data[7] = checksumByte(out);
            if (out.data[7] != previous)
                result.modified = true;
        }
        if (periodicRequested)
            cachePeriodic(out, periodicOp, periodicPluginName, sendPlan);

        if (!shouldSend || !result.modified)
            return result;

        const uint8_t sends = out.id == 2047 ? replayCount_ : 1;
        CanFrame sendFrame = out;
        for (uint8_t i = 0; i < sends; ++i)
        {
            if (i > 0)
                applySendPlan(sendFrame, sendPlan);
            if (driver.send(sendFrame))
            {
                result.sent = true;
                ++result.sendCount;
            }
        }
        return result;
    }

    void tickPeriodic(uint32_t nowMs, const DashPluginContext &ctx, CanDriver &driver)
    {
        if (!periodicValid_)
            return;
        if (!ctx.canActive || !ctx.otaAllowed || !ctx.apGateAllowed || !ctx.fsdMasterEnabled)
        {
            clearPeriodicCache();
            return;
        }
        const DashPlugin *plugin = findPluginConst(periodicPluginName_.c_str());
        if (plugin == nullptr || !plugin->enabled || !plugin->compatible)
        {
            clearPeriodicCache();
            return;
        }
        if (periodicLastMs_ && nowMs - periodicLastMs_ < periodicIntervalMs_)
            return;
        periodicLastMs_ = nowMs;
        applySendPlan(periodicFrame_, periodicSendPlan_);
        driver.send(periodicFrame_);
    }

private:
    struct DashPluginOwnership
    {
        const DashPlugin *plugin = nullptr;
        uint8_t priority = 0;
    };

    struct DashPluginSendPlan
    {
        std::vector<DashPluginOp> counterOps;
        bool checksumRequested = false;
    };

    static bool ruleMatches(const DashPluginRule &rule, const CanFrame &frame, uint8_t defaultBus)
    {
        if (frame.id != rule.id)
            return false;
        if (rule.hasBus)
        {
            uint8_t frameMask = dashPluginDefaultBusMask(frame.bus == CAN_BUS_ANY ? defaultBus : frame.bus);
            if ((rule.busMask & frameMask) == 0)
                return false;
        }
        if (rule.mux >= 0)
        {
            uint8_t mask = static_cast<uint8_t>(rule.muxMask ? rule.muxMask : 0xFF);
            if ((frame.data[0] & mask) != (static_cast<uint8_t>(rule.mux) & mask))
                return false;
        }
        if (rule.hasMatch)
        {
            if (rule.matchByte > 7)
                return false;
            if ((frame.data[rule.matchByte] & rule.matchMask) != (rule.matchVal & rule.matchMask))
                return false;
        }
        return true;
    }

    static uint8_t checksumByte(const CanFrame &frame)
    {
        uint16_t sum = static_cast<uint16_t>(frame.id & 0xFF) + static_cast<uint16_t>((frame.id >> 8) & 0xFF);
        for (uint8_t i = 0; i < 7; ++i)
            sum += frame.data[i];
        return static_cast<uint8_t>(sum & 0xFF);
    }

    static void applyCounter(CanFrame &frame, const DashPluginOp &op)
    {
        uint8_t mask = op.mask ? op.mask : 0x0F;
        uint8_t shift = 0;
        while (shift < 8 && ((mask >> shift) & 0x01) == 0)
            ++shift;
        uint8_t field = static_cast<uint8_t>((frame.data[op.byte] & mask) >> shift);
        uint8_t widthMask = static_cast<uint8_t>(mask >> shift);
        field = static_cast<uint8_t>((field + (op.step ? op.step : 1)) & widthMask);
        frame.data[op.byte] = static_cast<uint8_t>((frame.data[op.byte] & ~mask) | ((field << shift) & mask));
    }

    static bool bitBlockedByOtherPlugin(const std::array<DashPluginOwnership, 64> &owned, const DashPlugin &plugin, uint8_t bit)
    {
        if (bit >= owned.size())
            return false;
        const DashPluginOwnership &owner = owned[bit];
        return owner.plugin != nullptr && owner.plugin != &plugin && owner.priority <= plugin.priority;
    }

    static void markByteMaskOwned(std::array<DashPluginOwnership, 64> &owned, const DashPlugin &plugin, uint8_t byte, uint8_t mask)
    {
        if (byte > 7)
            return;
        for (uint8_t b = 0; b < 8; ++b)
            if (mask & (1u << b))
                owned[byte * 8 + b] = {&plugin, plugin.priority};
    }

    static uint8_t writableMask(const std::array<DashPluginOwnership, 64> &owned, const DashPlugin &plugin, uint8_t byte, uint8_t mask)
    {
        if (byte > 7)
            return 0;
        uint8_t available = 0;
        for (uint8_t b = 0; b < 8; ++b)
        {
            if ((mask & (1u << b)) == 0)
                continue;
            const DashPluginOwnership &owner = owned[byte * 8 + b];
            if (owner.plugin == nullptr || owner.plugin == &plugin || owner.priority > plugin.priority)
                available |= static_cast<uint8_t>(1u << b);
        }
        return available;
    }

    static bool hasBlockedOverlap(const std::array<DashPluginOwnership, 64> &owned, const DashPlugin &plugin, uint8_t byte, uint8_t mask)
    {
        if (byte > 7)
            return false;
        for (uint8_t b = 0; b < 8; ++b)
            if ((mask & (1u << b)) && bitBlockedByOtherPlugin(owned, plugin, byte * 8 + b))
                return true;
        return false;
    }

    static void recordPriorityOverlap(DashPlugin &plugin)
    {
        plugin.conflicts.push_back("Priority overlap");
    }

    void clearPeriodicCache()
    {
        periodicValid_ = false;
        periodicPluginName_.clear();
        periodicSendPlan_ = DashPluginSendPlan{};
        periodicLastMs_ = 0;
        gtwSilentRequested_ = false;
    }

    void cachePeriodic(const CanFrame &frame, const DashPluginOp &op, const std::string &pluginName, const DashPluginSendPlan &sendPlan)
    {
        periodicFrame_ = frame;
        periodicSendPlan_ = sendPlan;
        periodicIntervalMs_ = op.intervalMs ? op.intervalMs : 100;
        periodicValid_ = true;
        periodicPluginName_ = pluginName;
        gtwSilentRequested_ = op.gtwSilent;
    }

    static void applySendPlan(CanFrame &frame, const DashPluginSendPlan &sendPlan)
    {
        for (const DashPluginOp &op : sendPlan.counterOps)
            applyCounter(frame, op);
        if (sendPlan.checksumRequested)
            frame.data[7] = checksumByte(frame);
    }

    void applyByteOp(DashPlugin &plugin, CanFrame &out, uint8_t byte, uint8_t mask, uint8_t nextByte,
                     std::array<DashPluginOwnership, 64> &owned, DashPluginApplyResult &result)
    {
        if (byte > 7)
            return;
        if (hasBlockedOverlap(owned, plugin, byte, mask))
            recordPriorityOverlap(plugin);
        const uint8_t writable = writableMask(owned, plugin, byte, mask);
        if (writable == 0)
            return;
        const uint8_t previous = out.data[byte];
        out.data[byte] = static_cast<uint8_t>((previous & ~writable) | (nextByte & writable));
        markByteMaskOwned(owned, plugin, byte, writable);
        if (out.data[byte] != previous)
            result.modified = true;
    }

    void executeOp(DashPlugin &plugin, CanFrame &out, const CanFrame &original, const DashPluginOp &op,
                   std::array<DashPluginOwnership, 64> &owned, DashPluginApplyResult &result,
                   DashPluginSendPlan &sendPlan, bool &periodicRequested, DashPluginOp &periodicOp, std::string &periodicPluginName)
    {
        switch (op.type)
        {
        case DashPluginOpType::SetBit:
            if (bitBlockedByOtherPlugin(owned, plugin, op.bit))
            {
                plugin.conflicts.push_back("Priority overlap on bit " + std::to_string(op.bit));
                break;
            }
            {
                const uint8_t byte = static_cast<uint8_t>(op.bit / 8);
                const uint8_t bitMask = static_cast<uint8_t>(1u << (op.bit % 8));
                const uint8_t before = out.data[byte];
                setBit(out, op.bit, op.val != 0);
                markByteMaskOwned(owned, plugin, byte, bitMask);
                if (out.data[byte] != before)
                    result.modified = true;
            }
            break;
        case DashPluginOpType::SetByte:
        {
            const uint8_t mask = op.mask ? op.mask : 0xFF;
            const uint8_t nextByte = static_cast<uint8_t>((out.data[op.byte] & ~mask) | (op.val & mask));
            applyByteOp(plugin, out, op.byte, mask, nextByte, owned, result);
            break;
        }
        case DashPluginOpType::OrByte:
        {
            const uint8_t mask = op.mask ? op.mask : 0xFF;
            const uint8_t nextByte = static_cast<uint8_t>(out.data[op.byte] | (op.val & mask));
            applyByteOp(plugin, out, op.byte, mask, nextByte, owned, result);
            break;
        }
        case DashPluginOpType::AndByte:
        {
            const uint8_t mask = op.mask ? op.mask : 0xFF;
            const uint8_t nextByte = static_cast<uint8_t>((out.data[op.byte] & ~mask) | ((out.data[op.byte] & op.val) & mask));
            applyByteOp(plugin, out, op.byte, mask, nextByte, owned, result);
            break;
        }
        case DashPluginOpType::Counter:
        {
            CanFrame counted = out;
            applyCounter(counted, op);
            const uint8_t mask = op.mask ? op.mask : 0x0F;
            const uint8_t writable = op.byte <= 7 ? writableMask(owned, plugin, op.byte, mask) : 0;
            applyByteOp(plugin, out, op.byte, mask, counted.data[op.byte], owned, result);
            if (writable != 0)
                sendPlan.counterOps.push_back(op);
            break;
        }
        case DashPluginOpType::Checksum:
            sendPlan.checksumRequested = true;
            break;
        case DashPluginOpType::EmitPeriodic:
            periodicRequested = true;
            periodicOp = op;
            periodicPluginName = plugin.name;
            break;
        }
        (void)original;
    }

    DashPluginResult parsePlugin(JsonObjectConst obj, DashPlugin &plugin) const
    {
        const std::string name = dashPluginString(obj["name"]);
        if (name.empty())
            return DashPluginResult::fail("plugin name required");
        if (name.size() > 31)
            return DashPluginResult::fail("plugin name too long");

        plugin.name = name;
        plugin.version = dashPluginString(obj["version"], "1.0");
        plugin.author = dashPluginString(obj["author"]);

        JsonVariantConst rulesValue = obj["rules"];
        if (!rulesValue.is<JsonArrayConst>())
            return DashPluginResult::fail("rules required");
        JsonArrayConst rules = rulesValue.as<JsonArrayConst>();
        if (rules.size() == 0)
            return DashPluginResult::fail("rules required");
        if (rules.size() > kDashPluginMaxRules)
            return DashPluginResult::fail("too many rules");

        for (JsonVariantConst item : rules)
        {
            if (!item.is<JsonObjectConst>())
                return DashPluginResult::fail("rule must be object");
            DashPluginRule rule;
            DashPluginResult parsed = parseRule(item.as<JsonObjectConst>(), rule);
            if (!parsed.ok)
                return parsed;
            plugin.rules.push_back(rule);
        }

        return DashPluginResult::success();
    }

    DashPluginResult parseRule(JsonObjectConst obj, DashPluginRule &rule) const
    {
        int64_t id = 0;
        if (!dashPluginReadInt(obj["id"], 0, 0x1FFFFFFFLL, id))
            return DashPluginResult::fail("rule id must be integer in range");
        rule.id = static_cast<uint32_t>(id);

        if (!obj["bus"].isNull())
        {
            const uint8_t bus = dashPluginParseBus(obj["bus"]);
            if (bus == 0)
                return DashPluginResult::fail("unknown bus");
            rule.hasBus = true;
            rule.busMask = bus;
        }

        if (!obj["mux"].isNull())
        {
            int64_t mux = 0;
            if (!dashPluginReadInt(obj["mux"], -1, 255, mux))
                return DashPluginResult::fail("mux must be integer in range");
            rule.mux = static_cast<int16_t>(mux);
        }

        JsonVariantConst muxMaskValue = !obj["mux_mask"].isNull() ? obj["mux_mask"] : obj["muxMask"];
        if (!muxMaskValue.isNull())
        {
            int64_t muxMask = 0;
            if (!dashPluginReadInt(muxMaskValue, 0, 255, muxMask))
                return DashPluginResult::fail("mux mask must be integer in range");
            rule.muxMask = static_cast<uint16_t>(muxMask);
        }
        else if (rule.mux >= 0 && rule.mux <= 7)
            rule.muxMask = 7;
        else if (rule.mux >= 8)
            rule.muxMask = 255;

        DashPluginResult parsedMatch = parseMatch(obj, rule);
        if (!parsedMatch.ok)
            return parsedMatch;

        if (!obj["send"].isNull())
        {
            if (!dashPluginReadBool(obj["send"], rule.send))
                return DashPluginResult::fail("send must be bool");
        }

        JsonVariantConst opsValue = obj["ops"];
        if (!opsValue.is<JsonArrayConst>())
            return DashPluginResult::fail("ops required");
        JsonArrayConst ops = opsValue.as<JsonArrayConst>();
        if (ops.size() == 0)
            return DashPluginResult::fail("ops required");
        if (ops.size() > kDashPluginMaxOps)
            return DashPluginResult::fail("too many ops");

        for (JsonVariantConst item : ops)
        {
            if (!item.is<JsonObjectConst>())
                return DashPluginResult::fail("op must be object");
            DashPluginOp op;
            DashPluginResult parsed = parseOp(rule, item.as<JsonObjectConst>(), op);
            if (!parsed.ok)
                return parsed;
            rule.ops.push_back(op);
        }

        return DashPluginResult::success();
    }

    DashPluginResult parseMatch(JsonObjectConst obj, DashPluginRule &rule) const
    {
        JsonVariantConst matchValue = obj["match"];
        JsonObjectConst match;
        if (!matchValue.isNull())
        {
            if (!matchValue.is<JsonObjectConst>())
                return DashPluginResult::fail("match must be object");
            match = matchValue.as<JsonObjectConst>();
        }

        JsonVariantConst byteValue = match.isNull() ? JsonVariantConst(obj["match_byte"]) : JsonVariantConst(match["byte"]);
        if (byteValue.isNull())
            byteValue = obj["matchByte"];

        JsonVariantConst maskValue = match.isNull() ? JsonVariantConst(obj["match_mask"]) : JsonVariantConst(match["mask"]);
        if (maskValue.isNull())
            maskValue = obj["matchMask"];
        JsonVariantConst valValue = match.isNull() ? JsonVariantConst(obj["match_val"]) : JsonVariantConst(match["val"]);
        if (valValue.isNull())
            valValue = obj["matchValue"];
        if (valValue.isNull())
            valValue = obj["match_value"];
        if (valValue.isNull())
            valValue = obj["matchVal"];

        if (byteValue.isNull())
        {
            if (!matchValue.isNull() || !maskValue.isNull() || !valValue.isNull())
                return DashPluginResult::fail("match byte required");
            return DashPluginResult::success();
        }

        int64_t byte = 0;
        if (!dashPluginReadInt(byteValue, 0, 7, byte))
            return DashPluginResult::fail("match byte must be integer in range");
        int64_t mask = 0;
        if (!dashPluginReadOptionalInt(maskValue, 0, 255, mask, 0))
            return DashPluginResult::fail("match mask must be integer in range");
        int64_t val = 0;
        if (!dashPluginReadOptionalInt(valValue, 0, 255, val, 0))
            return DashPluginResult::fail("match value must be integer in range");

        rule.matchByte = static_cast<uint8_t>(byte);
        rule.matchMask = static_cast<uint8_t>(mask);
        rule.matchVal = static_cast<uint8_t>(val);
        rule.hasMatch = rule.matchMask != 0;
        return DashPluginResult::success();
    }

    DashPluginResult parseOp(const DashPluginRule &rule, JsonObjectConst obj, DashPluginOp &op) const
    {
        const std::string type = dashPluginString(obj["type"]);
        if (type == "set_bit")
        {
            op.type = DashPluginOpType::SetBit;
            int64_t bit = 0;
            if (!dashPluginReadInt(obj["bit"], 0, 63, bit))
                return DashPluginResult::fail("set_bit bit must be integer in range");
            op.bit = static_cast<uint8_t>(bit);
            op.byte = static_cast<uint8_t>(bit / 8);
            if (obj["val"].isNull())
            {
                op.val = 1;
            }
            else if (obj["val"].is<bool>())
            {
                op.val = obj["val"].as<bool>() ? 1 : 0;
            }
            else
            {
                int64_t val = 0;
                if (!dashPluginReadInt(obj["val"], 0, 1, val))
                    return DashPluginResult::fail("set_bit val must be bool or 0/1");
                op.val = static_cast<uint8_t>(val);
            }
            return DashPluginResult::success();
        }
        if (type == "set_byte" || type == "or_byte" || type == "and_byte" || type == "counter")
        {
            int64_t byte = 0;
            if (!dashPluginReadInt(obj["byte"], 0, 7, byte))
                return DashPluginResult::fail("byte must be integer in range");
            op.byte = static_cast<uint8_t>(byte);

            int64_t val = 0;
            if (!dashPluginReadOptionalInt(obj["val"], 0, 255, val, 0))
                return DashPluginResult::fail("val must be integer in range");
            op.val = static_cast<uint8_t>(val);

            const bool isCounter = type == "counter";
            int64_t mask = 0;
            if (!dashPluginReadOptionalInt(obj["mask"], isCounter ? 1 : 0, 255, mask, isCounter ? 0x0F : 0xFF))
                return DashPluginResult::fail("mask must be integer in range");
            op.mask = static_cast<uint8_t>(mask);

            int64_t step = 0;
            if (!dashPluginReadOptionalInt(obj["step"], isCounter ? 1 : 0, 255, step, 1))
                return DashPluginResult::fail("step must be integer in range");
            op.step = static_cast<uint8_t>(step);

            if (type == "set_byte")
                op.type = DashPluginOpType::SetByte;
            else if (type == "or_byte")
                op.type = DashPluginOpType::OrByte;
            else if (type == "and_byte")
                op.type = DashPluginOpType::AndByte;
            else
                op.type = DashPluginOpType::Counter;
            return DashPluginResult::success();
        }
        if (type == "checksum")
        {
            op.type = DashPluginOpType::Checksum;
            return DashPluginResult::success();
        }
        if (type == "emit_periodic")
        {
            if (!(rule.id == 2047 && rule.mux == 3 && rule.send))
                return DashPluginResult::fail("emit_periodic requires GTW 2047 mux 3 send rule");
            op.type = DashPluginOpType::EmitPeriodic;
            int64_t interval = 100;
            if (!dashPluginReadOptionalInt(obj["interval"], INT64_MIN, INT64_MAX, interval, 100))
                return DashPluginResult::fail("interval must be integer");
            op.intervalMs = static_cast<uint16_t>(std::max<int64_t>(10, std::min<int64_t>(5000, interval)));
            if (!dashPluginReadOptionalBool(obj["gtw_silent"], op.gtwSilent, false))
                return DashPluginResult::fail("gtw_silent must be bool");
            return DashPluginResult::success();
        }
        return DashPluginResult::fail("unknown op");
    }

    DashPlugin *findPlugin(const char *name)
    {
        if (name == nullptr)
            return nullptr;
        auto it = std::find_if(plugins_.begin(), plugins_.end(), [&](const DashPlugin &plugin) { return plugin.name == name; });
        return it == plugins_.end() ? nullptr : &(*it);
    }

    const DashPlugin *findPluginConst(const char *name) const
    {
        if (name == nullptr)
            return nullptr;
        auto it = std::find_if(plugins_.begin(), plugins_.end(), [&](const DashPlugin &plugin) { return plugin.name == name; });
        return it == plugins_.end() ? nullptr : &(*it);
    }

    static bool pluginRequestsGtwSilent(const DashPlugin &plugin)
    {
        if (plugin.gtwSilentRequested)
            return true;
        for (const DashPluginRule &rule : plugin.rules)
            for (const DashPluginOp &op : rule.ops)
                if (op.gtwSilent)
                    return true;
        return false;
    }

    static void setOpsGtwSilent(DashPlugin &plugin, bool enabled)
    {
        for (DashPluginRule &rule : plugin.rules)
            for (DashPluginOp &op : rule.ops)
                if (op.type == DashPluginOpType::EmitPeriodic)
                    op.gtwSilent = enabled && op.gtwSilent;
    }

    static void finalizeGtwSilentAvailability(DashPlugin &plugin)
    {
        plugin.gtwSilentRequested = pluginRequestsGtwSilent(plugin);
        plugin.gtwSilentAvailable = true;
        if (!plugin.gtwSilentRequested)
            return;
#ifdef PLUGIN_GTW_UDS_CUSTOM_KEY
        plugin.gtwSilentAvailable = true;
#else
        plugin.gtwSilentAvailable = false;
        plugin.lastError = "custom key build required";
        plugin.warnings.push_back("custom key build required");
        setOpsGtwSilent(plugin, false);
#endif
    }

    bool hasUnavailableGtwSilentPlugin() const
    {
        for (const DashPlugin &plugin : plugins_)
            if (plugin.gtwSilentRequested && !plugin.gtwSilentAvailable)
                return true;
        return false;
    }

    void normalizePriorities(const char *preferredName)
    {
        if (plugins_.empty())
            return;

        const size_t count = plugins_.size();
        DashPlugin *preferred = findPlugin(preferredName);
        const uint8_t desired = preferred == nullptr
                                    ? 1
                                    : static_cast<uint8_t>(std::max<size_t>(1, std::min<size_t>(count, preferred->priority)));

        std::vector<std::pair<uint8_t, size_t>> ordered;
        ordered.reserve(count);
        for (size_t i = 0; i < count; ++i)
            ordered.push_back({plugins_[i].priority, i});
        std::stable_sort(ordered.begin(), ordered.end(), [](const auto &a, const auto &b) {
            if (a.first != b.first)
                return a.first < b.first;
            return a.second < b.second;
        });

        uint8_t next = 1;
        for (const auto &entry : ordered)
        {
            DashPlugin &plugin = plugins_[entry.second];
            if (preferred != nullptr && plugin.name == preferredName)
                continue;
            if (preferred != nullptr && next == desired)
                ++next;
            plugin.priority = next++;
        }
        if (preferred != nullptr)
            preferred->priority = desired;
    }

    std::vector<DashPlugin> plugins_;
    uint8_t replayCount_ = 1;
    bool periodicValid_ = false;
    CanFrame periodicFrame_{};
    DashPluginSendPlan periodicSendPlan_{};
    uint16_t periodicIntervalMs_ = 100;
    uint32_t periodicLastMs_ = 0;
    std::string periodicPluginName_;
    bool gtwSilentRequested_ = false;
};
