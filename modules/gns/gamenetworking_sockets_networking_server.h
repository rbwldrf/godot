#ifndef GAMENETWORKING_SOCKETS_NETWORKING_SERVER_H
#define GAMENETWORKING_SOCKETS_NETWORKING_SERVER_H

#include "servers/networking_server.h"
#include "gamenetworking_sockets_peer.h"

class GameNetworkingSocketsNetworkingServer : public NetworkingServer {
	GDCLASS(GameNetworkingSocketsNetworkingServer, NetworkingServer);

protected:
	static void _bind_methods();

public:
	virtual void init() override;
	virtual void finish() override;

	virtual Ref<MultiplayerPeer> create_enet_peer() override;
	virtual Ref<MultiplayerPeer> create_gamenetworking_sockets_peer() override;

	GameNetworkingSocketsNetworkingServer();
	~GameNetworkingSocketsNetworkingServer();
};

#endif // GAMENETWORKING_SOCKETS_NETWORKING_SERVER_H