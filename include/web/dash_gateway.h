#pragma once

#if defined(ESP_PLATFORM) && defined(DASH_STA_AP_GATEWAY)

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <cstring>
#include <string>

#include <esp_log.h>
#include <esp_heap_caps.h>
#include <esp_netif.h>
#include <esp_netif_net_stack.h>
#include <nvs.h>
#include <apps/dhcpserver/dhcpserver.h>
#include <lwip/inet.h>
#include <lwip/lwip_napt.h>
#include <lwip/netif.h>
#include <fcntl.h>
#include <lwip/sockets.h>

static constexpr const char *kDashGatewayTag = "dash_gateway";
static constexpr const char *kDashGatewayPrefsNs = "gw";
static constexpr const char *kDashGatewayBlacklistPath = "/gw_black.txt";
static constexpr const char *kDashGatewayWhitelistPath = "/gw_white.txt";
static constexpr uint8_t kDashGatewayTeslaProfileVersion = 1;
static constexpr size_t kDashGatewayListMax = 3072;
static constexpr uint16_t kDashGatewayDnsTypeA = 1;
static constexpr uint16_t kDashGatewayDnsTypeAAAA = 28;
static constexpr uint16_t kDashGatewayDnsClassIN = 1;
static constexpr uint32_t kDashGatewayBlockedTtlSeconds = 60;
static constexpr size_t kDashGatewayMaxPending = 64;
static constexpr uint8_t kDashGatewayMaxPendingClients = 4;
static constexpr size_t kDashGatewayMaxWhitelistEntries = 200;
static constexpr size_t kDashGatewayMaxBlacklistEntries = 100;
static constexpr size_t kDashGatewayRuleMaxLen = 96;
static constexpr size_t kDashGatewayDnsCacheEntries = 128;
static constexpr size_t kDashGatewayDnsCacheRespMax = 512;
static constexpr uint32_t kDashGatewayDnsCacheTtlSec = 60;

struct DashGatewayBlockedDomain
{
    char domain[96];
    uint32_t count;
};

struct DashGatewayPendingQuery
{
    uint16_t originalId;
    uint16_t proxyId;
    uint16_t qtype;
    sockaddr_in clientAddr;
    uint16_t clientIds[kDashGatewayMaxPendingClients];
    sockaddr_in clients[kDashGatewayMaxPendingClients];
    uint8_t clientCount;
    uint32_t rulesVersion;
    TickType_t startTime;
    char domain[256];
    bool inUse;
};

struct DashGatewayDomainRule
{
    char domain[kDashGatewayRuleMaxLen];
    uint8_t len;
};

struct DashGatewayDnsCacheEntry
{
    char domain[128];
    uint16_t qtype;
    uint16_t respLen;
    uint32_t expiresSec;
    uint32_t lastUsedSec;
    uint8_t resp[kDashGatewayDnsCacheRespMax];
};

static bool gatewayEnabled = true;
static String gatewayDnsBlacklist;
static String gatewayDnsWhitelist;
static uint32_t gatewayUpstreamDns = IPADDR_NONE;
static uint32_t gatewayDhcpDns = IPADDR_NONE;
static uint8_t gatewayUpstreamDnsMode = 0; // 0=DHCP/auto, 1=Ali, 2=Tencent, 3=custom
static uint32_t gatewayCustomUpstreamDns = IPADDR_NONE;
static TaskHandle_t gatewayDnsTaskHandle = nullptr;
static int gatewayDnsSock = -1;
static int gatewayUpstreamSock = -1;
static bool gatewayDnsBindOk = false; // true once DNS socket bound to UDP 53
static uint16_t gatewayNextProxyId = 0;
static bool gatewayNaptEnabled = false;
static DashGatewayBlockedDomain *gatewayBlockedDomains = nullptr;
static bool gatewayBlockedDomainsInPsram = false;
static uint8_t gatewayBlockedDomainCount = 0;
static portMUX_TYPE gatewayBlockedMux = portMUX_INITIALIZER_UNLOCKED;
static DashGatewayPendingQuery *gatewayPendingQueries = nullptr;
static bool gatewayPendingQueriesInPsram = false;
static DashGatewayDomainRule *gatewayBlacklistRules = nullptr;
static DashGatewayDomainRule *gatewayWhitelistRules = nullptr;
static bool gatewayBlacklistRulesInPsram = false;
static bool gatewayWhitelistRulesInPsram = false;
static uint16_t gatewayBlacklistRuleCount = 0;
static uint16_t gatewayWhitelistRuleCount = 0;
static DashGatewayDnsCacheEntry *gatewayDnsCache = nullptr;
static bool gatewayDnsCacheInPsram = false;
static uint32_t gatewayDnsCacheHits = 0;
static uint32_t gatewayDnsCacheMisses = 0;
static uint32_t gatewayDnsLatencyLastMs = 0;
static uint32_t gatewayDnsLatencyAvgMs = 0;
static uint32_t gatewayDnsLatencyMaxMs = 0;
static uint32_t gatewayDnsSlow500Ms = 0;
static uint32_t gatewayDnsSlow1000Ms = 0;
static uint32_t gatewayDnsSlow2000Ms = 0;
static uint16_t gatewayDnsPendingMax = 0;
static uint32_t gatewayDnsPendingFull = 0;
static uint32_t gatewayDnsTimeouts = 0;
static uint32_t gatewayDnsUpstreamFails = 0;
static uint32_t gatewayDnsRulesVersion = 1;

static constexpr uint8_t kDashGatewayUpstreamAuto = 0;
static constexpr uint8_t kDashGatewayUpstreamAli = 1;
static constexpr uint8_t kDashGatewayUpstreamTencent = 2;
static constexpr uint8_t kDashGatewayUpstreamCustom = 3;

static uint32_t dashGatewaySelectedUpstreamDns();

static const char kDashGatewayDefaultBlacklist[] =
    "tesla.cn\n"
    "tesla.com\n"
    "teslamotors.com\n"
    "tesla.services";

static const char kDashGatewayDefaultWhitelist[] =
    "connman.vn.cloud.tesla.cn\n"
    "nav-prd-maps.tesla.cn\n"
    "maps-cn-prd.go.tesla.services\n"
    "signaling.vn.cloud.tesla.cn\n"
    "api-prd.vn.cloud.tesla.cn\n"
    "media-server-me.tesla.cn";

template <typename T>
static bool dashGatewayAllocateArray(T *&target, size_t count, const char *name, bool &inPsram)
{
    if (target)
        return true;
    inPsram = false;
#if defined(CONFIG_SPIRAM) && CONFIG_SPIRAM
    void *mem = heap_caps_calloc(count, sizeof(T), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (mem)
    {
        target = static_cast<T *>(mem);
        inPsram = true;
        ESP_LOGI(kDashGatewayTag, "%s cache allocated in PSRAM (%u bytes)",
                 name, static_cast<unsigned>(count * sizeof(T)));
    }
    else
    {
        ESP_LOGW(kDashGatewayTag, "%s PSRAM allocation failed; using internal RAM fallback", name);
    }
#endif
    if (!target)
        target = static_cast<T *>(heap_caps_calloc(count, sizeof(T), MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT));
    if (!target)
    {
        ESP_LOGE(kDashGatewayTag, "%s cache allocation failed (%u bytes)",
                 name, static_cast<unsigned>(count * sizeof(T)));
        return false;
    }
    return true;
}

static bool dashGatewayAllocateState()
{
    static bool allocated = false;
    if (allocated)
        return true;
    bool ok = true;
    ok = dashGatewayAllocateArray(gatewayBlockedDomains, 32, "blocked domain", gatewayBlockedDomainsInPsram) && ok;
    ok = dashGatewayAllocateArray(gatewayPendingQueries, kDashGatewayMaxPending, "pending DNS query", gatewayPendingQueriesInPsram) && ok;
    ok = dashGatewayAllocateArray(gatewayBlacklistRules, kDashGatewayMaxBlacklistEntries, "blacklist rule", gatewayBlacklistRulesInPsram) && ok;
    ok = dashGatewayAllocateArray(gatewayWhitelistRules, kDashGatewayMaxWhitelistEntries, "whitelist rule", gatewayWhitelistRulesInPsram) && ok;
    ok = dashGatewayAllocateArray(gatewayDnsCache, kDashGatewayDnsCacheEntries, "DNS response", gatewayDnsCacheInPsram) && ok;
    allocated = ok;
    return ok;
}

#define DASH_GATEWAY_FOR_PENDING(q)                                     \
    for (uint8_t q##Idx = 0; q##Idx < kDashGatewayMaxPending; q##Idx++) \
        if (auto &q = gatewayPendingQueries[q##Idx]; true)

static String dashGatewayTrim(String s)
{
    std::string v = static_cast<std::string>(s);
    size_t a = 0;
    while (a < v.size() && std::isspace(static_cast<unsigned char>(v[a])))
        a++;
    size_t b = v.size();
    while (b > a && std::isspace(static_cast<unsigned char>(v[b - 1])))
        b--;
    return v.substr(a, b - a);
}

static String dashGatewayLower(String s)
{
    std::string v = static_cast<std::string>(s);
    std::transform(v.begin(), v.end(), v.begin(), [](unsigned char c)
                   { return static_cast<char>(std::tolower(c)); });
    return v;
}

static String dashGatewayNormalizeDomain(const String &domain)
{
    String d = dashGatewayLower(dashGatewayTrim(domain));
    while (d.endsWith("."))
        d = d.substring(0, d.length() - 1);
    if (d.startsWith("*."))
        d = d.substring(2);
    return d;
}

static uint16_t dashGatewayReadU16(const uint8_t *ptr)
{
    return static_cast<uint16_t>((ptr[0] << 8) | ptr[1]);
}

static void dashGatewayWriteU16(uint8_t *ptr, uint16_t value)
{
    ptr[0] = static_cast<uint8_t>((value >> 8) & 0xff);
    ptr[1] = static_cast<uint8_t>(value & 0xff);
}

static void dashGatewayWriteU32(uint8_t *ptr, uint32_t value)
{
    ptr[0] = static_cast<uint8_t>((value >> 24) & 0xff);
    ptr[1] = static_cast<uint8_t>((value >> 16) & 0xff);
    ptr[2] = static_cast<uint8_t>((value >> 8) & 0xff);
    ptr[3] = static_cast<uint8_t>(value & 0xff);
}

static bool dashGatewayDomainMatchesRule(const String &domain, const String &rule)
{
    if (domain.length() == 0 || rule.length() == 0)
        return false;
    if (domain == rule)
        return true;
    return domain.length() > rule.length() && domain.endsWith(rule.c_str()) &&
           domain[domain.length() - rule.length() - 1] == '.';
}

static bool dashGatewayDomainMatchesCompiledRule(const char *domain, size_t domainLen, const DashGatewayDomainRule &rule)
{
    if (!domain || domainLen == 0 || rule.len == 0)
        return false;
    if (domainLen == rule.len)
        return std::memcmp(domain, rule.domain, rule.len) == 0;
    return domainLen > rule.len && domain[domainLen - rule.len - 1] == '.' &&
           std::memcmp(domain + domainLen - rule.len, rule.domain, rule.len) == 0;
}

static size_t dashGatewayCompiledRuleMatchLen(const String &domain, const DashGatewayDomainRule *rules, uint16_t count)
{
    String d = dashGatewayNormalizeDomain(domain);
    const char *dc = d.c_str();
    size_t dl = d.length();
    size_t best = 0;
    for (uint16_t i = 0; i < count; i++)
    {
        if (dashGatewayDomainMatchesCompiledRule(dc, dl, rules[i]) && rules[i].len > best)
            best = rules[i].len;
    }
    return best;
}

static void dashGatewayCompileList(const String &list, DashGatewayDomainRule *rules, uint16_t maxRules, uint16_t &outCount)
{
    outCount = 0;
    if (!rules || maxRules == 0)
        return;
    std::memset(rules, 0, sizeof(DashGatewayDomainRule) * maxRules);
    std::string all = static_cast<std::string>(list);
    size_t start = 0;
    while (start <= all.size() && outCount < maxRules)
    {
        while (start < all.size())
        {
            unsigned char c = static_cast<unsigned char>(all[start]);
            if (!std::isspace(c) && all[start] != ',' && all[start] != ';')
                break;
            start++;
        }
        if (start >= all.size())
            break;
        size_t end = start;
        while (end < all.size())
        {
            unsigned char c = static_cast<unsigned char>(all[end]);
            if (std::isspace(c) || all[end] == ',' || all[end] == ';')
                break;
            end++;
        }
        String rule = dashGatewayNormalizeDomain(all.substr(start, end - start));
        size_t len = std::min(static_cast<size_t>(rule.length()), kDashGatewayRuleMaxLen - 1);
        if (len > 0)
        {
            std::snprintf(rules[outCount].domain, sizeof(rules[outCount].domain), "%.*s",
                          static_cast<int>(len), rule.c_str());
            rules[outCount].len = static_cast<uint8_t>(len);
            outCount++;
        }
        start = end + 1;
    }
}

static void dashGatewayCompileRules()
{
    dashGatewayCompileList(gatewayDnsBlacklist, gatewayBlacklistRules, kDashGatewayMaxBlacklistEntries, gatewayBlacklistRuleCount);
    dashGatewayCompileList(gatewayDnsWhitelist, gatewayWhitelistRules, kDashGatewayMaxWhitelistEntries, gatewayWhitelistRuleCount);
}

static void dashGatewayCompileAllRules()
{
    dashGatewayCompileRules();
}

static void dashGatewayTrackBlocked(const String &domain)
{
    String d = dashGatewayNormalizeDomain(domain);
    if (d.length() == 0)
        return;
    portENTER_CRITICAL(&gatewayBlockedMux);
    for (uint8_t i = 0; i < gatewayBlockedDomainCount; i++)
    {
        if (d == gatewayBlockedDomains[i].domain)
        {
            gatewayBlockedDomains[i].count++;
            portEXIT_CRITICAL(&gatewayBlockedMux);
            return;
        }
    }
    uint8_t slot = gatewayBlockedDomainCount < 32 ? gatewayBlockedDomainCount++ : 31;
    std::snprintf(gatewayBlockedDomains[slot].domain, sizeof(gatewayBlockedDomains[slot].domain), "%s", d.c_str());
    gatewayBlockedDomains[slot].count = 1;
    portEXIT_CRITICAL(&gatewayBlockedMux);
}

static size_t dashGatewayDomainRuleMatchLen(const String &domain, const String &list)
{
    String d = dashGatewayNormalizeDomain(domain);
    std::string all = static_cast<std::string>(list);
    size_t best = 0;
    size_t start = 0;
    while (start <= all.size())
    {
        while (start < all.size())
        {
            unsigned char c = static_cast<unsigned char>(all[start]);
            if (!std::isspace(c) && all[start] != ',' && all[start] != ';')
                break;
            start++;
        }
        if (start >= all.size())
            break;
        size_t end = start;
        while (end < all.size())
        {
            unsigned char c = static_cast<unsigned char>(all[end]);
            if (std::isspace(c) || all[end] == ',' || all[end] == ';')
                break;
            end++;
        }
        std::string item = all.substr(start, end == std::string::npos ? std::string::npos : end - start);
        String rule = dashGatewayNormalizeDomain(item);
        if (dashGatewayDomainMatchesRule(d, rule) && rule.length() > best)
            best = rule.length();
        start = end + 1;
    }
    return best;
}

static bool dashGatewayDomainInList(const String &domain, const String &list)
{
    return dashGatewayDomainRuleMatchLen(domain, list) > 0;
}

static bool dashGatewayDomainAllowedForWhitelist(const String &domain)
{
    String d = dashGatewayNormalizeDomain(domain);
    if (d.length() == 0)
        return false;
    // Allow specific child domains as exceptions under a blocked root domain,
    // but reject a whitelist rule that exactly re-opens the blocked root.
    size_t blockLen = dashGatewayDomainRuleMatchLen(d, gatewayDnsBlacklist);
    return blockLen == 0 || blockLen < d.length();
}

static size_t dashGatewayCountEntries(const String &list)
{
    std::string all = static_cast<std::string>(list);
    size_t count = 0;
    size_t start = 0;
    while (start < all.size())
    {
        while (start < all.size())
        {
            unsigned char c = static_cast<unsigned char>(all[start]);
            if (!std::isspace(c) && all[start] != ',' && all[start] != ';')
                break;
            start++;
        }
        if (start >= all.size())
            break;
        size_t end = start;
        while (end < all.size())
        {
            unsigned char c = static_cast<unsigned char>(all[end]);
            if (std::isspace(c) || all[end] == ',' || all[end] == ';')
                break;
            end++;
        }
        if (end > start)
            count++;
        start = end + 1;
    }
    return count;
}

static bool dashGatewayAppendWhitelist(const String &domain)
{
    String d = dashGatewayNormalizeDomain(domain);
    if (!dashGatewayDomainAllowedForWhitelist(d) || dashGatewayDomainInList(d, gatewayDnsWhitelist))
        return false;
    if (dashGatewayCountEntries(gatewayDnsWhitelist) >= kDashGatewayMaxWhitelistEntries)
        return false;
    String next = gatewayDnsWhitelist;
    if (next.length() > 0 && !next.endsWith("\n"))
        next += "\n";
    next += d;
    if (next.length() > kDashGatewayListMax)
        return false;
    gatewayDnsWhitelist = next;
    return true;
}

static String dashGatewaySanitizeWhitelist(const String &list)
{
    String out;
    size_t entryCount = 0;
    std::string all = static_cast<std::string>(list);
    size_t start = 0;
    while (start <= all.size() && entryCount < kDashGatewayMaxWhitelistEntries)
    {
        while (start < all.size())
        {
            unsigned char c = static_cast<unsigned char>(all[start]);
            if (!std::isspace(c) && all[start] != ',' && all[start] != ';')
                break;
            start++;
        }
        if (start >= all.size())
            break;
        size_t end = start;
        while (end < all.size())
        {
            unsigned char c = static_cast<unsigned char>(all[end]);
            if (std::isspace(c) || all[end] == ',' || all[end] == ';')
                break;
            end++;
        }
        String rule = dashGatewayNormalizeDomain(all.substr(start, end - start));
        if (rule.length() > 0 && dashGatewayDomainAllowedForWhitelist(rule) && !dashGatewayDomainInList(rule, out))
        {
            if (out.length() > 0)
                out += "\n";
            out += rule;
            entryCount++;
        }
        start = end + 1;
    }
    return out.substring(0, kDashGatewayListMax);
}

static String dashGatewaySanitizeBlacklist(const String &list)
{
    String out;
    size_t entryCount = 0;
    std::string all = static_cast<std::string>(list);
    size_t start = 0;
    while (start <= all.size() && entryCount < kDashGatewayMaxBlacklistEntries)
    {
        while (start < all.size())
        {
            unsigned char c = static_cast<unsigned char>(all[start]);
            if (!std::isspace(c) && all[start] != ',' && all[start] != ';')
                break;
            start++;
        }
        if (start >= all.size())
            break;
        size_t end = start;
        while (end < all.size())
        {
            unsigned char c = static_cast<unsigned char>(all[end]);
            if (std::isspace(c) || all[end] == ',' || all[end] == ';')
                break;
            end++;
        }
        String rule = dashGatewayNormalizeDomain(all.substr(start, end - start));
        if (rule.length() > 0 && !dashGatewayDomainInList(rule, out))
        {
            if (out.length() > 0)
                out += "\n";
            out += rule;
            entryCount++;
        }
        start = end + 1;
    }
    return out.substring(0, kDashGatewayListMax);
}

static bool dashGatewayHardcodedWhitelist(const String &domain)
{
    // *.cdnhwcaoc115.cn (Alibaba Cloud CDN)
    if (dashGatewayDomainMatchesRule(domain, "cdnhwcaoc115.cn"))
        return true;
    // sdk.51.la
    if (domain == "sdk.51.la")
        return true;
    return false;
}

static bool dashGatewayDnsAllowed(const String &domain)
{
    String d = dashGatewayNormalizeDomain(domain);
    if (d.length() == 0)
        return false;
    // Hardcoded whitelist always passes
    if (dashGatewayHardcodedWhitelist(d))
        return true;
    size_t blockLen = dashGatewayCompiledRuleMatchLen(d, gatewayBlacklistRules, gatewayBlacklistRuleCount);
    size_t allowLen = dashGatewayCompiledRuleMatchLen(d, gatewayWhitelistRules, gatewayWhitelistRuleCount);
    if (allowLen > 0 && allowLen >= blockLen)
        return true;
    if (blockLen > 0)
        return false;
    return true;
}

static String dashGatewayDnsDecisionJson(const String &input)
{
    String domain = dashGatewayNormalizeDomain(input);
    size_t blockLen = dashGatewayCompiledRuleMatchLen(domain, gatewayBlacklistRules, gatewayBlacklistRuleCount);
    size_t allowLen = dashGatewayCompiledRuleMatchLen(domain, gatewayWhitelistRules, gatewayWhitelistRuleCount);
    bool blacklisted = blockLen > 0;
    bool whitelisted = allowLen > 0;
    bool allowed = domain.length() > 0 && dashGatewayDnsAllowed(domain);
    String j = "{\"domain\":\"";
    j += jsonEscape(domain.c_str());
    j += "\",\"enabled\":";
    j += gatewayEnabled ? "true" : "false";
    j += ",\"allowed\":";
    j += allowed ? "true" : "false";
    j += ",\"blocked\":";
    j += (!allowed && gatewayEnabled && domain.length() > 0) ? "true" : "false";
    j += ",\"blacklisted\":";
    j += blacklisted ? "true" : "false";
    j += ",\"whitelisted\":";
    j += whitelisted ? "true" : "false";
    j += ",\"reason\":\"";
    if (!gatewayEnabled)
        j += "gateway disabled";
    else if (domain.length() == 0)
        j += "empty domain";
    else if (whitelisted && blacklisted && allowLen >= blockLen)
        j += "whitelist override blacklist";
    else
        j += blacklisted ? "matched blacklist" : "not in blacklist";
    j += "\"}";
    return j;
}

__attribute__((unused)) static bool dashGatewayParseDnsName(const uint8_t *buf, size_t len, String &out)
{
    if (len < 13)
        return false;
    size_t pos = 12;
    std::string name;
    while (pos < len)
    {
        uint8_t n = buf[pos++];
        if (n == 0)
            break;
        if ((n & 0xC0) != 0 || n > 63 || pos + n > len)
            return false;
        if (!name.empty())
            name += '.';
        name.append(reinterpret_cast<const char *>(buf + pos), n);
        pos += n;
    }
    if (name.empty())
        return false;
    out = name;
    return true;
}

static bool dashGatewayParseDnsQuestion(const uint8_t *buf, size_t len, String &name, uint16_t &qtype, size_t *questionEnd = nullptr)
{
    if (len < 16)
        return false;
    size_t pos = 12;
    std::string out;
    while (pos < len)
    {
        uint8_t n = buf[pos++];
        if (n == 0)
            break;
        if ((n & 0xC0) != 0 || n > 63 || pos + n > len)
            return false;
        if (!out.empty())
            out += '.';
        out.append(reinterpret_cast<const char *>(buf + pos), n);
        pos += n;
    }
    if (out.empty() || pos + 4 > len)
        return false;
    qtype = dashGatewayReadU16(buf + pos);
    if (questionEnd)
        *questionEnd = pos + 4;
    name = out;
    return true;
}

static void dashGatewayDnsCacheClear()
{
    if (!gatewayDnsCache)
        return;
    std::memset(gatewayDnsCache, 0, sizeof(DashGatewayDnsCacheEntry) * kDashGatewayDnsCacheEntries);
}

static size_t dashGatewayDnsCacheLookup(const String &domain, uint16_t qtype, uint32_t nowSec, uint8_t *out, size_t outCap)
{
    if (!gatewayDnsCache || domain.length() == 0)
        return 0;
    String d = dashGatewayNormalizeDomain(domain);
    for (size_t i = 0; i < kDashGatewayDnsCacheEntries; i++)
    {
        DashGatewayDnsCacheEntry &e = gatewayDnsCache[i];
        if (e.expiresSec == 0)
            continue;
        if (static_cast<int32_t>(e.expiresSec - nowSec) <= 0)
        {
            e.expiresSec = 0;
            continue;
        }
        if (e.qtype != qtype || e.respLen == 0 || e.respLen > outCap)
            continue;
        if (std::strcmp(e.domain, d.c_str()) != 0)
            continue;
        std::memcpy(out, e.resp, e.respLen);
        e.lastUsedSec = nowSec;
        gatewayDnsCacheHits++;
        return e.respLen;
    }
    gatewayDnsCacheMisses++;
    return 0;
}

static void dashGatewayDnsCachePut(const String &domain, uint16_t qtype, uint32_t nowSec, const uint8_t *resp, size_t respLen)
{
    if (!gatewayDnsCache || domain.length() == 0 || !resp || respLen < 12 || respLen > kDashGatewayDnsCacheRespMax)
        return;
    // Cache successful answers only. NXDOMAIN/REFUSED and empty answers should
    // reflect live policy/upstream state rather than linger in the bridge.
    if ((resp[3] & 0x0F) != 0 || dashGatewayReadU16(resp + 6) == 0)
        return;
    String d = dashGatewayNormalizeDomain(domain);
    size_t target = kDashGatewayDnsCacheEntries;
    uint32_t oldest = 0xFFFFFFFFu;
    for (size_t i = 0; i < kDashGatewayDnsCacheEntries; i++)
    {
        DashGatewayDnsCacheEntry &e = gatewayDnsCache[i];
        if (e.expiresSec == 0)
        {
            target = i;
            break;
        }
        if (e.qtype == qtype && std::strcmp(e.domain, d.c_str()) == 0)
        {
            target = i;
            break;
        }
        if (e.lastUsedSec < oldest)
        {
            oldest = e.lastUsedSec;
            target = i;
        }
    }
    if (target >= kDashGatewayDnsCacheEntries)
        return;
    DashGatewayDnsCacheEntry &e = gatewayDnsCache[target];
    std::memset(&e, 0, sizeof(e));
    std::snprintf(e.domain, sizeof(e.domain), "%s", d.c_str());
    e.qtype = qtype;
    e.respLen = static_cast<uint16_t>(respLen);
    e.expiresSec = nowSec + kDashGatewayDnsCacheTtlSec;
    e.lastUsedSec = nowSec;
    std::memcpy(e.resp, resp, respLen);
}

static bool dashGatewayPendingAddClient(DashGatewayPendingQuery &q, uint16_t origId, const sockaddr_in &client)
{
    if (q.clientCount >= kDashGatewayMaxPendingClients)
        return false;
    q.clientIds[q.clientCount] = origId;
    q.clients[q.clientCount] = client;
    q.clientCount++;
    return true;
}

static bool dashGatewayAttachDuplicatePending(const String &domain, uint16_t qtype, uint16_t origId, const sockaddr_in &client)
{
    String d = dashGatewayNormalizeDomain(domain);
    DASH_GATEWAY_FOR_PENDING(q)
    {
        if (!q.inUse || q.rulesVersion != gatewayDnsRulesVersion)
            continue;
        if (q.qtype == qtype && std::strcmp(q.domain, d.c_str()) == 0)
            return dashGatewayPendingAddClient(q, origId, client);
    }
    return false;
}

static void dashGatewayInitPending(DashGatewayPendingQuery &q, uint16_t origId, uint16_t proxyId, uint16_t qtype,
                                   const sockaddr_in *client, const String &domain)
{
    std::memset(&q, 0, sizeof(q));
    q.originalId = origId;
    q.proxyId = proxyId;
    q.qtype = qtype;
    q.startTime = xTaskGetTickCount();
    q.inUse = true;
    q.rulesVersion = gatewayDnsRulesVersion;
    String d = dashGatewayNormalizeDomain(domain);
    std::snprintf(q.domain, sizeof(q.domain), "%s", d.c_str());
    if (client)
    {
        q.clientAddr = *client;
        dashGatewayPendingAddClient(q, origId, *client);
    }
}

static uint16_t dashGatewayPendingCount()
{
    uint16_t count = 0;
    DASH_GATEWAY_FOR_PENDING(q)
    if (q.inUse)
        count++;
    return count;
}

static void dashGatewayUpdatePendingMax()
{
    uint16_t count = dashGatewayPendingCount();
    if (count > gatewayDnsPendingMax)
        gatewayDnsPendingMax = count;
}

static bool dashGatewayStorePending(uint16_t origId, uint16_t proxyId, uint16_t qtype,
                                    const sockaddr_in *client, const String &domain)
{
    DASH_GATEWAY_FOR_PENDING(q)
    {
        if (!q.inUse)
        {
            dashGatewayInitPending(q, origId, proxyId, qtype, client, domain);
            dashGatewayUpdatePendingMax();
            return true;
        }
    }
    gatewayDnsPendingFull++;
    return false;
}

static void dashGatewayClearPending()
{
    DASH_GATEWAY_FOR_PENDING(q)
    q.inUse = false;
}

static void dashGatewayTrackLatency(const DashGatewayPendingQuery &q, TickType_t nowTick)
{
    uint32_t elapsedMs = static_cast<uint32_t>((nowTick - q.startTime) * portTICK_PERIOD_MS);
    gatewayDnsLatencyLastMs = elapsedMs;
    gatewayDnsLatencyAvgMs = gatewayDnsLatencyAvgMs == 0
                                 ? elapsedMs
                                 : static_cast<uint32_t>((gatewayDnsLatencyAvgMs * 7UL + elapsedMs) / 8UL);
    if (elapsedMs > gatewayDnsLatencyMaxMs)
        gatewayDnsLatencyMaxMs = elapsedMs;
    if (elapsedMs > 500)
        gatewayDnsSlow500Ms++;
    if (elapsedMs > 1000)
        gatewayDnsSlow1000Ms++;
    if (elapsedMs > 2000)
        gatewayDnsSlow2000Ms++;
}

static size_t dashGatewayMakeDnsBlockedReply(const uint8_t *query, size_t qlen, uint8_t *reply, size_t cap)
{
    if (qlen < 12)
        return 0;

    size_t pos = 12;
    while (pos < qlen)
    {
        uint8_t n = query[pos++];
        if (n == 0)
            break;
        if ((n & 0xC0) != 0 || n > 63 || pos + n > qlen)
            return 0;
        pos += n;
    }
    if (pos + 4 > qlen)
        return 0;

    const size_t questionEnd = pos + 4;
    const uint16_t qType = dashGatewayReadU16(query + pos);
    const uint16_t flags = dashGatewayReadU16(query + 2);
    const uint16_t responseFlags = static_cast<uint16_t>(0x8000 | 0x0080 | (flags & 0x0100));
    const bool answerA = qType == kDashGatewayDnsTypeA;
    const size_t answerLen = answerA ? 16 : 0;
    const size_t outLen = questionEnd + answerLen;
    if (outLen > cap)
        return 0;

    std::memcpy(reply, query, questionEnd);
    dashGatewayWriteU16(reply + 2, responseFlags);
    dashGatewayWriteU16(reply + 4, 1);
    dashGatewayWriteU16(reply + 6, answerA ? 1 : 0);
    dashGatewayWriteU16(reply + 8, 0);
    dashGatewayWriteU16(reply + 10, 0);

    if (!answerA)
        return outLen;

    size_t offset = questionEnd;
    reply[offset++] = 0xC0;
    reply[offset++] = 0x0C;
    dashGatewayWriteU16(reply + offset, kDashGatewayDnsTypeA);
    offset += 2;
    dashGatewayWriteU16(reply + offset, kDashGatewayDnsClassIN);
    offset += 2;
    dashGatewayWriteU32(reply + offset, kDashGatewayBlockedTtlSeconds);
    offset += 4;
    dashGatewayWriteU16(reply + offset, 4);
    offset += 2;
    reply[offset++] = 0;
    reply[offset++] = 0;
    reply[offset++] = 0;
    reply[offset++] = 0;
    return outLen;
}

static size_t dashGatewayMakeDnsFakeReply(const uint8_t *query, size_t qlen, uint8_t *reply, size_t cap, uint32_t fakeIp)
{
    if (qlen < 12 || qlen + 16 > cap)
        return 0;
    std::memcpy(reply, query, qlen);
    reply[2] = 0x81;
    reply[3] = 0x80;
    reply[6] = 0x00;
    reply[7] = 0x01;
    reply[8] = reply[9] = reply[10] = reply[11] = 0;
    reply[qlen] = 0xC0;
    reply[qlen + 1] = 0x0C;
    reply[qlen + 2] = 0x00;
    reply[qlen + 3] = 0x01;
    reply[qlen + 4] = 0x00;
    reply[qlen + 5] = 0x01;
    reply[qlen + 6] = reply[qlen + 7] = reply[qlen + 8] = reply[qlen + 9] = 0;
    reply[qlen + 10] = 0x00;
    reply[qlen + 11] = 0x04;
    const auto *ip = reinterpret_cast<const uint8_t *>(&fakeIp);
    reply[qlen + 12] = ip[0];
    reply[qlen + 13] = ip[1];
    reply[qlen + 14] = ip[2];
    reply[qlen + 15] = ip[3];
    return qlen + 16;
}

extern "C" int dashGatewayHookIp4CanForward(unsigned int destAddrNbo)
{
    (void)destAddrNbo;
    // DNS filtering is intentionally domain-only: blacklist root domains are
    // blocked in the DNS proxy, whitelist subdomains override those roots, and
    // NAT forwarding stays on the fastest path with no per-packet IP filtering.
    return 1;
}

static void dashGatewayDnsTask(void *)
{
    uint8_t rx[512];
    uint8_t tx[512];

    gatewayUpstreamSock = socket(AF_INET, SOCK_DGRAM, IPPROTO_IP);
    if (gatewayUpstreamSock < 0)
    {
        ESP_LOGW(kDashGatewayTag, "DNS upstream socket failed");
        vTaskDelete(nullptr);
        return;
    }
    int flags = fcntl(gatewayUpstreamSock, F_GETFL, 0);
    fcntl(gatewayUpstreamSock, F_SETFL, flags | O_NONBLOCK);

    DASH_GATEWAY_FOR_PENDING(q)
    q.inUse = false;

    TickType_t lastCleanup = xTaskGetTickCount();

    for (;;)
    {
        fd_set rfds;
        FD_ZERO(&rfds);
        FD_SET(gatewayDnsSock, &rfds);
        FD_SET(gatewayUpstreamSock, &rfds);
        int maxFd = gatewayDnsSock > gatewayUpstreamSock ? gatewayDnsSock : gatewayUpstreamSock;
        timeval tv = {};
        tv.tv_sec = 1;
        int ret = select(maxFd + 1, &rfds, nullptr, nullptr, &tv);
        if (ret < 0)
            continue;

        // Periodic cleanup of timed-out pending queries
        if (xTaskGetTickCount() - lastCleanup > pdMS_TO_TICKS(1000))
        {
            TickType_t now = xTaskGetTickCount();
            DASH_GATEWAY_FOR_PENDING(q)
            if (q.inUse && (int32_t)(now - q.startTime) > (int32_t)pdMS_TO_TICKS(5000))
            {
                gatewayDnsTimeouts++;
                q.inUse = false;
            }
            lastCleanup = xTaskGetTickCount();
        }

        if (FD_ISSET(gatewayDnsSock, &rfds))
        {
            sockaddr_in client = {};
            socklen_t clientLen = sizeof(client);
            int n = recvfrom(gatewayDnsSock, rx, sizeof(rx), 0, reinterpret_cast<sockaddr *>(&client), &clientLen);
            if (n >= 12)
            {
                String qname;
                uint16_t qtype = 0;
                bool parsed = dashGatewayParseDnsQuestion(rx, n, qname, qtype);

                // IPv6 forwarding/filtering is intentionally not supported:
                // answer AAAA locally with no data so clients fall back to A.
                if (parsed && qtype == kDashGatewayDnsTypeAAAA)
                {
                    size_t len = dashGatewayMakeDnsBlockedReply(rx, n, tx, sizeof(tx));
                    if (len > 0)
                        sendto(gatewayDnsSock, tx, len, 0, reinterpret_cast<sockaddr *>(&client), clientLen);
                    continue;
                }

                // Special domain t.sl -> fake IP 100.100.1.1
                if (parsed && n >= 16 && (qname == "t.sl" || qname == "t.sl."))
                {
                    if (qtype == kDashGatewayDnsTypeA)
                    {
                        ESP_LOGI(kDashGatewayTag, "Fake response for %s -> 100.100.1.1", qname.c_str());
                        uint32_t fakeIp = PP_HTONL(LWIP_MAKEU32(100, 100, 1, 1));
                        size_t len = dashGatewayMakeDnsFakeReply(rx, n, tx, sizeof(tx), fakeIp);
                        if (len > 0)
                            sendto(gatewayDnsSock, tx, len, 0, reinterpret_cast<sockaddr *>(&client), clientLen);
                        continue;
                    }
                }

                bool allowed = !gatewayEnabled || !parsed || dashGatewayDnsAllowed(qname);
                if (!allowed)
                {
                    dashGatewayTrackBlocked(qname);
                    size_t len = dashGatewayMakeDnsBlockedReply(rx, n, tx, sizeof(tx));
                    if (len > 0)
                        sendto(gatewayDnsSock, tx, len, 0, reinterpret_cast<sockaddr *>(&client), clientLen);
                }
                else
                {
                    uint16_t origId = static_cast<uint16_t>((rx[0] << 8) | rx[1]);
                    if (parsed)
                    {
                        uint32_t nowSec = static_cast<uint32_t>(esp_timer_get_time() / 1000000ULL);
                        size_t cachedLen = dashGatewayDnsCacheLookup(qname, qtype, nowSec, tx, sizeof(tx));
                        if (cachedLen > 0)
                        {
                            tx[0] = origId >> 8;
                            tx[1] = origId & 0xFF;
                            sendto(gatewayDnsSock, tx, cachedLen, 0, reinterpret_cast<sockaddr *>(&client), clientLen);
                            continue;
                        }
                    }
                    if (parsed && dashGatewayAttachDuplicatePending(qname, qtype, origId, client))
                        continue;
                    uint16_t proxyId = gatewayNextProxyId++;
                    rx[0] = proxyId >> 8;
                    rx[1] = proxyId & 0xFF;
                    uint32_t upstream = gatewayUpstreamDns != IPADDR_NONE ? gatewayUpstreamDns : dashGatewaySelectedUpstreamDns();
                    sockaddr_in dst = {};
                    dst.sin_family = AF_INET;
                    dst.sin_port = htons(53);
                    dst.sin_addr.s_addr = upstream;
                    ssize_t sent = sendto(gatewayUpstreamSock, rx, n, 0, reinterpret_cast<sockaddr *>(&dst), sizeof(dst));
                    if (sent == n)
                        dashGatewayStorePending(origId, proxyId, qtype, &client, qname);
                    else
                        gatewayDnsUpstreamFails++;
                }
            }
        }

        if (FD_ISSET(gatewayUpstreamSock, &rfds))
        {
            sockaddr_in from = {};
            socklen_t fromLen = sizeof(from);
            int rn = recvfrom(gatewayUpstreamSock, rx, sizeof(rx), 0, reinterpret_cast<sockaddr *>(&from), &fromLen);
            if (rn > 0)
            {
                uint16_t respId = static_cast<uint16_t>((rx[0] << 8) | rx[1]);
                DASH_GATEWAY_FOR_PENDING(q)
                {
                    if (q.inUse && q.proxyId == respId)
                    {
                        if (q.rulesVersion != gatewayDnsRulesVersion)
                        {
                            q.inUse = false;
                            break;
                        }
                        dashGatewayTrackLatency(q, xTaskGetTickCount());
                        dashGatewayDnsCachePut(q.domain, q.qtype, static_cast<uint32_t>(esp_timer_get_time() / 1000000ULL), rx, rn);
                        for (uint8_t ci = 0; ci < q.clientCount; ci++)
                        {
                            rx[0] = q.clientIds[ci] >> 8;
                            rx[1] = q.clientIds[ci] & 0xFF;
                            sendto(gatewayDnsSock, rx, rn, 0, reinterpret_cast<sockaddr *>(&q.clients[ci]), sizeof(q.clients[ci]));
                        }
                        q.inUse = false;
                        break;
                    }
                }
            }
        }
    }
}

static bool dashGatewayReadListFile(const char *path, String &out)
{
    if (!SPIFFS.exists(path))
        return false;
    File f = SPIFFS.open(path, "r");
    if (!f)
        return false;
    out = f.readString();
    f.close();
    return true;
}

static bool dashGatewayWriteListFile(const char *path, const String &value)
{
    File f = SPIFFS.open(path, "w");
    if (!f)
        return false;
    size_t written = f.write(reinterpret_cast<const uint8_t *>(value.c_str()), value.length());
    f.close();
    return written == value.length();
}

static String dashGatewayIpToString(uint32_t ip, const char *emptyText = "none")
{
    if (ip == IPADDR_NONE || ip == 0)
        return emptyText;
    struct in_addr a;
    a.s_addr = ip;
    const char *p = inet_ntoa(a);
    return p ? String(p) : String(emptyText);
}

static bool dashGatewayParseDnsIp(const String &value, uint32_t &out)
{
    std::string s = static_cast<std::string>(value);
    while (!s.empty() && std::isspace(static_cast<unsigned char>(s.front())))
        s.erase(s.begin());
    while (!s.empty() && std::isspace(static_cast<unsigned char>(s.back())))
        s.pop_back();
    if (s.empty())
        return false;
    uint32_t ip = inet_addr(s.c_str());
    if (ip == IPADDR_NONE || ip == 0)
        return false;
    out = ip;
    return true;
}

static const char *dashGatewayUpstreamModeName(uint8_t mode)
{
    switch (mode)
    {
    case kDashGatewayUpstreamAli:
        return "ali";
    case kDashGatewayUpstreamTencent:
        return "tencent";
    case kDashGatewayUpstreamCustom:
        return "custom";
    default:
        return "auto";
    }
}

static uint32_t dashGatewaySelectedUpstreamDns()
{
    switch (gatewayUpstreamDnsMode)
    {
    case kDashGatewayUpstreamAli:
        return inet_addr("223.5.5.5");
    case kDashGatewayUpstreamTencent:
        return inet_addr("119.29.29.29");
    case kDashGatewayUpstreamCustom:
        if (gatewayCustomUpstreamDns != IPADDR_NONE && gatewayCustomUpstreamDns != 0)
            return gatewayCustomUpstreamDns;
        break;
    default:
        break;
    }
    if (gatewayDhcpDns != IPADDR_NONE && gatewayDhcpDns != 0)
        return gatewayDhcpDns;
    return inet_addr("223.5.5.5");
}

static void dashGatewayRefreshSelectedUpstreamDns()
{
    if (WiFi.status() == WL_CONNECTED)
        gatewayUpstreamDns = dashGatewaySelectedUpstreamDns();
    else
        gatewayUpstreamDns = IPADDR_NONE;
}

static void dashGatewayResetDnsStats()
{
    gatewayDnsCacheHits = 0;
    gatewayDnsCacheMisses = 0;
    gatewayDnsLatencyLastMs = 0;
    gatewayDnsLatencyAvgMs = 0;
    gatewayDnsLatencyMaxMs = 0;
    gatewayDnsSlow500Ms = 0;
    gatewayDnsSlow1000Ms = 0;
    gatewayDnsSlow2000Ms = 0;
    gatewayDnsPendingMax = dashGatewayPendingCount();
    gatewayDnsPendingFull = 0;
    gatewayDnsTimeouts = 0;
    gatewayDnsUpstreamFails = 0;
}

static bool dashGatewaySaveMeta()
{
    nvs_handle_t h = 0;
    esp_err_t err = nvs_open(kDashGatewayPrefsNs, NVS_READWRITE, &h);
    if (err != ESP_OK)
    {
        ESP_LOGW(kDashGatewayTag, "NVS open failed while saving gateway meta: %s", esp_err_to_name(err));
        return false;
    }

    err = nvs_set_u8(h, "en", gatewayEnabled ? 1 : 0);
    // Daily DNS mode is fixed: blacklist roots, whitelist subdomains override.
    if (err == ESP_OK)
        err = nvs_set_u8(h, "profile", kDashGatewayTeslaProfileVersion);
    if (err == ESP_OK)
        err = nvs_set_u8(h, "upmode", gatewayUpstreamDnsMode);
    if (err == ESP_OK)
        err = nvs_set_str(h, "upcustom", dashGatewayIpToString(gatewayCustomUpstreamDns, "").c_str());
    // Lists now live in SPIFFS so large edits no longer consume scarce NVS pages.
    if (err == ESP_OK)
    {
        esp_err_t eraseBlack = nvs_erase_key(h, "black");
        if (eraseBlack != ESP_OK && eraseBlack != ESP_ERR_NVS_NOT_FOUND)
            err = eraseBlack;
    }
    if (err == ESP_OK)
    {
        esp_err_t eraseWhite = nvs_erase_key(h, "white");
        if (eraseWhite != ESP_OK && eraseWhite != ESP_ERR_NVS_NOT_FOUND)
            err = eraseWhite;
    }
    if (err == ESP_OK)
    {
        esp_err_t eraseMode = nvs_erase_key(h, "mode");
        if (eraseMode != ESP_OK && eraseMode != ESP_ERR_NVS_NOT_FOUND)
            err = eraseMode;
    }
    if (err == ESP_OK)
    {
        esp_err_t eraseStrict = nvs_erase_key(h, "strict");
        if (eraseStrict != ESP_OK && eraseStrict != ESP_ERR_NVS_NOT_FOUND)
            err = eraseStrict;
    }
    if (err == ESP_OK)
        err = nvs_commit(h);
    nvs_close(h);

    if (err != ESP_OK)
    {
        ESP_LOGW(kDashGatewayTag, "NVS commit failed while saving gateway meta: %s", esp_err_to_name(err));
        return false;
    }
    return true;
}

static bool dashGatewaySave()
{
    bool ok = dashGatewayWriteListFile(kDashGatewayBlacklistPath, gatewayDnsBlacklist);
    ok = dashGatewayWriteListFile(kDashGatewayWhitelistPath, gatewayDnsWhitelist) && ok;
    if (SPIFFS.exists("/gw_cidr.txt"))
        SPIFFS.remove("/gw_cidr.txt");
    ok = dashGatewaySaveMeta() && ok;
    if (!ok)
        ESP_LOGW(kDashGatewayTag, "Gateway DNS settings save failed");
    return ok;
}

static void dashGatewayLoad()
{
    if (!dashGatewayAllocateState())
    {
        gatewayEnabled = false;
        ESP_LOGE(kDashGatewayTag, "Gateway disabled because DNS state allocation failed");
        return;
    }
    String legacyBlacklist = kDashGatewayDefaultBlacklist;
    String legacyWhitelist = kDashGatewayDefaultWhitelist;
    uint8_t profileVersion = 0;
    Preferences p;
    if (p.begin(kDashGatewayPrefsNs, false))
    {
        gatewayEnabled = p.getBool("en", true);
        profileVersion = p.getUChar("profile", 0);
        gatewayUpstreamDnsMode = p.getUChar("upmode", kDashGatewayUpstreamAuto);
        if (gatewayUpstreamDnsMode > kDashGatewayUpstreamCustom)
            gatewayUpstreamDnsMode = kDashGatewayUpstreamAuto;
        uint32_t customDns = IPADDR_NONE;
        if (dashGatewayParseDnsIp(p.getString("upcustom", ""), customDns))
            gatewayCustomUpstreamDns = customDns;
        else
            gatewayCustomUpstreamDns = IPADDR_NONE;
        legacyBlacklist = p.getString("black", kDashGatewayDefaultBlacklist);
        legacyWhitelist = p.getString("white", kDashGatewayDefaultWhitelist);
        p.end();
    }

    String fileList;
    bool loadedBlackFromFile = dashGatewayReadListFile(kDashGatewayBlacklistPath, fileList);
    gatewayDnsBlacklist = dashGatewaySanitizeBlacklist(loadedBlackFromFile ? fileList : legacyBlacklist);
    bool loadedWhiteFromFile = dashGatewayReadListFile(kDashGatewayWhitelistPath, fileList);
    gatewayDnsWhitelist = dashGatewaySanitizeWhitelist(loadedWhiteFromFile ? fileList : legacyWhitelist);
    if (profileVersion < kDashGatewayTeslaProfileVersion)
    {
        gatewayEnabled = true;
        gatewayDnsBlacklist = dashGatewaySanitizeBlacklist(kDashGatewayDefaultBlacklist);
        gatewayDnsWhitelist = dashGatewaySanitizeWhitelist(kDashGatewayDefaultWhitelist);
        gatewayDnsRulesVersion++;
        dashGatewayDnsCacheClear();
        loadedBlackFromFile = loadedWhiteFromFile = false;
    }

    dashGatewayCompileAllRules();

    if (!loadedBlackFromFile || !loadedWhiteFromFile)
        dashGatewaySave();
}

static void dashGatewayStartDns()
{
    if (gatewayDnsTaskHandle)
        return;
    gatewayDnsSock = socket(AF_INET, SOCK_DGRAM, IPPROTO_IP);
    if (gatewayDnsSock < 0)
    {
        ESP_LOGW(kDashGatewayTag, "DNS socket open failed");
        return;
    }
    int yes = 1;
    setsockopt(gatewayDnsSock, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes));
    sockaddr_in addr = {};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(53);
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    if (bind(gatewayDnsSock, reinterpret_cast<sockaddr *>(&addr), sizeof(addr)) != 0)
    {
        ESP_LOGW(kDashGatewayTag, "DNS bind failed (errno=%d)", errno);
        close(gatewayDnsSock);
        gatewayDnsSock = -1;
        gatewayDnsBindOk = false;
        return;
    }
    gatewayDnsBindOk = true;
    ESP_LOGI(kDashGatewayTag, "DNS socket bound to UDP 53 (fd=%d)", gatewayDnsSock);
    xTaskCreatePinnedToCore(dashGatewayDnsTask, "gw_dns", 6144, nullptr, 1, &gatewayDnsTaskHandle, 1);
}

static bool gatewayApDnsConfigured = false; // guard: only configure DHCP/DNS once

static void dashGatewayConfigureApDns(esp_netif_t *apNetif)
{
    if (!apNetif)
        return;
    // Only stop/start DHCP server once. Re-running this disrupts AP clients
    // that already have a lease (forces re-DHCP). Call only at first AP start.
    if (gatewayApDnsConfigured)
    {
        ESP_LOGI(kDashGatewayTag, "AP DNS already configured, skipping DHCP restart");
        return;
    }
    esp_netif_ip_info_t apIp = {};
    if (esp_netif_get_ip_info(apNetif, &apIp) != ESP_OK)
        return;
    esp_netif_dns_info_t dns = {};
    dns.ip.type = ESP_IPADDR_TYPE_V4;
    dns.ip.u_addr.ip4 = apIp.ip;
    esp_netif_dhcps_stop(apNetif);
    esp_netif_set_dns_info(apNetif, ESP_NETIF_DNS_MAIN, &dns);
    dhcps_offer_t offer = OFFER_DNS;
    esp_netif_dhcps_option(apNetif, ESP_NETIF_OP_SET, ESP_NETIF_DOMAIN_NAME_SERVER, &offer, sizeof(offer));
    esp_netif_dhcps_start(apNetif);
    gatewayApDnsConfigured = true;
    ESP_LOGI(kDashGatewayTag, "AP DNS configured -> " IPSTR, IP2STR(&apIp.ip));
}

static void dashGatewayOnApStarted(esp_netif_t *apNetif)
{
    if (!gatewayEnabled)
        return;
    dashGatewayConfigureApDns(apNetif);
    dashGatewayStartDns();
    ESP_LOGI(kDashGatewayTag, "AP ready ip=%s clients=%u dns=%s",
             WiFi.softAPIP().toString().c_str(),
             static_cast<unsigned>(WiFi.softAPgetStationNum()),
             gatewayApDnsConfigured ? "configured" : "pending");
}

static void dashGatewayOnStaConnected(esp_netif_t *staNetif, esp_netif_t *apNetif)
{
    if (!gatewayEnabled)
        return;
    esp_netif_dns_info_t dns = {};
    if (staNetif && esp_netif_get_dns_info(staNetif, ESP_NETIF_DNS_MAIN, &dns) == ESP_OK &&
        dns.ip.type == ESP_IPADDR_TYPE_V4 && dns.ip.u_addr.ip4.addr != 0)
    {
        gatewayDhcpDns = dns.ip.u_addr.ip4.addr;
    }
    else
    {
        gatewayDhcpDns = IPADDR_NONE;
    }
    gatewayUpstreamDns = dashGatewaySelectedUpstreamDns();

#if IP_NAPT
    void *lwipAp = apNetif ? esp_netif_get_netif_impl(apNetif) : nullptr;
    if (lwipAp)
    {
        if (ip_napt_enable_netif(static_cast<netif *>(lwipAp), 1))
            gatewayNaptEnabled = true;
        else
            ESP_LOGW(kDashGatewayTag, "ip_napt_enable_netif failed");
    }
#else
    ESP_LOGW(kDashGatewayTag, "CONFIG_LWIP_IPV4_NAPT is disabled");
#endif
    char upstreamLog[16] = "none";
    if (gatewayUpstreamDns != IPADDR_NONE && gatewayUpstreamDns != 0)
    {
        struct in_addr a;
        a.s_addr = gatewayUpstreamDns;
        const char *p = inet_ntoa(a);
        if (p)
            snprintf(upstreamLog, sizeof(upstreamLog), "%s", p);
    }
    ESP_LOGI(kDashGatewayTag, "STA ready ip=%s upstream_dns=%s mode=%s dhcp=%s nat=%s",
             WiFi.localIP().toString().c_str(),
             upstreamLog,
             dashGatewayUpstreamModeName(gatewayUpstreamDnsMode),
             dashGatewayIpToString(gatewayDhcpDns).c_str(),
             gatewayNaptEnabled ? "on" : "waiting");
}

static void dashGatewayOnStaDisconnected(esp_netif_t *apNetif)
{
    gatewayUpstreamDns = IPADDR_NONE;
    gatewayDhcpDns = IPADDR_NONE;
    gatewayNaptEnabled = false;
    dashGatewayClearPending();
#if IP_NAPT
    void *lwipAp = apNetif ? esp_netif_get_netif_impl(apNetif) : nullptr;
    if (lwipAp)
        ip_napt_enable_netif(static_cast<netif *>(lwipAp), 0);
#endif
    ESP_LOGI(kDashGatewayTag, "STA offline; NAT disabled and DNS pending cleared");
}

static String dashGatewayStatusJson()
{
    // Format upstream DNS as dotted-decimal string
    String upstreamStr = dashGatewayIpToString(gatewayUpstreamDns);
    String dhcpDnsStr = dashGatewayIpToString(gatewayDhcpDns);
    String customDnsStr = dashGatewayIpToString(gatewayCustomUpstreamDns, "");

    wifi_ap_record_t staInfo = {};
    bool hasStaInfo = esp_wifi_sta_get_ap_info(&staInfo) == ESP_OK;
    wifi_config_t apConfig = {};
    uint8_t apChannel = 0;
    if (esp_wifi_get_config(WIFI_IF_AP, &apConfig) == ESP_OK)
        apChannel = apConfig.ap.channel;

    String j = "{\"enabled\":";
    j += gatewayEnabled ? "true" : "false";
    j += ",\"nat\":";
    j += (gatewayNaptEnabled && WiFi.status() == WL_CONNECTED) ? "true" : "false";
#if IP_NAPT
    j += ",\"napt_compiled\":true";
#else
    j += ",\"napt_compiled\":false";
#endif
    j += ",\"ap_ip\":\"";
    j += WiFi.softAPIP().toString();
    j += "\",\"ap_clients\":";
    j += String(WiFi.softAPgetStationNum());
    j += ",\"ap_channel\":";
    j += String(apChannel);
    j += ",\"sta_connected\":";
    j += (WiFi.status() == WL_CONNECTED) ? "true" : "false";
    j += ",\"sta_ip\":\"";
    j += WiFi.localIP().toString();
    j += "\",\"sta_ssid\":\"";
    j += jsonEscape(WiFi.status() == WL_CONNECTED ? WiFi.SSID() : String(""));
    j += "\"";
    j += ",\"sta_rssi\":";
    j += hasStaInfo ? String(staInfo.rssi) : String("null");
    j += ",\"sta_channel\":";
    j += hasStaInfo ? String(staInfo.primary) : String("0");
    j += ",\"same_channel\":";
    j += (hasStaInfo && apChannel > 0 && staInfo.primary == apChannel) ? "true" : "false";
    j += ",\"blocked\":";
    j += String(gatewayBlockedDomainCount);
    j += ",\"dns_cache_psram\":";
    j += (gatewayPendingQueriesInPsram || gatewayDnsCacheInPsram) ? "true" : "false";
    j += ",\"dns_resp_cache\":";
    j += gatewayDnsCache ? String(static_cast<unsigned>(kDashGatewayDnsCacheEntries)) : "0";
    j += ",\"dns_resp_hits\":";
    j += String(gatewayDnsCacheHits);
    j += ",\"dns_resp_misses\":";
    j += String(gatewayDnsCacheMisses);
    j += ",\"black_rules\":";
    j += String(gatewayBlacklistRuleCount);
    j += ",\"white_rules\":";
    j += String(gatewayWhitelistRuleCount);
    j += ",\"rules_version\":";
    j += String(gatewayDnsRulesVersion);
    // DNS health observability fields
    j += ",\"dns_bind_ok\":";
    j += gatewayDnsBindOk ? "true" : "false";
    j += ",\"dns_task_active\":";
    j += (gatewayDnsTaskHandle != nullptr) ? "true" : "false";
    j += ",\"dns_sock\":";
    j += String(gatewayDnsSock);
    j += ",\"upstream_dns\":\"";
    j += upstreamStr;
    j += "\",\"upstream_dns_mode\":";
    j += String(gatewayUpstreamDnsMode);
    j += ",\"upstream_dns_mode_name\":\"";
    j += dashGatewayUpstreamModeName(gatewayUpstreamDnsMode);
    j += "\",\"upstream_dns_dhcp\":\"";
    j += dhcpDnsStr;
    j += "\",\"upstream_dns_custom\":\"";
    j += customDnsStr;
    j += "\",\"ap_dns_configured\":";
    j += gatewayApDnsConfigured ? "true" : "false";
    j += ",\"dns_latency_last_ms\":";
    j += String(gatewayDnsLatencyLastMs);
    j += ",\"dns_latency_avg_ms\":";
    j += String(gatewayDnsLatencyAvgMs);
    j += ",\"dns_latency_max_ms\":";
    j += String(gatewayDnsLatencyMaxMs);
    j += ",\"dns_slow_500ms\":";
    j += String(gatewayDnsSlow500Ms);
    j += ",\"dns_slow_1000ms\":";
    j += String(gatewayDnsSlow1000Ms);
    j += ",\"dns_slow_2000ms\":";
    j += String(gatewayDnsSlow2000Ms);
    j += ",\"dns_pending\":";
    j += String(dashGatewayPendingCount());
    j += ",\"dns_pending_capacity\":";
    j += String(static_cast<unsigned>(kDashGatewayMaxPending));
    j += ",\"dns_pending_max\":";
    j += String(gatewayDnsPendingMax);
    j += ",\"dns_pending_full\":";
    j += String(gatewayDnsPendingFull);
    j += ",\"dns_timeouts\":";
    j += String(gatewayDnsTimeouts);
    j += ",\"dns_upstream_fails\":";
    j += String(gatewayDnsUpstreamFails);
    j += "}";
    return j;
}

static void handleGatewayStatus()
{
    server.send(200, "application/json", dashGatewayStatusJson());
}

static String dashGatewayDnsSettingsJson(bool ok)
{
    String j = "{\"ok\":";
    j += ok ? "true" : "false";
    j += ",\"enabled\":";
    j += gatewayEnabled ? "true" : "false";
    j += ",\"blacklist\":\"";
    j += jsonEscape(gatewayDnsBlacklist.c_str());
    j += "\",\"whitelist\":\"";
    j += jsonEscape(gatewayDnsWhitelist.c_str());
    j += "\",\"upstream_mode\":";
    j += String(gatewayUpstreamDnsMode);
    j += ",\"upstream_mode_name\":\"";
    j += dashGatewayUpstreamModeName(gatewayUpstreamDnsMode);
    j += "\",\"upstream_custom\":\"";
    j += dashGatewayIpToString(gatewayCustomUpstreamDns, "");
    j += "\",\"upstream_dhcp\":\"";
    j += dashGatewayIpToString(gatewayDhcpDns);
    j += "\",\"upstream_effective\":\"";
    j += dashGatewayIpToString(gatewayUpstreamDns);
    j += "\",\"black_count\":";
    j += String(static_cast<unsigned>(dashGatewayCountEntries(gatewayDnsBlacklist)));
    j += ",\"white_count\":";
    j += String(static_cast<unsigned>(dashGatewayCountEntries(gatewayDnsWhitelist)));
    j += ",\"black_max\":";
    j += String(static_cast<unsigned>(kDashGatewayMaxBlacklistEntries));
    j += ",\"white_max\":";
    j += String(static_cast<unsigned>(kDashGatewayMaxWhitelistEntries));
    j += "}";
    return j;
}

static void handleGatewayDnsGet()
{
    String j = dashGatewayDnsSettingsJson(true);
    server.send(200, "application/json", j);
}

static void handleGatewayDnsTest()
{
    String domain = server.hasArg("domain") ? server.arg("domain") : "";
    server.send(200, "application/json", dashGatewayDnsDecisionJson(domain));
}

static void handleGatewayDnsPost()
{
    bool oldEnabled = gatewayEnabled;
    String oldBlacklist = gatewayDnsBlacklist;
    String oldWhitelist = gatewayDnsWhitelist;
    uint8_t oldUpstreamMode = gatewayUpstreamDnsMode;
    uint32_t oldCustomUpstream = gatewayCustomUpstreamDns;

    gatewayEnabled = !server.hasArg("enabled") || server.arg("enabled").toInt() != 0;
    bool rulesChanged = false;
    bool upstreamChanged = false;
    if (server.hasArg("upstream_mode"))
    {
        int nextMode = server.arg("upstream_mode").toInt();
        if (nextMode < 0 || nextMode > kDashGatewayUpstreamCustom)
            nextMode = kDashGatewayUpstreamAuto;
        gatewayUpstreamDnsMode = static_cast<uint8_t>(nextMode);
    }
    if (server.hasArg("upstream_custom"))
    {
        uint32_t custom = IPADDR_NONE;
        String customArg = server.arg("upstream_custom");
        if (customArg.length() > 0 && !dashGatewayParseDnsIp(customArg, custom))
        {
            gatewayEnabled = oldEnabled;
            gatewayDnsBlacklist = oldBlacklist;
            gatewayDnsWhitelist = oldWhitelist;
            gatewayUpstreamDnsMode = oldUpstreamMode;
            gatewayCustomUpstreamDns = oldCustomUpstream;
            server.send(400, "application/json", "{\"ok\":false,\"error\":\"invalid upstream DNS\"}");
            return;
        }
        gatewayCustomUpstreamDns = custom;
    }
    if (gatewayUpstreamDnsMode == kDashGatewayUpstreamCustom &&
        (gatewayCustomUpstreamDns == IPADDR_NONE || gatewayCustomUpstreamDns == 0))
    {
        gatewayEnabled = oldEnabled;
        gatewayDnsBlacklist = oldBlacklist;
        gatewayDnsWhitelist = oldWhitelist;
        gatewayUpstreamDnsMode = oldUpstreamMode;
        gatewayCustomUpstreamDns = oldCustomUpstream;
        server.send(400, "application/json", "{\"ok\":false,\"error\":\"custom upstream DNS required\"}");
        return;
    }
    upstreamChanged = oldUpstreamMode != gatewayUpstreamDnsMode || oldCustomUpstream != gatewayCustomUpstreamDns;
    if (server.hasArg("blacklist"))
    {
        String next = dashGatewaySanitizeBlacklist(server.arg("blacklist"));
        if (next != gatewayDnsBlacklist)
        {
            gatewayDnsBlacklist = next;
            rulesChanged = true;
        }
    }
    if (server.hasArg("whitelist"))
    {
        String next = dashGatewaySanitizeWhitelist(server.arg("whitelist"));
        if (next != gatewayDnsWhitelist)
        {
            gatewayDnsWhitelist = next;
            rulesChanged = true;
        }
    }
    if (!dashGatewaySave())
    {
        gatewayEnabled = oldEnabled;
        gatewayDnsBlacklist = oldBlacklist;
        gatewayDnsWhitelist = oldWhitelist;
        gatewayUpstreamDnsMode = oldUpstreamMode;
        gatewayCustomUpstreamDns = oldCustomUpstream;
        dashGatewayCompileAllRules();
        server.send(500, "application/json", "{\"ok\":false,\"error\":\"save failed\"}");
        return;
    }
    // Rule changes invalidate DNS response cache so the next query uses the
    // current blacklist root / whitelist subdomain policy immediately.
    if (rulesChanged)
    {
        gatewayDnsRulesVersion++;
        dashGatewayCompileAllRules();
        dashGatewayDnsCacheClear();
    }
    else if (oldEnabled != gatewayEnabled)
    {
        gatewayDnsRulesVersion++;
    }
    if (upstreamChanged)
    {
        dashGatewayRefreshSelectedUpstreamDns();
        dashGatewayClearPending();
        ESP_LOGI(kDashGatewayTag, "DNS upstream changed mode=%s effective=%s custom=%s",
                 dashGatewayUpstreamModeName(gatewayUpstreamDnsMode),
                 dashGatewayIpToString(gatewayUpstreamDns).c_str(),
                 dashGatewayIpToString(gatewayCustomUpstreamDns, "").c_str());
    }
    server.send(200, "application/json", dashGatewayDnsSettingsJson(true));
}

static void handleGatewayDnsStatsReset()
{
    dashGatewayResetDnsStats();
    server.send(200, "application/json", dashGatewayStatusJson());
}

static void handleGatewayWhitelistAdd()
{
    if (!server.hasArg("domain"))
    {
        server.send(400, "application/json", "{\"ok\":false,\"error\":\"domain required\"}");
        return;
    }
    String domain = dashGatewayNormalizeDomain(server.arg("domain"));
    if (dashGatewayDomainInList(domain, gatewayDnsBlacklist))
    {
        server.send(409, "application/json", "{\"ok\":false,\"error\":\"domain is blacklisted\"}");
        return;
    }
    if (dashGatewayDomainInList(domain, gatewayDnsWhitelist))
    {
        server.send(200, "application/json", "{\"ok\":true,\"already\":true}");
        return;
    }
    if (dashGatewayCountEntries(gatewayDnsWhitelist) >= kDashGatewayMaxWhitelistEntries)
    {
        server.send(409, "application/json", "{\"ok\":false,\"error\":\"whitelist full (max 200)\"}");
        return;
    }
    String oldWhitelist = gatewayDnsWhitelist;
    if (!dashGatewayAppendWhitelist(domain))
    {
        server.send(400, "application/json", "{\"ok\":false,\"error\":\"cannot add domain\"}");
        return;
    }
    if (!dashGatewaySave())
    {
        gatewayDnsWhitelist = oldWhitelist;
        server.send(500, "application/json", "{\"ok\":false,\"error\":\"save failed\"}");
        return;
    }
    gatewayDnsRulesVersion++;
    dashGatewayCompileAllRules();
    dashGatewayDnsCacheClear();
    server.send(200, "application/json", dashGatewayDnsSettingsJson(true));
}

static void handleGatewayBlocked()
{
    // Snapshot under lock 鈥?never call String/heap ops inside portENTER_CRITICAL.
    DashGatewayBlockedDomain snapshot[32];
    uint8_t count;
    portENTER_CRITICAL(&gatewayBlockedMux);
    count = gatewayBlockedDomainCount;
    if (count > 32)
        count = 32;
    for (uint8_t i = 0; i < count; i++)
        snapshot[i] = gatewayBlockedDomains[i];
    portEXIT_CRITICAL(&gatewayBlockedMux);

    String j = "[";
    for (uint8_t i = 0; i < count; i++)
    {
        if (i)
            j += ",";
        j += "{\"domain\":\"";
        j += jsonEscape(snapshot[i].domain);
        j += "\",\"count\":";
        j += String(snapshot[i].count);
        j += ",\"blacklisted\":";
        j += dashGatewayDomainInList(snapshot[i].domain, gatewayDnsBlacklist) ? "true" : "false";
        j += ",\"whitelisted\":";
        j += dashGatewayDomainInList(snapshot[i].domain, gatewayDnsWhitelist) ? "true" : "false";
        j += ",\"canWhitelist\":";
        j += dashGatewayDomainAllowedForWhitelist(snapshot[i].domain) ? "true" : "false";
        j += "}";
    }
    j += "]";
    server.send(200, "application/json", j);
}

static void handleGatewayBlockedClear()
{
    portENTER_CRITICAL(&gatewayBlockedMux);
    gatewayBlockedDomainCount = 0;
    std::memset(gatewayBlockedDomains, 0, sizeof(DashGatewayBlockedDomain) * 32);
    portEXIT_CRITICAL(&gatewayBlockedMux);
    server.send(200, "application/json", "{\"ok\":true}");
}

#else

static void dashGatewayLoad() {}
static void dashGatewayOnApStarted(esp_netif_t *) {}
static void dashGatewayOnStaConnected(esp_netif_t *, esp_netif_t *) {}
static void dashGatewayOnStaDisconnected(esp_netif_t *) {}

#endif
