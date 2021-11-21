//
// Created by Aidan on 20/11/2021.
//

#ifndef SNMP_MANAGER_H
#define SNMP_MANAGER_H

#include "include/PollingInfo.h"
#include "include/SNMPPacket.h"
#include "include/ValueCallbacks.h"
#include <list>
#include <string>
#include <unordered_map>

#define SNMPREQUEST_VARBIND_COUNT 6

class SNMPManager {
  public:
    SNMPManager(){};

    SNMPManager(const char *community) : _defaultCommunity(community){};

    ValueCallback *addIntegerPoller(SNMPDevice *device, const char *oid, int *value, unsigned long pollingInterval = 30000);

    void removePoller(ValueCallback *callbackPoller, SNMPDevice *device);

    void begin();

    void loop();

    void setUDP(UDP *u);

  private:
    std::string _defaultCommunity = "public";

    ValueCallback *addCallbackPoller(SNMPDevice *device, ValueCallback *callback, unsigned long pollingInterval);

    std::deque<ValueCallbackContainer> pollingCallbacks;
    std::unordered_map<snmp_request_id_t, ASN_TYPE> liveRequests;

    uint8_t _packetBuffer[MAX_SNMP_PACKET_LENGTH] = {0};

    void teardown_old_requests();

    UDP *udp;

    snmp_request_id_t
    send_polling_request(const SNMPDevice *constdevice, std::array<ValueCallbackContainer *, SNMPREQUEST_VARBIND_COUNT> &callbacks);

    snmp_request_id_t prepare_next_polling_request();

    SNMP_ERROR_RESPONSE process_incoming_packets();

    static bool responseCallback(std::shared_ptr<OIDType> responseOID, bool success, int errorStatus,
                                 const ValueCallbackContainer &container);
    unsigned long last_processed;
};

#endif//SNMP_MANAGER_H
