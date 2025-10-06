#include "Aoo.hpp"

#include "common/time.hpp"

#include "oscpack/osc/OscReceivedElements.h"

#include <string>
#include <thread>

namespace sc {

class AooClient {
public:
    AooClient(int32_t port);
    ~AooClient();

    void connect(int token, const char* host, int port, const char* pwd,
                 const AooData *metadata);

    void disconnect(int token);

    void joinGroup(int token, const char* groupName, const char *groupPwd,
                   const AooData *groupMetadata, const char* userName,
                   const char *userPwd, const AooData *userMetadata,
                   const AooIpEndpoint *relayAddress);

    void leaveGroup(int token, AooId group);

    void updateGroup(int token, AooId group, const AooData& groupMetadata);

    void updateUser(int token, AooId group, const AooData& userMetadata);

    void handleEvent(const AooEvent* e);

    void setPingInterval(AooSeconds s) {
        AooPingSettings settings;
        node_->client()->getPeerPingSettings(settings);
        settings.interval = s;
        node_->client()->setPeerPingSettings(settings);
    }

    void setPacketSize(AooInt32 size) {
        node_->client()->setPacketSize(size);
    }

    // simulate bad network
    void setSimulatePacketLoss(float pct) {
        node_->client()->control(kAooCtlSetSimulatePacketLoss, 0, AOO_ARG(pct));
    }

    void setSimulatePacketReorder(AooSeconds s) {
        node_->client()->control(kAooCtlSetSimulatePacketReorder, 0, AOO_ARG(s));
    }

    void setSimulatePacketJitter(AooBool b) {
        node_->client()->control(kAooCtlSetSimulatePacketJitter, 0, AOO_ARG(b));
    }

private:
    std::shared_ptr<INode> node_;

    void sendReply(const osc::OutboundPacketStream& msg) {
        node_->sendReply(msg);
    }

    void sendError(const char *cmd, AooId token,
                   AooError error, const AooResponse& response) {
        sendError(cmd, token, error, response.error.errorCode,
                  response.error.errorMessage);
    }

    void sendError(const char *cmd, AooId token, AooError error,
                   int code = 0, const char *msg = "");

    void handlePeerMessage(const AooEventPeerMessage& msg);
};

struct AooClientCmd {
    World *world;
    int port;
    int token;
};

struct ConnectCmd : AooClientCmd {
    ~ConnectCmd();

    char serverHost[256];
    int32_t serverPort;
    char password[64];
    AooData metadata;
};

struct GroupJoinCmd : AooClientCmd {
    ~GroupJoinCmd();

    char groupName[64];
    char userName[64];
    char groupPwd[64];
    char userPwd[64];
    AooData groupMetadata;
    AooData userMetadata;
    AooIpEndpoint relayAddress;
};

struct GroupLeaveCmd : AooClientCmd {
    AooId group;
};

struct UpdateCmd : AooClientCmd {
    ~UpdateCmd();

    AooId groupID;
    AooData metadata;
};

struct ControlCmd : AooClientCmd {
    union {
        int i;
        float f;
    };
};

struct RequestData {
    AooClient *client;
    AooId token;
};

} // namespace sc
