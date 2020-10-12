#include "Aoo.hpp"
#include "aoo/aoo_net.hpp"
#include "common/time.hpp"

#include "oscpack/osc/OscReceivedElements.h"

#include <string>
#include <thread>

class AooClient : public INodeClient {
public:
    AooClient(World *world, int32_t port);
    ~AooClient();

    void doSend() override;

    void doHandleMessage(const char* data, int32_t size,
                         void* endpoint, aoo_replyfn fn) override;

    void doUpdate() override;

    void connect(const char* host, int port,
                 const char* user, const char* pwd);

    void disconnect();

    void joinGroup(const char* name, const char* pwd);

    void leaveGroup(const char* name);

    aoo::net::iclient * client() {
        return client_.get();
    }

    void forwardMessage(const char *data, int32_t size,
                        aoo::time_tag time);
private:
    World* world_;
    aoo::net::iclient::pointer client_;
    int32_t port_;
    std::thread thread_;
    std::thread::id nrtThread_;

    void handleEvent(const aoo_event* e);

    void sendReply(const char *cmd, bool success,
                   const char *errmsg = nullptr);

    void sendGroupReply(const char* cmd, const char *group,
                        bool success, const char* errmsg = nullptr);

    void handlePeerMessage(const char *data, int32_t size,
                           const aoo::ip_address& address, aoo::time_tag t);

    void handlePeerBundle(const osc::ReceivedBundle& bundle,
                          const aoo::ip_address& address);
};

struct AooClientCmd {
    int port;
};

struct ConnectCmd : AooClientCmd {
    char serverName[256];
    int32_t serverPort;
    char userName[64];
    char userPwd[64];
};

struct GroupCmd : AooClientCmd {
    char name[64];
    char pwd[64];
};

struct GroupRequest {
    AooClient* obj;
    std::string group;
};
