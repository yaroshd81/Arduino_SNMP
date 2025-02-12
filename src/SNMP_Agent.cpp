#include "SNMP_Agent.h"

const char* SNMP_TAG = "SNMP";

bool SNMPAgent::setUDP(UDP* udp){
    this->_udp.push_back(udp);
    this->begin();
    return true;
}

bool SNMPAgent::begin(){
    this->restartUDP();
    return true;
}

bool SNMPAgent::begin(const char* prefix){
    this->oidPrefix = prefix;
    return this->begin();
}

void SNMPAgent::stop(){
    for(auto udp : _udp){
        udp->stop();
    }
}

SNMP_ERROR_RESPONSE SNMPAgent::loop(){
    for(auto udp : _udp){
        delay(1);
        int packetLength = udp->parsePacket();
        if(packetLength > 0){
            SNMP_LOGD("Received packet from: %s, of size: %d", udp->remoteIP().toString().c_str(), packetLength);

            if(packetLength < 0 || packetLength > MAX_SNMP_PACKET_LENGTH){
                SNMP_LOGW("Incoming packet too large: %d\n", packetLength);
                return SNMP_REQUEST_TOO_LARGE;
            }

            memset(_packetBuffer, 0, MAX_SNMP_PACKET_LENGTH);

            int readBytes = udp->read(_packetBuffer, packetLength);
            if(readBytes != packetLength){
                SNMP_LOGW("Packet length mismatch: expected: %d, actual: %d\n", packetLength, readBytes);
                return SNMP_REQUEST_INVALID;
            }

            int reponseLength = 0;
            
            bool flagSaveIpForSet = false;
            SNMP_ERROR_RESPONSE response = handlePacket(_packetBuffer, packetLength, &reponseLength, MAX_SNMP_PACKET_LENGTH, callbacks, _community, _readOnlyCommunity, flagSaveIpForSet, informCallback, (void*)this);
            if(response > 0 && response != SNMP_INFORM_RESPONSE_OCCURRED){
                // send it
                if (flagSaveIpForSet)
                {
                    fromIP = udp->remoteIP();
                }
                SNMP_LOGD("Built packet, sending back response to: %s, %d\n", udp->remoteIP().toString().c_str(), udp->remotePort());
                udp->beginPacket(udp->remoteIP(), udp->remotePort());
                udp->write(_packetBuffer, reponseLength);

                if(!udp->endPacket()){
                    SNMP_LOGW("Failed to send response packet\n");
                }
            }

            if(response == SNMP_SET_OCCURRED){
                setOccurred = true;
            }

            this->handleInformQueue();
            return response;
        }
    }
    
    this->handleInformQueue();
    return SNMP_NO_PACKET;
}

SortableOIDType* SNMPAgent::buildOIDWithPrefix(const char* oid, bool overwritePrefix){
    SortableOIDType* newOid;
    if(this->oidPrefix.length() && !overwritePrefix){
        std::string temp;
        temp.append(this->oidPrefix);
        temp.append(oid);
        newOid = new SortableOIDType(temp);
    } else {
        newOid = new SortableOIDType(oid);
    }
    if(newOid->valid){
        return newOid;
    }
    delete newOid;
    return nullptr;
}

ValueCallback* SNMPAgent::addReadWriteStringHandler(const char* oid, char** value, size_t max_len, bool isSettable, bool overwritePrefix){
    if(!value || !*value) return nullptr;

    SortableOIDType* oidType = buildOIDWithPrefix(oid, overwritePrefix);
    if(!oidType) return nullptr;
    return addHandler(new StringCallback(oidType, value, max_len), isSettable);
}

ValueCallback *SNMPAgent::addReadOnlyStaticStringHandler(const char* oid, std::string value, bool overwritePrefix) {
    SortableOIDType* oidType = buildOIDWithPrefix(oid, overwritePrefix);
    if(!oidType) return nullptr;
    return addHandler(new ReadOnlyStringCallback(oidType, value), false);
}

ValueCallback *SNMPAgent::addIpAddressHandler(const char* oid, IPAddress* value, bool overwritePrefix)
{
    SortableOIDType* oidType = buildOIDWithPrefix(oid, overwritePrefix);
    if(!oidType) return nullptr;
    return addHandler(new IpAddressCallback(oidType, value), false);
}

ValueCallback* SNMPAgent::addOpaqueHandler(const char* oid, uint8_t* value, size_t data_len, bool isSettable, bool overwritePrefix){
    if(!value) return nullptr;

    SortableOIDType* oidType = buildOIDWithPrefix(oid, overwritePrefix);
    if(!oidType) return nullptr;
    return addHandler(new OpaqueCallback(oidType, value, data_len), isSettable);
}

ValueCallback* SNMPAgent::addIntegerHandler(const char* oid, int* value, const int minValue, const int maxValue, bool isSettable, bool overwritePrefix){
    if(!value) return nullptr;

    SortableOIDType* oidType = buildOIDWithPrefix(oid, overwritePrefix);
    if(!oidType) return nullptr;
    return addHandler(new IntegerCallback(oidType, value, minValue, maxValue), isSettable);
}

ValueCallback* SNMPAgent::addTimestampHandler(const char* oid, uint32_t* value, bool isSettable, bool overwritePrefix){
    if(!value) return nullptr;

    SortableOIDType* oidType = buildOIDWithPrefix(oid, overwritePrefix);
    if(!oidType) return nullptr;
    return addHandler(new TimestampCallback(oidType, value), isSettable);
}

ValueCallback* SNMPAgent::addOIDHandler(const char* oid, std::string value, bool overwritePrefix){
    SortableOIDType* oidType = buildOIDWithPrefix(oid, overwritePrefix);
    if(!oidType) return nullptr;
    return addHandler(new OIDCallback(oidType, value), false);
}

ValueCallback* SNMPAgent::addCounter64Handler(const char* oid, uint64_t* value, bool overwritePrefix){
    if(!value) return nullptr;

    SortableOIDType* oidType = buildOIDWithPrefix(oid, overwritePrefix);
    if(!oidType) return nullptr;
    return addHandler(new Counter64Callback(oidType, value), false);
}

ValueCallback* SNMPAgent::addCounter32Handler(const char* oid, uint32_t* value, bool overwritePrefix){
    if(!value) return nullptr;

    SortableOIDType* oidType = buildOIDWithPrefix(oid, overwritePrefix);
    if(!oidType) return nullptr;
    return addHandler(new Counter32Callback(oidType, value), false);
}

ValueCallback* SNMPAgent::addGuageHandler(const char* oid, uint32_t* value, bool overwritePrefix){
    if(!value) return nullptr;

    SortableOIDType* oidType = buildOIDWithPrefix(oid, overwritePrefix);
    if(!oidType) return nullptr;
    return addHandler(new Guage32Callback(oidType, value), false);
}

ValueCallback * SNMPAgent::addHandler(ValueCallback *callback, bool isSettable) {
    callback->isSettable = isSettable;
    this->callbacks.push_back(callback);
    return callback;
}

bool SNMPAgent::removeHandler(ValueCallback* callback){ // this will remove the callback from the list and shift everything in the list back so there are no gaps, this will not delete the actual callback
    remove_handler(this->callbacks, callback);
    return true;
}

bool SNMPAgent::sortHandlers(){
    sort_handlers(this->callbacks);
    return true;
}

snmp_request_id_t SNMPAgent::sendTrapTo(SNMPTrap* trap, const IPAddress& ip, CallbackFunctionSendStatus callbackFunctionSendStatus, bool replaceQueuedRequests, int retries, int delay_ms){
    return queue_and_send_trap(this->informList, trap, ip, replaceQueuedRequests, retries, delay_ms, callbackFunctionSendStatus);
}

void SNMPAgent::informCallback(void* ctx, snmp_request_id_t requestID, bool responseReceiveSuccess){
    if(!ctx) return;
    SNMPAgent* agent = static_cast<SNMPAgent*>(ctx);

    return inform_callback(agent->informList, requestID, responseReceiveSuccess);
}

void SNMPAgent::handleInformQueue(){
    handle_inform_queue(this->informList);
}

void SNMPAgent::markTrapDeleted(SNMPTrap* trap){
    for(auto agent : SNMPAgent::agents){
        mark_trap_deleted(agent->informList, trap);
    }
}

std::list<SNMPAgent*> SNMPAgent::agents = std::list<SNMPAgent*>();

bool SNMPAgent::restartUDP() {
    for(auto udp : _udp){
        udp->stop();
        udp->begin(_AgentUDPport);
    }
    return true;
}
