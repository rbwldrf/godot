#ifndef GAMENETWORKING_SOCKETS_PEER_H
#define GAMENETWORKING_SOCKETS_PEER_H

#include "scene/main/multiplayer_peer.h"
#include "core/templates/hash_map.h"
#include "core/templates/list.h"

#include <steam/steamnetworkingsockets.h>

class GameNetworkingSocketsPeer : public MultiplayerPeerExtension {
	GDCLASS(GameNetworkingSocketsPeer, MultiplayerPeerExtension);

	struct PendingPacket {
		Vector<uint8_t> data;
		int from_peer;
		int channel;
		TransferMode mode;
	};

private:
	ConnectionStatus connection_status = CONNECTION_DISCONNECTED;
	int unique_id = 0;
	int target_peer = 0;
	bool refuse_new_connections = false;
	TransferMode transfer_mode = TRANSFER_MODE_RELIABLE;
	int transfer_channel = 0;

	// GameNetworkingSockets interface
	ISteamNetworkingSockets *networking_sockets = nullptr;
	HSteamListenSocket listen_socket = k_HSteamListenSocket_Invalid;
	HSteamNetPollGroup poll_group = k_HSteamNetPollGroup_Invalid;
	
	// Connection management
	HashMap<int, HSteamNetConnection> peer_connections;
	HashMap<HSteamNetConnection, int> connection_to_peer;
	List<PendingPacket> incoming_packets;
	int next_peer_id = 2;

	// Helper methods
	void _process_connection_events();
	void _process_incoming_messages();
	void _check_for_new_connections();
	int _get_peer_id_for_connection(HSteamNetConnection connection);
	void _add_peer_connection(HSteamNetConnection connection);
	void _remove_peer_connection(HSteamNetConnection connection);
	
	// Connection callback handling
	static HashMap<HSteamNetConnection, GameNetworkingSocketsPeer*> connection_to_instance;
	static HashMap<HSteamListenSocket, GameNetworkingSocketsPeer*> listen_socket_to_instance;
	static void _connection_status_changed_callback(SteamNetConnectionStatusChangedCallback_t *pInfo);
	void _handle_connection_status_changed(SteamNetConnectionStatusChangedCallback_t *pInfo);

protected:
	static void _bind_methods();

public:
	GameNetworkingSocketsPeer();
	~GameNetworkingSocketsPeer();

	// MultiplayerPeer interface implementation
	virtual void set_transfer_channel(int p_channel) override;
	virtual int get_transfer_channel() const override;
	virtual void set_transfer_mode(TransferMode p_mode) override;
	virtual TransferMode get_transfer_mode() const override;
	virtual void set_target_peer(int p_peer) override;
	virtual int get_packet_peer() const override;
	virtual int get_packet_channel() const override;
	virtual TransferMode get_packet_mode() const override;

	virtual bool is_server() const override;
	virtual void poll() override;
	virtual void close() override;
	virtual void disconnect_peer(int p_peer, bool p_force = false) override;

	virtual ConnectionStatus get_connection_status() const override;
	virtual int get_unique_id() const override;
	virtual void set_refuse_new_connections(bool p_enable) override;
	virtual bool is_refusing_new_connections() const override;

	virtual int get_available_packet_count() const override;
	virtual Error get_packet(const uint8_t **r_buffer, int &r_buffer_size) override;
	virtual Error put_packet(const uint8_t *p_buffer, int p_buffer_size) override;
	virtual int get_max_packet_size() const override;

	// Server/Client creation
	Error create_server(int p_port, int p_max_clients = 32);
	Error create_client(const String &p_address, int p_port);

	// GameNetworkingSockets-specific features
	Error configure_connection_lanes(int p_num_lanes);
	void set_bandwidth_limit(int p_bytes_per_second);
	float get_round_trip_time() const;
	void set_encryption_key(const PackedByteArray &p_key);
};

#endif // GAMENETWORKING_SOCKETS_PEER_H