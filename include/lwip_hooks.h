#pragma once
// LWIP IP-layer hook header.
// ESP-IDF lwIP includes this file when ESP_IDF_LWIP_HOOK_FILENAME is defined
// at compile time (see waveshare_ESP32_S3_RS485_CAN env in platformio.ini).
//
// Wires the per-packet "can forward" hook to dashGatewayHookIp4CanForward().
// The current gateway build keeps DNS filtering domain-only, so this hook
// always allows forwarding and stays out of the NAPT hot path.
//
// Compiles down to no-op when DASH_STA_AP_GATEWAY is not defined (e.g. for
// non-gateway boards that share this header path).

#ifdef DASH_STA_AP_GATEWAY

#ifdef __cplusplus
extern "C"
{
#endif

    // Returns nonzero (allow forwarding).
    // destAddrNbo is the destination IPv4 address in NETWORK byte order, taken
    // straight from ip4_addr_t::addr (which lwIP stores in NBO on ESP32).
    int dashGatewayHookIp4CanForward(unsigned int destAddrNbo);

#ifdef __cplusplus
}
#endif

// lwIP IP4_CANFORWARD hook. Called from ip4_forward() for every outbound IPv4
// packet that needs to be NAPT-forwarded across our AP↔STA bridge.
// In this ESP-IDF lwIP build, the second argument is already a u32_t in
// network byte order (see lwip/src/core/ipv4/ip4.c::ip4_canforward), so we
// pass it through unchanged.
#define LWIP_HOOK_IP4_CANFORWARD(p, dest) dashGatewayHookIp4CanForward(dest)

#endif // DASH_STA_AP_GATEWAY
