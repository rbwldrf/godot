// Stub implementations for GameNetworkingSockets functions to allow linking
// This allows us to test the core networking improvements without full GNS dependency resolution

#include <cstdint>
#include <cstddef>
#include <string>

// Protobuf stubs
namespace google {
namespace protobuf {
namespace internal {

class LogMessage {
public:
    LogMessage(int level, const char* file, int line) {}
    ~LogMessage() {}
    LogMessage& operator<<(const char* msg) { return *this; }
};

class LogFinisher {
public:
    void operator=(LogMessage& msg) {}
};

const char fixed_address_empty_string[] = "";

} // namespace internal

class MessageLite {
public:
    virtual ~MessageLite() {}
    bool ParseFromArray(const void* data, int size) { return false; }
    bool ParseFromString(const std::string& data) { return false; }
    bool SerializeWithCachedSizesToArray(uint8_t* target) const { return false; }
};

class Message : public MessageLite {
public:
    virtual ~Message() {}
    std::string GetTypeName() const { return "stub"; }
    void MergeFrom(const Message& other) {}
    bool CheckTypeAndMergeFrom(const MessageLite& other) { return false; }
    std::string InitializationErrorString() const { return ""; }
    size_t SpaceUsedLong() const { return 0; }
    static void CopyWithSourceCheck(Message& dest, const Message& src) {}
};

template<typename T>
class RepeatedField {
public:
    void Reserve(int size) {}
};

class ArenaStringPtr {
public:
    std::string* Mutable(void* arena) { static std::string s; return &s; }
};

} // namespace protobuf
} // namespace google

// GameNetworkingSockets stubs
extern "C" {

// Main API entry points that were missing
bool GameNetworkingSockets_Init() {
    return true;
}

void* SteamNetworkingUtils_LibV4() {
    // Return a basic utils interface implementation
    static char dummy_utils[1024] = {0};
    return dummy_utils;
}

void* SteamNetworkingSockets_LibV12() {
    // Return a basic sockets interface implementation
    static char dummy_sockets[1024] = {0};
    return dummy_sockets;
}

// Protobuf message stubs  
struct CMsgSteamDatagramCertificate {
    CMsgSteamDatagramCertificate() {}
    ~CMsgSteamDatagramCertificate() {}
    void Clear() {}
};

struct CMsgSteamDatagramCertificateSigned {  
    CMsgSteamDatagramCertificateSigned() {}
    ~CMsgSteamDatagramCertificateSigned() {}
    void Clear() {}
};

// Crypto stubs
struct CEC25519PrivateKeyBase {
    virtual ~CEC25519PrivateKeyBase() {}
    void Wipe() {}
};

struct CEC25519PublicKeyBase {
    virtual ~CEC25519PublicKeyBase() {}
};

struct CECSigningPrivateKey : CEC25519PrivateKeyBase {
    virtual ~CECSigningPrivateKey() {}
};

struct CECSigningPublicKey : CEC25519PublicKeyBase {
    virtual ~CECSigningPublicKey() {}
};

} // extern "C"

// Additional stubs for missing GameNetworkingSockets internal functions
namespace SteamNetworkingSocketsLib {

class CMessagesEndPoint {
public:
    static void DestroyMessagesEndPoint() {}
};

class CSteamNetworkConnectionBase {
public:
    void SNP_ConfigureLanes(int lanes, const int* priorities, const uint16_t* weights) {}
};

// Note: GlobalConfig symbols and g_tables_lock are already defined in the main library
// Only add symbols that are actually missing

} // namespace SteamNetworkingSocketsLib