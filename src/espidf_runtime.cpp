#include "platform/espidf_runtime.h"

#ifdef ESP_PLATFORM

#include <errno.h>
#include <fcntl.h>
#include <driver/usb_serial_jtag.h>
#include <sys/stat.h>
#include <unistd.h>

SerialClass Serial;
SPIFFSClass SPIFFS;
WiFiClass WiFi;
UpdateClass Update;
ESPClass ESP;
ArduinoOTAClass ArduinoOTA;

static constexpr const char *kCompatTag = "idf_compat";
static constexpr const char *kSpiffsBase = "/spiffs";
static bool serialInputConfigured = false;
static bool serialUsbJtagReady = false;
static bool serialUsbJtagTried = false;
static int serialPeekByte = -1;
static volatile wl_status_t wifiStaStatus = WL_DISCONNECTED;
static volatile uint8_t wifiLastDisconnectReason = 0;
static bool wifiEventHandlersRegistered = false;

static void configureWifiLogLevels()
{
    esp_log_level_set("wifi", ESP_LOG_WARN);
    esp_log_level_set("wifi_init", ESP_LOG_WARN);
    esp_log_level_set("phy_init", ESP_LOG_WARN);
    esp_log_level_set("esp_netif_lwip", ESP_LOG_WARN);
    esp_log_level_set("esp_netif_handlers", ESP_LOG_WARN);
    esp_log_level_set("httpd_txrx", ESP_LOG_ERROR);
    esp_log_level_set("pp", ESP_LOG_WARN);
    esp_log_level_set("net80211", ESP_LOG_WARN);
}

static const char *wifiDisconnectReasonName(uint8_t reason)
{
    switch (reason)
    {
    case 0:
        return "none";
    case WIFI_REASON_AUTH_EXPIRE:
        return "auth_expire";
    case WIFI_REASON_AUTH_LEAVE:
        return "auth_leave";
    case WIFI_REASON_DISASSOC_DUE_TO_INACTIVITY:
        return "inactive";
    case WIFI_REASON_ASSOC_TOOMANY:
        return "assoc_toomany";
    case WIFI_REASON_CLASS2_FRAME_FROM_NONAUTH_STA:
        return "not_authed";
    case WIFI_REASON_CLASS3_FRAME_FROM_NONASSOC_STA:
        return "not_assoced";
    case WIFI_REASON_ASSOC_LEAVE:
        return "assoc_leave";
    case WIFI_REASON_ASSOC_NOT_AUTHED:
        return "assoc_not_authed";
    case WIFI_REASON_DISASSOC_PWRCAP_BAD:
        return "pwrcap_bad";
    case WIFI_REASON_DISASSOC_SUPCHAN_BAD:
        return "supchan_bad";
    case WIFI_REASON_4WAY_HANDSHAKE_TIMEOUT:
        return "4way_timeout";
    case WIFI_REASON_HANDSHAKE_TIMEOUT:
        return "handshake_timeout";
    case WIFI_REASON_NO_AP_FOUND:
        return "no_ap_found";
    case WIFI_REASON_AUTH_FAIL:
        return "auth_fail";
    case WIFI_REASON_ASSOC_FAIL:
        return "assoc_fail";
#ifdef WIFI_REASON_NO_AP_FOUND_W_COMPATIBLE_SECURITY
    case WIFI_REASON_NO_AP_FOUND_W_COMPATIBLE_SECURITY:
        return "no_compatible_security";
    case WIFI_REASON_NO_AP_FOUND_IN_AUTHMODE_THRESHOLD:
        return "authmode_threshold";
    case WIFI_REASON_NO_AP_FOUND_IN_RSSI_THRESHOLD:
        return "rssi_threshold";
#endif
    default:
        return "unknown";
    }
}

static void wifiEventHandler(void *, esp_event_base_t base, int32_t id, void *eventData)
{
    if (base == WIFI_EVENT)
    {
        switch (id)
        {
        case WIFI_EVENT_STA_START:
            if (wifiStaStatus != WL_CONNECTED)
                wifiStaStatus = WL_IDLE_STATUS;
            break;
        case WIFI_EVENT_STA_CONNECTED:
            wifiStaStatus = WL_IDLE_STATUS; // wait for DHCP/GOT_IP before reporting connected
            wifiLastDisconnectReason = 0;
            break;
        case WIFI_EVENT_STA_DISCONNECTED:
        {
            uint8_t reason = 0;
            if (eventData)
                reason = static_cast<wifi_event_sta_disconnected_t *>(eventData)->reason;
            wifiLastDisconnectReason = reason;
            switch (reason)
            {
            case WIFI_REASON_NO_AP_FOUND:
                wifiStaStatus = WL_NO_SSID_AVAIL;
                break;
            case WIFI_REASON_AUTH_FAIL:
            case WIFI_REASON_ASSOC_FAIL:
            case WIFI_REASON_4WAY_HANDSHAKE_TIMEOUT:
            case WIFI_REASON_HANDSHAKE_TIMEOUT:
                wifiStaStatus = WL_CONNECT_FAILED;
                break;
            default:
                wifiStaStatus = (wifiStaStatus == WL_CONNECTED) ? WL_CONNECTION_LOST : WL_DISCONNECTED;
                break;
            }
            break;
        }
        default:
            break;
        }
    }
    else if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP)
    {
        wifiStaStatus = WL_CONNECTED;
        wifiLastDisconnectReason = 0;
    }
}

static std::string urlDecode(const std::string &input)
{
    std::string out;
    out.reserve(input.size());
    for (size_t i = 0; i < input.size(); i++)
    {
        if (input[i] == '+')
        {
            out += ' ';
        }
        else if (input[i] == '%' && i + 2 < input.size())
        {
            char hex[3] = {input[i + 1], input[i + 2], 0};
            out += static_cast<char>(std::strtoul(hex, nullptr, 16));
            i += 2;
        }
        else
        {
            out += input[i];
        }
    }
    return out;
}

void delay(uint32_t ms)
{
    vTaskDelay(pdMS_TO_TICKS(ms));
}

void yield()
{
    vTaskDelay(1);
}

static void configureSerialInput()
{
    if (serialInputConfigured)
        return;
    int flags = fcntl(STDIN_FILENO, F_GETFL, 0);
    if (flags >= 0)
        fcntl(STDIN_FILENO, F_SETFL, flags | O_NONBLOCK);
    serialInputConfigured = true;
}

static void configureUsbSerialJtagInput()
{
#if CONFIG_ESP_CONSOLE_USB_SERIAL_JTAG_ENABLED || CONFIG_ESP_CONSOLE_SECONDARY_USB_SERIAL_JTAG
    if (serialUsbJtagTried)
        return;
    serialUsbJtagTried = true;
    usb_serial_jtag_driver_config_t cfg = USB_SERIAL_JTAG_DRIVER_CONFIG_DEFAULT();
    serialUsbJtagReady = usb_serial_jtag_driver_install(&cfg) == ESP_OK;
#endif
}

void SerialClass::begin(unsigned long)
{
    configureSerialInput();
}

int SerialClass::available()
{
    configureSerialInput();
    configureUsbSerialJtagInput();
    if (serialPeekByte >= 0)
        return 1;
    uint8_t byte = 0;
    if (serialUsbJtagReady && usb_serial_jtag_read_bytes(&byte, 1, 0) == 1)
    {
        serialPeekByte = byte;
        return 1;
    }
    int len = ::read(STDIN_FILENO, &byte, 1);
    if (len == 1)
    {
        serialPeekByte = byte;
        return 1;
    }
    return 0;
}

int SerialClass::read()
{
    configureSerialInput();
    configureUsbSerialJtagInput();
    if (serialPeekByte >= 0)
    {
        int byte = serialPeekByte;
        serialPeekByte = -1;
        return byte;
    }
    uint8_t byte = 0;
    if (serialUsbJtagReady && usb_serial_jtag_read_bytes(&byte, 1, 0) == 1)
        return byte;
    int len = ::read(STDIN_FILENO, &byte, 1);
    return len == 1 ? byte : -1;
}

uint32_t millis()
{
    return static_cast<uint32_t>(esp_timer_get_time() / 1000ULL);
}

void pinMode(uint8_t pin, uint8_t mode)
{
    gpio_config_t cfg = {};
    cfg.pin_bit_mask = 1ULL << pin;
    cfg.mode = mode == OUTPUT ? GPIO_MODE_OUTPUT : GPIO_MODE_INPUT;
    cfg.pull_up_en = mode == INPUT_PULLUP ? GPIO_PULLUP_ENABLE : GPIO_PULLUP_DISABLE;
    cfg.pull_down_en = GPIO_PULLDOWN_DISABLE;
    cfg.intr_type = GPIO_INTR_DISABLE;
    gpio_config(&cfg);
}

void digitalWrite(uint8_t pin, uint8_t value)
{
    gpio_set_level(static_cast<gpio_num_t>(pin), value ? 1 : 0);
}

int digitalRead(uint8_t pin)
{
    return gpio_get_level(static_cast<gpio_num_t>(pin));
}

bool Preferences::begin(const char *name, bool readOnly)
{
    end();
    readOnly_ = readOnly;
    esp_err_t err = nvs_open(name, readOnly ? NVS_READONLY : NVS_READWRITE, &handle_);
    open_ = err == ESP_OK;
    return open_;
}

void Preferences::end()
{
    if (open_)
        nvs_close(handle_);
    handle_ = 0;
    open_ = false;
}

bool Preferences::isKey(const char *key)
{
    if (!open_)
        return false;
    size_t required = 0;
    esp_err_t strErr = nvs_get_str(handle_, key, nullptr, &required);
    if (strErr == ESP_OK || strErr == ESP_ERR_NVS_INVALID_LENGTH)
        return true;
    uint8_t value = 0;
    return nvs_get_u8(handle_, key, &value) == ESP_OK;
}

int8_t Preferences::getChar(const char *key, int8_t defaultValue)
{
    int8_t value = defaultValue;
    if (open_)
        nvs_get_i8(handle_, key, &value);
    return value;
}

uint8_t Preferences::getUChar(const char *key, uint8_t defaultValue)
{
    uint8_t value = defaultValue;
    if (open_)
        nvs_get_u8(handle_, key, &value);
    return value;
}

uint32_t Preferences::getUInt(const char *key, uint32_t defaultValue)
{
    uint32_t value = defaultValue;
    if (open_)
        nvs_get_u32(handle_, key, &value);
    return value;
}

bool Preferences::getBool(const char *key, bool defaultValue)
{
    uint8_t value = defaultValue ? 1 : 0;
    if (open_)
        nvs_get_u8(handle_, key, &value);
    return value != 0;
}

String Preferences::getString(const char *key, const char *defaultValue)
{
    if (!open_)
        return defaultValue;
    size_t len = 0;
    if (nvs_get_str(handle_, key, nullptr, &len) != ESP_OK || len == 0)
        return defaultValue;
    std::vector<char> buf(len);
    if (nvs_get_str(handle_, key, buf.data(), &len) != ESP_OK)
        return defaultValue;
    return buf.data();
}

void Preferences::putChar(const char *key, int8_t value)
{
    if (open_ && !readOnly_)
    {
        nvs_set_i8(handle_, key, value);
        nvs_commit(handle_);
    }
}

void Preferences::putUChar(const char *key, uint8_t value)
{
    if (open_ && !readOnly_)
    {
        nvs_set_u8(handle_, key, value);
        nvs_commit(handle_);
    }
}

void Preferences::putUInt(const char *key, uint32_t value)
{
    if (open_ && !readOnly_)
    {
        nvs_set_u32(handle_, key, value);
        nvs_commit(handle_);
    }
}

bool Preferences::putBoolChecked(const char *key, bool value)
{
    if (!open_ || readOnly_)
        return false;
    if (nvs_set_u8(handle_, key, value ? 1 : 0) != ESP_OK)
        return false;
    return nvs_commit(handle_) == ESP_OK;
}

void Preferences::putBool(const char *key, bool value)
{
    (void)putBoolChecked(key, value);
}

void Preferences::putString(const char *key, const String &value)
{
    if (open_ && !readOnly_)
    {
        nvs_set_str(handle_, key, value.c_str());
        nvs_commit(handle_);
    }
}

void Preferences::remove(const char *key)
{
    if (open_ && !readOnly_)
    {
        nvs_erase_key(handle_, key);
        nvs_commit(handle_);
    }
}

File::File(const std::string &path, const char *mode)
{
    std::string full = path.rfind(kSpiffsBase, 0) == 0 ? path : std::string(kSpiffsBase) + path;
    fp_ = std::fopen(full.c_str(), mode);
    name_ = path;
}

File::File(DIR *dir, std::string basePath) : dir_(dir), basePath_(std::move(basePath)) {}

void File::moveFrom(File &other)
{
    fp_ = other.fp_;
    dir_ = other.dir_;
    basePath_ = std::move(other.basePath_);
    name_ = std::move(other.name_);
    other.fp_ = nullptr;
    other.dir_ = nullptr;
}

void File::close()
{
    if (fp_)
        std::fclose(fp_);
    if (dir_)
        closedir(dir_);
    fp_ = nullptr;
    dir_ = nullptr;
}

size_t File::write(const uint8_t *buf, size_t len)
{
    return fp_ ? std::fwrite(buf, 1, len, fp_) : 0;
}

size_t File::read(uint8_t *buf, size_t len)
{
    return fp_ ? std::fread(buf, 1, len, fp_) : 0;
}

String File::readString()
{
    if (!fp_)
        return "";
    std::string out;
    char buf[256];
    size_t n = 0;
    while ((n = std::fread(buf, 1, sizeof(buf), fp_)) > 0)
        out.append(buf, n);
    return out;
}

void File::print(const String &value)
{
    if (fp_)
        std::fputs(value.c_str(), fp_);
}

void File::print(const char *value)
{
    if (fp_)
        std::fputs(value ? value : "", fp_);
}

void File::print(char value)
{
    if (fp_)
        std::fputc(value, fp_);
}

void File::print(unsigned long value)
{
    if (fp_)
        std::fprintf(fp_, "%lu", value);
}

void File::print(int value)
{
    if (fp_)
        std::fprintf(fp_, "%d", value);
}

void File::println()
{
    if (fp_)
        std::fputc('\n', fp_);
}

void File::println(const String &value)
{
    print(value);
    println();
}

void File::println(const char *value)
{
    print(value);
    println();
}

File File::openNextFile()
{
    if (!dir_)
        return {};
    dirent *entry = readdir(dir_);
    while (entry && entry->d_name[0] == '.')
        entry = readdir(dir_);
    if (!entry)
        return {};
    std::string rel = basePath_ == "/" ? std::string("/") + entry->d_name : basePath_ + "/" + entry->d_name;
    return File(rel, "r");
}

bool SPIFFSClass::begin(bool formatOnFail)
{
    esp_vfs_spiffs_conf_t conf = {};
    conf.base_path = kSpiffsBase;
    conf.partition_label = nullptr;
    conf.max_files = 8;
    conf.format_if_mount_failed = formatOnFail;
    esp_err_t err = esp_vfs_spiffs_register(&conf);
    return err == ESP_OK || err == ESP_ERR_INVALID_STATE;
}

File SPIFFSClass::open(const String &path, const char *mode)
{
    if (path == "/")
        return File(opendir(kSpiffsBase), "/");
    return File(path.c_str(), mode);
}

bool SPIFFSClass::exists(const String &path)
{
    std::string full = std::string(kSpiffsBase) + path.c_str();
    struct stat st = {};
    return stat(full.c_str(), &st) == 0;
}

bool SPIFFSClass::remove(const String &path)
{
    std::string full = std::string(kSpiffsBase) + path.c_str();
    return std::remove(full.c_str()) == 0;
}

void WiFiClass::ensure()
{
    if (initialized_)
        return;
    configureWifiLogLevels();
    esp_netif_init();
    esp_event_loop_create_default();
    apNetif_ = esp_netif_create_default_wifi_ap();
    staNetif_ = esp_netif_create_default_wifi_sta();
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    esp_wifi_init(&cfg);
    if (!wifiEventHandlersRegistered)
    {
        esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID, wifiEventHandler, nullptr, nullptr);
        esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP, wifiEventHandler, nullptr, nullptr);
        wifiEventHandlersRegistered = true;
    }
    initialized_ = true;
}

void WiFiClass::mode(wifi_mode_t modeValue)
{
    ensure();
    esp_wifi_set_mode(modeValue);
    esp_wifi_start();
}

wifi_mode_t WiFiClass::getMode()
{
    if (!initialized_)
        return WIFI_MODE_NULL;
    wifi_mode_t cur = WIFI_MODE_NULL;
    if (esp_wifi_get_mode(&cur) != ESP_OK)
        return WIFI_MODE_NULL;
    return cur;
}

void WiFiClass::setSleep(bool enabled)
{
    ensure();
    esp_wifi_set_ps(enabled ? WIFI_PS_MIN_MODEM : WIFI_PS_NONE);
}

bool WiFiClass::softAPConfig(IPAddress local, IPAddress gateway, IPAddress subnet)
{
    ensure();
    esp_netif_ip_info_t ip = {};
    ip.ip = local.raw();
    ip.gw = gateway.raw();
    ip.netmask = subnet.raw();
    esp_netif_dhcps_stop(apNetif_);
    esp_err_t err = esp_netif_set_ip_info(apNetif_, &ip);
    esp_netif_dhcps_start(apNetif_);
    return err == ESP_OK;
}

bool WiFiClass::softAP(const char *ssid, const char *pass, int channelValue, int hidden, int maxConn)
{
    ensure();
    wifi_config_t cfg = {};
    std::snprintf(reinterpret_cast<char *>(cfg.ap.ssid), sizeof(cfg.ap.ssid), "%s", ssid ? ssid : "");
    std::snprintf(reinterpret_cast<char *>(cfg.ap.password), sizeof(cfg.ap.password), "%s", pass ? pass : "");
    cfg.ap.ssid_len = std::strlen(reinterpret_cast<const char *>(cfg.ap.ssid));
    cfg.ap.channel = channelValue;
    cfg.ap.max_connection = maxConn;
    cfg.ap.ssid_hidden = hidden;
    cfg.ap.authmode = pass && std::strlen(pass) >= 8 ? WIFI_AUTH_WPA_WPA2_PSK : WIFI_AUTH_OPEN;
    esp_wifi_set_config(WIFI_IF_AP, &cfg);
    esp_wifi_start();
    return true;
}

void WiFiClass::begin(const char *ssid, const char *pass)
{
    ensure();
    wifiStaStatus = WL_IDLE_STATUS;
    wifiLastDisconnectReason = 0;
    // Ensure the STA interface is enabled before configuring it.
    // If only AP mode is active, esp_wifi_set_config(WIFI_IF_STA, ...) silently fails.
    wifi_mode_t curMode = WIFI_MODE_NULL;
    if (esp_wifi_get_mode(&curMode) != ESP_OK || (curMode != WIFI_MODE_STA && curMode != WIFI_MODE_APSTA))
    {
        wifi_mode_t target = (curMode == WIFI_MODE_AP) ? WIFI_MODE_APSTA : WIFI_MODE_STA;
        esp_wifi_set_mode(target);
        esp_wifi_start();
    }
    wifi_ap_record_t ap = {};
    if (esp_wifi_sta_get_ap_info(&ap) == ESP_OK)
        esp_wifi_disconnect();
    wifi_config_t cfg = {};
    std::snprintf(reinterpret_cast<char *>(cfg.sta.ssid), sizeof(cfg.sta.ssid), "%s", ssid ? ssid : "");
    std::snprintf(reinterpret_cast<char *>(cfg.sta.password), sizeof(cfg.sta.password), "%s", pass ? pass : "");
    cfg.sta.scan_method = WIFI_ALL_CHANNEL_SCAN;
    cfg.sta.sort_method = WIFI_CONNECT_AP_BY_SIGNAL;
    cfg.sta.threshold.authmode = WIFI_AUTH_OPEN;
    cfg.sta.pmf_cfg.capable = true;
    cfg.sta.pmf_cfg.required = false;
#ifdef WPA3_SAE_PWE_BOTH
    cfg.sta.sae_pwe_h2e = WPA3_SAE_PWE_BOTH;
#endif
    cfg.sta.failure_retry_cnt = 1;
    esp_wifi_set_config(WIFI_IF_STA, &cfg);
    esp_wifi_start();
    esp_wifi_connect();
}

wl_status_t WiFiClass::status()
{
    wifi_ap_record_t ap = {};
    if (esp_wifi_sta_get_ap_info(&ap) == ESP_OK)
    {
        esp_netif_ip_info_t ip = {};
        if (staNetif_ && esp_netif_get_ip_info(staNetif_, &ip) == ESP_OK && ip.ip.addr != 0)
        {
            wifiStaStatus = WL_CONNECTED;
            return WL_CONNECTED;
        }
        return wifiStaStatus == WL_CONNECTED ? WL_IDLE_STATUS : wifiStaStatus;
    }
    return wifiStaStatus;
}

uint8_t WiFiClass::lastDisconnectReason() const
{
    return wifiLastDisconnectReason;
}

const char *WiFiClass::lastDisconnectReasonName() const
{
    return wifiDisconnectReasonName(wifiLastDisconnectReason);
}

void WiFiClass::disconnect(bool wifioff, bool)
{
    esp_wifi_disconnect();
    wifiStaStatus = WL_DISCONNECTED;
    if (wifioff)
        esp_wifi_stop();
}

void WiFiClass::config(IPAddress local, IPAddress gateway, IPAddress subnet, IPAddress dns)
{
    ensure();
    uint32_t localRaw = static_cast<uint32_t>(local);
    if (localRaw == 0 || localRaw == IPADDR_NONE)
    {
        esp_netif_dhcpc_start(staNetif_);
        return;
    }

    esp_netif_dhcpc_stop(staNetif_);
    esp_netif_ip_info_t ip = {};
    ip.ip = local.raw();
    ip.gw = gateway.raw();
    ip.netmask = subnet.raw();
    esp_netif_set_ip_info(staNetif_, &ip);
    if ((uint32_t)dns != 0)
    {
        esp_netif_dns_info_t dnsInfo = {};
        dnsInfo.ip.type = ESP_IPADDR_TYPE_V4;
        dnsInfo.ip.u_addr.ip4 = dns.raw();
        esp_netif_set_dns_info(staNetif_, ESP_NETIF_DNS_MAIN, &dnsInfo);
    }
}

IPAddress WiFiClass::localIP()
{
    ensure();
    esp_netif_ip_info_t ip = {};
    esp_netif_get_ip_info(staNetif_, &ip);
    return IPAddress(ip.ip.addr);
}

IPAddress WiFiClass::softAPIP()
{
    ensure();
    esp_netif_ip_info_t ip = {};
    esp_netif_get_ip_info(apNetif_, &ip);
    return IPAddress(ip.ip.addr);
}

int WiFiClass::softAPgetStationNum()
{
    wifi_sta_list_t list = {};
    return esp_wifi_ap_get_sta_list(&list) == ESP_OK ? list.num : 0;
}

int WiFiClass::scanNetworks(bool, bool, bool, uint32_t)
{
    ensure();
    scanRecords_.clear();
    wifi_mode_t mode = WIFI_MODE_NULL;
    if (esp_wifi_get_mode(&mode) == ESP_OK && mode == WIFI_MODE_AP)
        esp_wifi_set_mode(WIFI_MODE_APSTA);
    if (esp_wifi_scan_start(nullptr, true) != ESP_OK)
        return 0;
    uint16_t count = 0;
    esp_wifi_scan_get_ap_num(&count);
    scanRecords_.resize(count);
    if (count)
        esp_wifi_scan_get_ap_records(&count, scanRecords_.data());
    scanRecords_.resize(count);
    return static_cast<int>(scanRecords_.size());
}

String WiFiClass::SSID(int index)
{
    if (index < 0 || static_cast<size_t>(index) >= scanRecords_.size())
        return "";
    return reinterpret_cast<const char *>(scanRecords_[index].ssid);
}

String WiFiClass::SSID()
{
    wifi_ap_record_t ap = {};
    if (esp_wifi_sta_get_ap_info(&ap) != ESP_OK)
        return "";
    return reinterpret_cast<const char *>(ap.ssid);
}

int32_t WiFiClass::RSSI(int index)
{
    return (index >= 0 && static_cast<size_t>(index) < scanRecords_.size()) ? scanRecords_[index].rssi : 0;
}

wifi_auth_mode_t WiFiClass::encryptionType(int index)
{
    return (index >= 0 && static_cast<size_t>(index) < scanRecords_.size()) ? scanRecords_[index].authmode : WIFI_AUTH_OPEN;
}

int32_t WiFiClass::channel(int index)
{
    return (index >= 0 && static_cast<size_t>(index) < scanRecords_.size()) ? scanRecords_[index].primary : 0;
}

void WiFiClass::scanDelete()
{
    scanRecords_.clear();
}

size_t WiFiClient::readBytes(uint8_t *buf, size_t len)
{
    size_t available = data_.size() - std::min(offset_, data_.size());
    size_t count = std::min(len, available);
    if (count)
    {
        std::memcpy(buf, data_.data() + offset_, count);
        offset_ += count;
    }
    return count;
}

bool HTTPClient::begin(WiFiClientSecure &, const String &url)
{
    url_ = url;
    response_ = "";
    return true;
}

int HTTPClient::GET()
{
    esp_http_client_config_t cfg = {};
    cfg.url = url_.c_str();
    cfg.timeout_ms = timeoutMs_;
    cfg.skip_cert_common_name_check = true;
    esp_http_client_handle_t client = esp_http_client_init(&cfg);
    if (!client)
        return -1;
    yield();
    esp_err_t err = esp_http_client_perform(client);
    yield();
    int status = esp_http_client_get_status_code(client);
    if (err == ESP_OK)
    {
        int len = esp_http_client_get_content_length(client);
        std::string body;
        char buf[512];
        int read = 0;
        while ((read = esp_http_client_read(client, buf, sizeof(buf))) > 0)
        {
            body.append(buf, read);
            yield();
        }
        if (body.empty() && len > 0)
        {
            body.resize(len);
            esp_http_client_read_response(client, body.data(), len);
            yield();
        }
        response_ = body;
        stream_ = WiFiClient(body);
    }
    esp_http_client_cleanup(client);
    return status;
}

WiFiClient *HTTPClient::getStreamPtr()
{
    return &stream_;
}

void HTTPClient::end()
{
    response_ = "";
}

void UpdateClass::setError(const char *message)
{
    error_ = true;
    errorText_ = message ? message : "OTA error";
}

bool UpdateClass::begin(size_t)
{
    abort();
    partition_ = esp_ota_get_next_update_partition(nullptr);
    if (!partition_)
    {
        setError("No OTA partition");
        return false;
    }
    esp_err_t err = esp_ota_begin(partition_, OTA_SIZE_UNKNOWN, &handle_);
    if (err != ESP_OK)
    {
        setError(esp_err_to_name(err));
        return false;
    }
    running_ = true;
    finished_ = false;
    error_ = false;
    errorText_.clear();
    return true;
}

size_t UpdateClass::write(const uint8_t *buf, size_t len)
{
    if (!running_)
        return 0;
    esp_err_t err = esp_ota_write(handle_, buf, len);
    if (err != ESP_OK)
    {
        setError(esp_err_to_name(err));
        return 0;
    }
    return len;
}

size_t UpdateClass::writeStream(WiFiClient &stream)
{
    uint8_t buf[1024];
    size_t total = 0;
    size_t n = 0;
    while ((n = stream.readBytes(buf, sizeof(buf))) > 0)
    {
        size_t written = write(buf, n);
        total += written;
        if (written != n)
            break;
    }
    return total;
}

bool UpdateClass::end(bool)
{
    if (!running_)
        return false;
    esp_err_t err = esp_ota_end(handle_);
    running_ = false;
    if (err != ESP_OK)
    {
        setError(esp_err_to_name(err));
        return false;
    }
    err = esp_ota_set_boot_partition(partition_);
    if (err != ESP_OK)
    {
        setError(esp_err_to_name(err));
        return false;
    }
    finished_ = true;
    return true;
}

void UpdateClass::abort()
{
    if (running_)
        esp_ota_abort(handle_);
    running_ = false;
    finished_ = false;
    handle_ = 0;
    partition_ = nullptr;
}

void WebServer::on(const char *uriValue, http_method methodValue, Handler handler)
{
    routes_.push_back({uriValue, methodValue, std::move(handler), nullptr});
}

void WebServer::on(const char *uriValue, http_method methodValue, Handler handler, Handler uploadHandler)
{
    routes_.push_back({uriValue, methodValue, std::move(handler), std::move(uploadHandler)});
}

void WebServer::begin()
{
    if (handle_)
        return;
    httpd_config_t cfg = HTTPD_DEFAULT_CONFIG();
    cfg.server_port = port_;
    cfg.uri_match_fn = httpd_uri_match_wildcard;
    cfg.max_uri_handlers = 16;
    cfg.max_open_sockets = 8;
    cfg.backlog_conn = 8;
    cfg.lru_purge_enable = true;
    cfg.stack_size = 16384;
    cfg.recv_wait_timeout = 10;
    cfg.send_wait_timeout = 10;
    if (httpd_start(&handle_, &cfg) != ESP_OK)
        return;
    httpd_uri_t get = {};
    get.uri = "/*";
    get.method = HTTP_GET;
    get.handler = &WebServer::dispatch;
    get.user_ctx = this;
    httpd_register_uri_handler(handle_, &get);
    httpd_uri_t post = get;
    post.method = HTTP_POST;
    httpd_register_uri_handler(handle_, &post);
}

esp_err_t WebServer::dispatch(httpd_req_t *req)
{
    auto *self = static_cast<WebServer *>(req->user_ctx);
    return self ? self->handle(req) : ESP_FAIL;
}

const WebServer::Route *WebServer::findRoute(const char *uriValue, httpd_method_t methodValue) const
{
    for (const auto &route : routes_)
    {
        if (route.method == methodValue && route.uri == uriValue)
            return &route;
    }
    return nullptr;
}

void WebServer::parseArgs(const std::string &query)
{
    size_t start = 0;
    while (start < query.size())
    {
        size_t end = query.find('&', start);
        if (end == std::string::npos)
            end = query.size();
        size_t eq = query.find('=', start);
        if (eq != std::string::npos && eq < end)
            args_[urlDecode(query.substr(start, eq - start))] = urlDecode(query.substr(eq + 1, end - eq - 1)).c_str();
        start = end + 1;
    }
}

void WebServer::readPostBody(httpd_req_t *req)
{
    if (req->content_len == 0)
        return;
    std::string body(req->content_len, '\0');
    size_t offset = 0;
    while (offset < body.size())
    {
        int got = httpd_req_recv(req, body.data() + offset, body.size() - offset);
        if (got <= 0)
            break;
        offset += got;
    }
    body.resize(offset);
    args_["plain"] = body.c_str();
    parseArgs(body);
}

bool WebServer::readUploadBody(httpd_req_t *req, const Route &route)
{
    if (!route.uploadHandler)
        return true;

    std::string filename = "firmware.bin";
    size_t filenameLen = httpd_req_get_hdr_value_len(req, "X-File-Name");
    if (filenameLen > 0)
    {
        std::vector<char> header(filenameLen + 1, 0);
        if (httpd_req_get_hdr_value_str(req, "X-File-Name", header.data(), header.size()) == ESP_OK && header[0])
            filename = header.data();
    }

    upload_ = {};
    upload_.status = UPLOAD_FILE_START;
    upload_.filename = filename.c_str();
    upload_.totalSize = 0;
    route.uploadHandler();

    uint8_t buf[2048];
    size_t remaining = req->content_len;
    while (remaining > 0)
    {
        size_t want = std::min(remaining, sizeof(buf));
        int got = httpd_req_recv(req, reinterpret_cast<char *>(buf), want);
        if (got == HTTPD_SOCK_ERR_TIMEOUT)
            continue;
        if (got <= 0)
        {
            upload_.status = UPLOAD_FILE_ABORTED;
            upload_.buf = nullptr;
            upload_.currentSize = 0;
            route.uploadHandler();
            return false;
        }

        upload_.status = UPLOAD_FILE_WRITE;
        upload_.buf = buf;
        upload_.currentSize = static_cast<size_t>(got);
        upload_.totalSize += static_cast<size_t>(got);
        route.uploadHandler();
        remaining -= static_cast<size_t>(got);
        yield();
    }

    upload_.status = UPLOAD_FILE_END;
    upload_.buf = nullptr;
    upload_.currentSize = 0;
    route.uploadHandler();
    return true;
}

esp_err_t WebServer::handle(httpd_req_t *req)
{
    currentReq_ = req;
    std::string routeUri = req->uri;
    size_t queryPos = routeUri.find('?');
    if (queryPos != std::string::npos)
        routeUri.resize(queryPos);
    currentUri_ = routeUri.c_str();
    args_.clear();
    headers_.clear();
    upload_ = {};

    size_t qlen = httpd_req_get_url_query_len(req);
    if (qlen > 0)
    {
        std::vector<char> query(qlen + 1, 0);
        if (httpd_req_get_url_query_str(req, query.data(), query.size()) == ESP_OK)
            parseArgs(query.data());
    }

    const Route *route = findRoute(routeUri.c_str(), static_cast<httpd_method_t>(req->method));
    if (!route || !route->handler)
    {
        send(404, "text/plain", "Not found");
        currentReq_ = nullptr;
        return ESP_OK;
    }
    if (req->method == HTTP_POST)
    {
        if (route->uploadHandler)
            readUploadBody(req, *route);
        else
            readPostBody(req);
    }
    route->handler();
    currentReq_ = nullptr;
    return ESP_OK;
}

bool WebServer::hasArg(const char *name) const
{
    return args_.find(name ? name : "") != args_.end();
}

String WebServer::arg(const char *name) const
{
    auto it = args_.find(name ? name : "");
    return it == args_.end() ? String("") : it->second;
}

void WebServer::applyHeaders()
{
    if (!currentReq_)
        return;
    for (const auto &header : headers_)
        httpd_resp_set_hdr(currentReq_, header.first.c_str(), header.second.c_str());
}

void WebServer::send(int code, const char *type, const String &body)
{
    sendRaw(code, type, body.c_str(), body.length());
}

void WebServer::sendRaw(int code, const char *type, const char *body, size_t len)
{
    if (!currentReq_)
        return;
    httpd_resp_set_status(currentReq_, code == 200 ? "200 OK" : code == 400 ? "400 Bad Request"
                                                            : code == 404   ? "404 Not Found"
                                                            : code == 500   ? "500 Internal Server Error"
                                                                            : "200 OK");
    httpd_resp_set_type(currentReq_, type);
    applyHeaders();

    constexpr size_t kChunk = 4096;
    if (!body || len == 0)
    {
        httpd_resp_send(currentReq_, nullptr, 0);
        return;
    }
    if (len <= kChunk)
    {
        httpd_resp_send(currentReq_, body, len);
        return;
    }
    size_t offset = 0;
    while (offset < len)
    {
        size_t n = std::min(kChunk, len - offset);
        if (httpd_resp_send_chunk(currentReq_, body + offset, n) != ESP_OK)
            return;
        offset += n;
    }
    httpd_resp_send_chunk(currentReq_, nullptr, 0);
}

void WebServer::sendHeader(const char *name, const char *value)
{
    headers_.push_back({name ? name : "", value ? value : ""});
}

void WebServer::streamFile(File &file, const char *type)
{
    if (!currentReq_)
        return;
    httpd_resp_set_type(currentReq_, type);
    applyHeaders();
    uint8_t buf[512];
    size_t n = 0;
    while ((n = file.read(buf, sizeof(buf))) > 0)
        httpd_resp_send_chunk(currentReq_, reinterpret_cast<const char *>(buf), n);
    httpd_resp_send_chunk(currentReq_, nullptr, 0);
}

bool WebServer::authenticate(const char *, const char *)
{
    return true;
}

void WebServer::requestAuthentication()
{
    sendHeader("WWW-Authenticate", "Basic realm=\"ev-open-can-tools\"");
    send(401, "text/plain", "Authentication required");
}

#endif
