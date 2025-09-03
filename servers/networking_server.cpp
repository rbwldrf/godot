#include "networking_server.h"

#include "core/config/project_settings.h"
#include "core/string/print_string.h"

NetworkingServer *NetworkingServer::singleton = nullptr;
NetworkingServerManager *NetworkingServerManager::singleton = nullptr;

const String NetworkingServerManager::setting_property_name = "network/backend/networking_engine";

NetworkingServer *NetworkingServer::get_singleton() {
	return singleton;
}

void NetworkingServer::_bind_methods() {
	ClassDB::bind_method(D_METHOD("create_enet_peer"), &NetworkingServer::create_enet_peer);
	ClassDB::bind_method(D_METHOD("create_gamenetworking_sockets_peer"), &NetworkingServer::create_gamenetworking_sockets_peer);
}

NetworkingServer::NetworkingServer() {
	singleton = this;
}

NetworkingServer::~NetworkingServer() {
	singleton = nullptr;
}

NetworkingServerManager *NetworkingServerManager::get_singleton() {
	return singleton;
}

void NetworkingServerManager::_bind_methods() {
	ClassDB::bind_method(D_METHOD("get_servers_count"), &NetworkingServerManager::get_servers_count);
	ClassDB::bind_method(D_METHOD("get_server_name", "index"), &NetworkingServerManager::get_server_name);
}

void NetworkingServerManager::register_server(const String &p_name, const Callable &p_create_callback) {
	ClassInfo ci;
	ci.name = p_name;
	ci.create_callback = p_create_callback;
	networking_servers.push_back(ci);
	on_servers_changed();
}

void NetworkingServerManager::set_default_server(const String &p_name, int p_priority) {
	for (int i = networking_servers.size() - 1; 0 <= i; --i) {
		if (networking_servers[i].name == p_name) {
			if (default_server_priority < p_priority) {
				default_server_id = i;
				default_server_priority = p_priority;
			}
		}
	}
}

NetworkingServer *NetworkingServerManager::new_server(const String &p_name) {
	int index = -1;
	if (p_name == "DEFAULT") {
		index = default_server_id;
	} else {
		for (int i = networking_servers.size() - 1; 0 <= i; --i) {
			if (networking_servers[i].name == p_name) {
				index = i;
				break;
			}
		}
	}

	ERR_FAIL_COND_V_MSG(index == -1, nullptr, "Invalid networking server name: " + p_name);

	Variant ret;
	Callable::CallError ce;
	networking_servers[index].create_callback.callp(nullptr, 0, ret, ce);
	ERR_FAIL_COND_V_MSG(ce.error != Callable::CallError::CALL_OK, nullptr, "Failed to create networking server.");

	return Object::cast_to<NetworkingServer>(ret.get_validated_object());
}

NetworkingServer *NetworkingServerManager::new_default_server() {
	ERR_FAIL_COND_V_MSG(default_server_id == -1, nullptr, "No default networking server registered.");
	return new_server("DEFAULT");
}

int NetworkingServerManager::get_servers_count() {
	return networking_servers.size();
}

String NetworkingServerManager::get_server_name(int p_index) {
	ERR_FAIL_INDEX_V(p_index, networking_servers.size(), "");
	return networking_servers[p_index].name;
}

NetworkingServerManager::NetworkingServerManager() {
	singleton = this;
}

NetworkingServerManager::~NetworkingServerManager() {
	singleton = nullptr;
}

void NetworkingServerManager::on_servers_changed() {
	String networking_servers_list = "DEFAULT";
	for (int i = get_servers_count() - 1; 0 <= i; --i) {
		networking_servers_list += "," + get_server_name(i);
	}

	ProjectSettings::get_singleton()->set_custom_property_info(
		PropertyInfo(Variant::STRING, setting_property_name, PROPERTY_HINT_ENUM, networking_servers_list));
	ProjectSettings::get_singleton()->set_restart_if_changed(setting_property_name, true);
	ProjectSettings::get_singleton()->set_as_basic(setting_property_name, true);
}