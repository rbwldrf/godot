#include "register_types.h"

#include "core/object/class_db.h"
#include "gamenetworking_sockets_peer.h"
#include "gamenetworking_sockets_networking_server.h"

#include "servers/networking_server.h"

static NetworkingServer *_createGNSNetworkingServerCallback() {
	return memnew(GameNetworkingSocketsNetworkingServer);
}

void initialize_gns_module(ModuleInitializationLevel p_level) {
	if (p_level == MODULE_INITIALIZATION_LEVEL_SERVERS) {
		NetworkingServerManager::get_singleton()->register_server("GameNetworkingSockets", callable_mp_static(_createGNSNetworkingServerCallback));
		return;
	}

	if (p_level != MODULE_INITIALIZATION_LEVEL_SCENE) {
		return;
	}
	
	GDREGISTER_CLASS(GameNetworkingSocketsPeer);
	GDREGISTER_CLASS(GameNetworkingSocketsNetworkingServer);
}

void uninitialize_gns_module(ModuleInitializationLevel p_level) {
	if (p_level != MODULE_INITIALIZATION_LEVEL_SCENE) {
		return;
	}
}