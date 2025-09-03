#ifndef ENET_NETWORKING_SERVER_H
#define ENET_NETWORKING_SERVER_H

#include "servers/networking_server.h"
#include "enet_multiplayer_peer.h"

class ENetNetworkingServer : public NetworkingServer {
	GDCLASS(ENetNetworkingServer, NetworkingServer);

protected:
	static void _bind_methods();

public:
	virtual void init() override;
	virtual void finish() override;

	virtual Ref<MultiplayerPeer> create_enet_peer() override;
	virtual Ref<MultiplayerPeer> create_gamenetworking_sockets_peer() override;

	ENetNetworkingServer();
	~ENetNetworkingServer();
};

#endif // ENET_NETWORKING_SERVER_H