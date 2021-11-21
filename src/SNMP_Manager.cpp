//
// Created by Aidan on 20/11/2021.
//

#ifdef COMPILING_TESTS
#include <tests/required/millis.h>
#endif

#include "SNMP_Manager.h"
#include "include/SNMPRequest.h"
#include "include/SNMPParser.h"

#define REQUEST_TIMEOUT 5000


void SNMPManager::loop() {
    // Put on a timer
    teardown_old_requests();
    prepare_next_polling_request();
    process_incoming_packets();
}

snmp_request_id_t SNMPManager::prepare_next_polling_request(){
    // Loop through our pollers, find the first one that needs to be polled again,
    // Use its device to et the next n callbacks, then send request
    SNMPDevice* device = nullptr;
    std::list<ValueCallbackContainer*> callbacks;
    int callbackCount = 0;
    for(auto& container : pollingCallbacks) {
        if(device == nullptr || container.agentDevice == device){
            if(container.pollingInfo->should_poll()){
                device = container.agentDevice;
                callbacks.push_back(&container);
                if(++callbackCount == SNMPREQUEST_VARBIND_COUNT) continue;
            }
        }
    }

    if(callbackCount > 0){
        return send_polling_request(device, callbacks);
    }

    return 0;
}

snmp_request_id_t SNMPManager::send_polling_request(SNMPDevice* device, std::list<ValueCallbackContainer*> callbacks){
    // Should only call if we know we're going to send a request
    SNMP_LOGD("send_polling_request: %lu", callbacks.size());
    SNMPRequest request(GetRequestPDU);

    request.setVersion(device->_version);
    request.setCommunityString(device->_community);

    for(const auto& callback : callbacks) {
        request.addValueCallback(callback->operator->());
    }

    memset(_packetBuffer, 0, MAX_SNMP_PACKET_LENGTH);
    int packetLength = request.serialiseInto(_packetBuffer, MAX_SNMP_PACKET_LENGTH);
    snmp_request_id_t request_id = request.requestID;
    if(packetLength > 0){
        // send
        SNMP_LOGD("Built request, sending to: %s, %d\n", device->_ip.toString().c_str(), device->_port);
        udp->beginPacket(device->_ip, device->_port);
        udp->write(_packetBuffer, packetLength);

        if(!udp->endPacket()){
            SNMP_LOGW("Failed to send response packet\n");
            return 0;
        }

        // Record tracking
        for(const auto& callback : callbacks) {
            callback->pollingInfo->send(request_id);
        }
        this->liveRequests.emplace_back(request_id, GetRequestPDU);
    }
    return request_id;
}

void SNMPManager::teardown_old_requests(){
    std::array<snmp_request_id_t, 10> cleared_request_ids;
    int cleared = 0;
    for(const auto& container : pollingCallbacks){
        if(cleared == 10) continue;
        if(container.pollingInfo->has_timed_out(REQUEST_TIMEOUT)){
            cleared_request_ids[cleared++] = container.pollingInfo->last_request_id;
            container.pollingInfo->reset_poller(false);
        }
    }
    liveRequests.remove_if([&](const AwaitingResponse& request) -> bool{
        return std::find(cleared_request_ids.begin(), cleared_request_ids.end(), request.requestId) != cleared_request_ids.end();
    });
}

void SNMPManager::begin() {
    udp->begin(162);
}

void SNMPManager::setUDP(UDP *u) {
    this->udp = u;
}

ValueCallback *SNMPManager::addIntegerPoller(SNMPDevice *device, char *oid, int *value, unsigned long pollingInterval) {
    if(!value) return nullptr;
    SNMP_LOGD("AddIntegerPoller");
    SortableOIDType* oidType = new SortableOIDType(std::string(oid));
    ValueCallback* callback = new IntegerCallback(oidType, value);

    return this->addCallbackPoller(device, callback, pollingInterval);
}

void SNMPManager::removePoller(ValueCallback *callbackPoller, SNMPDevice *device) {
    // remove poller
    auto it = std::remove_if(this->pollingCallbacks.begin(), this->pollingCallbacks.end(), [=](const ValueCallbackContainer& container){
        return callbackPoller == container.operator->() && device == container.agentDevice;
    });
    this->pollingCallbacks.erase(it, this->pollingCallbacks.end());
}

bool SNMPManager::responseCallback(std::shared_ptr<OIDType> responseOID, bool success, int errorStatus, ValueCallbackContainer& container){
    if(container){
        container.pollingInfo->reset_poller(success);
    } else {
        SNMP_LOGI("Unsolicited OID response: %s\n", responseOID->string().c_str());
        SNMP_LOGD("Error Status: %d\n", errorStatus);
    }
    return true;
}

SNMP_ERROR_RESPONSE SNMPManager::process_incoming_packets() {
    int packetLength = udp->parsePacket();
    if(packetLength > 0){
        SNMP_LOGD("Manager Received packet from: %s, of size: %d", udp->remoteIP().toString().c_str(), packetLength);

        if(packetLength > MAX_SNMP_PACKET_LENGTH){
            SNMP_LOGW("Incoming packet too large: %d\n", packetLength);
            return SNMP_REQUEST_TOO_LARGE;
        }

        memset(_packetBuffer, 0, MAX_SNMP_PACKET_LENGTH);

        int readBytes = udp->read(_packetBuffer, packetLength);
        if(readBytes != packetLength){
            SNMP_LOGW("Packet length mismatch: expected: %d, actual: %d\n", packetLength, readBytes);
            return SNMP_REQUEST_INVALID;
        }

        SNMPDevice incomingDevice = SNMPDevice(udp->remoteIP(), udp->remotePort());

        int reponseLength = 0;
        return handlePacket(_packetBuffer, packetLength, &reponseLength,
                                                    MAX_SNMP_PACKET_LENGTH, pollingCallbacks, "",
                                                    "", liveRequests, nullptr, &responseCallback, nullptr, incomingDevice);
    }
    return SNMP_NO_PACKET;
}