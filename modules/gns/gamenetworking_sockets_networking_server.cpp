#include "gamenetworking_sockets_networking_server.h"

#include "core/string/print_string.h"

void GameNetworkingSocketsNetworkingServer::_bind_methods() {
}

void GameNetworkingSocketsNetworkingServer::init() {
}

void GameNetworkingSocketsNetworkingServer::finish() {
}

Ref<MultiplayerPeer> GameNetworkingSocketsNetworkingServer::create_enet_peer() {
	ERR_PRINT("ENet not supported by GameNetworkingSockets networking server. Use ENet networking server instead.");
	return Ref<MultiplayerPeer>();
}

Ref<MultiplayerPeer> GameNetworkingSocketsNetworkingServer::create_gamenetworking_sockets_peer() {
	Ref<GameNetworkingSocketsPeer> peer;
	peer.instantiate();
	return peer;
}

GameNetworkingSocketsNetworkingServer::GameNetworkingSocketsNetworkingServer() {
}

GameNetworkingSocketsNetworkingServer::~GameNetworkingSocketsNetworkingServer() {
}