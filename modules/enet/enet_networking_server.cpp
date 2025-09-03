#include "enet_networking_server.h"

#include "core/string/print_string.h"

void ENetNetworkingServer::_bind_methods() {
}

void ENetNetworkingServer::init() {
}

void ENetNetworkingServer::finish() {
}

Ref<MultiplayerPeer> ENetNetworkingServer::create_enet_peer() {
	Ref<ENetMultiplayerPeer> peer;
	peer.instantiate();
	return peer;
}

Ref<MultiplayerPeer> ENetNetworkingServer::create_gamenetworking_sockets_peer() {
	ERR_PRINT("GameNetworkingSockets not supported by ENet networking server. Use GameNetworkingSockets networking server instead.");
	return Ref<MultiplayerPeer>();
}

ENetNetworkingServer::ENetNetworkingServer() {
}

ENetNetworkingServer::~ENetNetworkingServer() {
}