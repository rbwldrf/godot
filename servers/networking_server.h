#ifndef NETWORKING_SERVER_H
#define NETWORKING_SERVER_H

#include "core/object/object.h"
#include "core/templates/vector.h"
#include "core/variant/callable.h"
#include "scene/main/multiplayer_peer.h"

class NetworkingServer : public Object {
	GDCLASS(NetworkingServer, Object);

	static NetworkingServer *singleton;

protected:
	static void _bind_methods();

public:
	static NetworkingServer *get_singleton();

	virtual void init() = 0;
	virtual void finish() = 0;

	virtual Ref<MultiplayerPeer> create_enet_peer() = 0;
	virtual Ref<MultiplayerPeer> create_gamenetworking_sockets_peer() = 0;

	NetworkingServer();
	~NetworkingServer();
};

class NetworkingServerManager : public Object {
	GDCLASS(NetworkingServerManager, Object);

	struct ClassInfo {
		String name;
		Callable create_callback;
	};

	Vector<ClassInfo> networking_servers;
	int default_server_id = -1;
	int default_server_priority = -1;

	static NetworkingServerManager *singleton;

protected:
	static void _bind_methods();

public:
	static const String setting_property_name;

	static NetworkingServerManager *get_singleton();

	void register_server(const String &p_name, const Callable &p_create_callback);
	void set_default_server(const String &p_name, int p_priority = 0);
	NetworkingServer *new_server(const String &p_name);
	NetworkingServer *new_default_server();

	int get_servers_count();
	String get_server_name(int p_index);

	NetworkingServerManager();
	~NetworkingServerManager();

private:
	void on_servers_changed();
};

#endif // NETWORKING_SERVER_H