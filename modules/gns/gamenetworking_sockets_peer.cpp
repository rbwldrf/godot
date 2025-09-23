#include "gamenetworking_sockets_peer.h"

#include "core/io/ip.h"
#include "core/os/os.h"
#include "core/string/print_string.h"
#include "core/debugger/engine_debugger.h"
#include "core/debugger/remote_debugger.h"
#include <steam/steamnetworkingsockets_flat.h>

GameNetworkingSocketsPeer::GameNetworkingSocketsPeer() {
	// Initialize GameNetworkingSockets library
	SteamDatagramErrMsg errMsg;
	if (!GameNetworkingSockets_Init(nullptr, errMsg)) {
		ERR_FAIL_MSG("Failed to initialize GameNetworkingSockets: " + String(errMsg));
	}
	
	networking_sockets = SteamNetworkingSockets();
	ERR_FAIL_NULL_MSG(networking_sockets, "Failed to get SteamNetworkingSockets interface.");
	
	// Set up global connection status change callback only once
	if (connection_to_instance.is_empty()) {
		SteamNetworkingUtils()->SetGlobalCallback_SteamNetConnectionStatusChanged(_connection_status_changed_callback);
		print_line("Initialized GameNetworkingSockets global callback system");
	}
	
	// For servers, register in a way that we can find them for incoming connections
	// We'll add this instance to a list that can be checked for server instances
}

GameNetworkingSocketsPeer::~GameNetworkingSocketsPeer() {
	close();
	// Shutdown GameNetworkingSockets (should only be called once globally)
	// GameNetworkingSockets_Kill();
}

void GameNetworkingSocketsPeer::_bind_methods() {
	ClassDB::bind_method(D_METHOD("create_server", "port", "max_clients"), &GameNetworkingSocketsPeer::create_server, DEFVAL(32));
	ClassDB::bind_method(D_METHOD("create_client", "address", "port"), &GameNetworkingSocketsPeer::create_client);
	ClassDB::bind_method(D_METHOD("is_server"), &GameNetworkingSocketsPeer::is_server);
	
	ClassDB::bind_method(D_METHOD("configure_connection_lanes", "num_lanes"), &GameNetworkingSocketsPeer::configure_connection_lanes);
	ClassDB::bind_method(D_METHOD("set_bandwidth_limit", "bytes_per_second"), &GameNetworkingSocketsPeer::set_bandwidth_limit);
	ClassDB::bind_method(D_METHOD("get_round_trip_time"), &GameNetworkingSocketsPeer::get_round_trip_time);
	ClassDB::bind_method(D_METHOD("set_encryption_key", "key"), &GameNetworkingSocketsPeer::set_encryption_key);
	ClassDB::bind_method(D_METHOD("get_connection_stats"), &GameNetworkingSocketsPeer::get_connection_stats);
	ClassDB::bind_method(D_METHOD("get_connection_quality"), &GameNetworkingSocketsPeer::get_connection_quality);
	ClassDB::bind_method(D_METHOD("notify_connected_peers"), &GameNetworkingSocketsPeer::notify_connected_peers);
}

Error GameNetworkingSocketsPeer::create_server(int p_port, int p_max_clients) {
	ERR_FAIL_COND_V_MSG(connection_status != CONNECTION_DISCONNECTED, ERR_ALREADY_IN_USE, "Peer already connected.");
	ERR_FAIL_COND_V_MSG(p_port <= 0 || p_port > 65535, ERR_INVALID_PARAMETER, "Port must be between 1 and 65535.");
	ERR_FAIL_COND_V_MSG(p_max_clients <= 0, ERR_INVALID_PARAMETER, "Maximum clients must be greater than 0.");
	ERR_FAIL_NULL_V_MSG(networking_sockets, ERR_UNCONFIGURED, "GameNetworkingSockets interface not available.");

	// Create server address - bind to all interfaces
	SteamNetworkingIPAddr server_addr;
	server_addr.Clear();
	server_addr.SetIPv4(0, p_port); // Listen on all interfaces (0.0.0.0)

	// Create listen socket with better error handling
	print_line("Attempting to create listen socket on port " + itos(p_port));
	listen_socket = networking_sockets->CreateListenSocketIP(server_addr, 0, nullptr);
	if (listen_socket == k_HSteamListenSocket_Invalid) {
		ERR_FAIL_V_MSG(ERR_CANT_CREATE, "Failed to create listen socket on port " + itos(p_port) + ". Check if port is available and not blocked by firewall.");
	}
	print_line("Listen socket created successfully: " + itos(listen_socket));

	// Create poll group for managing connections
	poll_group = networking_sockets->CreatePollGroup();
	ERR_FAIL_COND_V_MSG(poll_group == k_HSteamNetPollGroup_Invalid, ERR_CANT_CREATE, "Failed to create poll group.");

	connection_status = CONNECTION_CONNECTED;
	unique_id = 1; // Server is always ID 1
	is_server_instance = true;
	
	// Register this server instance for handling incoming connections
	listen_socket_to_instance[listen_socket] = this;
	print_line("Registered server instance for incoming connections on listen socket ", listen_socket);
	
	print_line("GameNetworkingSockets server created at address ", SteamAPI_SteamNetworkingIPAddr_GetIPv4(&server_addr)," on port ",itos(p_port));
	return OK;
}

Error GameNetworkingSocketsPeer::create_client(const String &p_address, int p_port) {
	ERR_FAIL_COND_V_MSG(connection_status == CONNECTION_CONNECTED, ERR_ALREADY_IN_USE, "Peer already connected.");
	ERR_FAIL_COND_V_MSG(p_port <= 0 || p_port > 65535, ERR_INVALID_PARAMETER, "Port must be between 1 and 65535.");
	ERR_FAIL_NULL_V_MSG(networking_sockets, ERR_UNCONFIGURED, "GameNetworkingSockets interface not available.");

	// Parse server address
	SteamNetworkingIPAddr server_addr;
	server_addr.Clear();
	if (!SteamAPI_SteamNetworkingIPAddr_ParseString(&server_addr, p_address.utf8().get_data())) {
		ERR_FAIL_V_MSG(ERR_INVALID_PARAMETER, "Invalid server address: " + p_address);
	}
	server_addr.m_port = p_port;

	// Connect to server
	HSteamNetConnection connection = networking_sockets->ConnectByIPAddress(server_addr, 0, nullptr);
	ERR_FAIL_COND_V_MSG(connection == k_HSteamNetConnection_Invalid, ERR_CANT_CONNECT, "Failed to create connection to server.");

	// Create poll group for receiving messages
	poll_group = networking_sockets->CreatePollGroup();
	ERR_FAIL_COND_V_MSG(poll_group == k_HSteamNetPollGroup_Invalid, ERR_CANT_CREATE, "Failed to create poll group for client.");

	// Store the connection
	peer_connections[1] = connection; // Server is peer ID 1
	connection_to_peer[connection] = 1;
	connection_to_instance[connection] = this; // Register for callbacks

	// Add connection to poll group for message receiving
	networking_sockets->SetConnectionPollGroup(connection, poll_group);

	connection_status = CONNECTION_CONNECTING;
	unique_id = 0; // Will be assigned by server during handshake
	is_server_instance = false;
	
	print_line("GameNetworkingSockets client connecting to " + p_address + ":" + itos(p_port));
	return OK;
}

void GameNetworkingSocketsPeer::poll() {
	ERR_FAIL_NULL(networking_sockets);

	// Run networking callbacks to process events
	networking_sockets->RunCallbacks();

	// Process connection state changes
	_process_connection_events();

	// Process incoming messages
	_process_incoming_messages();
}

void GameNetworkingSocketsPeer::close() {
	if (networking_sockets) {
		// Close all connections
		for (const KeyValue<int, HSteamNetConnection> &pair : peer_connections) {
			networking_sockets->CloseConnection(pair.value, 0, nullptr, false);
		}
		
		// Close listen socket
		if (listen_socket != k_HSteamListenSocket_Invalid) {
			listen_socket_to_instance.erase(listen_socket);
			networking_sockets->CloseListenSocket(listen_socket);
			listen_socket = k_HSteamListenSocket_Invalid;
		}
		
		// Destroy poll group
		if (poll_group != k_HSteamNetPollGroup_Invalid) {
			networking_sockets->DestroyPollGroup(poll_group);
			poll_group = k_HSteamNetPollGroup_Invalid;
		}
	}

	peer_connections.clear();
	connection_to_peer.clear();
	incoming_packets.clear();
	
	connection_status = CONNECTION_DISCONNECTED;
	unique_id = 0;
	target_peer = 0;
	next_peer_id = 2;
	is_server_instance = false;
	last_received_peer = 0;
}

void GameNetworkingSocketsPeer::disconnect_peer(int p_peer, bool p_force) {
	// TODO: Implement peer disconnection
}

MultiplayerPeer::ConnectionStatus GameNetworkingSocketsPeer::get_connection_status() const {
	return connection_status;
}

int GameNetworkingSocketsPeer::get_unique_id() const {
	return unique_id;
}

void GameNetworkingSocketsPeer::set_refuse_new_connections(bool p_enable) {
	refuse_new_connections = p_enable;
}

bool GameNetworkingSocketsPeer::is_refusing_new_connections() const {
	return refuse_new_connections;
}

bool GameNetworkingSocketsPeer::is_server() const {
	return is_server_instance;
}

void GameNetworkingSocketsPeer::set_transfer_channel(int p_channel) {
	// TODO: Implement channel setting
}

int GameNetworkingSocketsPeer::get_transfer_channel() const {
	return 0; // TODO: Return actual channel
}

void GameNetworkingSocketsPeer::set_transfer_mode(TransferMode p_mode) {
	transfer_mode = p_mode;
}

MultiplayerPeer::TransferMode GameNetworkingSocketsPeer::get_transfer_mode() const {
	return transfer_mode;
}

void GameNetworkingSocketsPeer::set_target_peer(int p_peer) {
	target_peer = p_peer;
}

int GameNetworkingSocketsPeer::get_packet_peer() const {
	return last_received_peer;
}

int GameNetworkingSocketsPeer::get_packet_channel() const {
	return 0; // TODO: Return actual packet channel
}

MultiplayerPeer::TransferMode GameNetworkingSocketsPeer::get_packet_mode() const {
	return TRANSFER_MODE_RELIABLE; // TODO: Return actual packet mode
}

int GameNetworkingSocketsPeer::get_available_packet_count() const {
	return incoming_packets.size();
}

Error GameNetworkingSocketsPeer::get_packet(const uint8_t **r_buffer, int &r_buffer_size) {
	ERR_FAIL_COND_V_MSG(incoming_packets.is_empty(), ERR_UNAVAILABLE, "No packets available.");

	List<PendingPacket>::Element *front = incoming_packets.front();
	ERR_FAIL_NULL_V(front, ERR_UNAVAILABLE);

	*r_buffer = front->get().data.ptr();
	r_buffer_size = front->get().data.size();

	// Store packet info for get_packet_peer() calls
	int from_peer = front->get().from_peer;
	transfer_channel = front->get().channel;
	transfer_mode = front->get().mode;
	
	// Store the peer ID for get_packet_peer() to return
	last_received_peer = from_peer;
	
	print_line("DEBUG: get_packet() storing last_received_peer = " + itos(from_peer));
	print_line("DEBUG: This " + String(is_server() ? "SERVER" : "CLIENT") + " will report packet from peer " + itos(from_peer));

	incoming_packets.pop_front();
	return OK;
}

Error GameNetworkingSocketsPeer::put_packet(const uint8_t *p_buffer, int p_buffer_size) {
	print_line("DEBUG: put_packet called with size " + itos(p_buffer_size) + " (is_server: " + (is_server() ? "yes" : "no") + ")");
	ERR_FAIL_NULL_V_MSG(networking_sockets, ERR_UNCONFIGURED, "GameNetworkingSockets not initialized.");
	
	// For clients, check if connected to server
	// For servers, check if we have at least one peer connection
	if (is_server()) {
		ERR_FAIL_COND_V_MSG(peer_connections.is_empty(), ERR_UNCONFIGURED, "Server has no connected peers.");
	} else {
		ERR_FAIL_COND_V_MSG(connection_status != CONNECTION_CONNECTED, ERR_UNCONFIGURED, "Not connected to server.");
	}

	// Determine send flags based on transfer mode
	int send_flags = k_nSteamNetworkingSend_Reliable;
	if (transfer_mode == TRANSFER_MODE_UNRELIABLE) {
		send_flags = k_nSteamNetworkingSend_Unreliable;
	} else if (transfer_mode == TRANSFER_MODE_UNRELIABLE_ORDERED) {
		send_flags = k_nSteamNetworkingSend_UnreliableNoDelay;
	}

	if (target_peer == 0) {
		// Send to all peers
		print_line("Broadcasting packet to " + itos(peer_connections.size()) + " peers");
		for (const KeyValue<int, HSteamNetConnection> &pair : peer_connections) {
			EResult result = networking_sockets->SendMessageToConnection(pair.value, p_buffer, p_buffer_size, send_flags, nullptr);
			if (result != k_EResultOK) {
				ERR_PRINT("Failed to send message to peer " + itos(pair.key) + ": " + itos(result));
			} else {
				print_line("Sent packet to peer " + itos(pair.key));
#ifdef DEBUG_ENABLED
				_profile_bandwidth("out", p_buffer_size);
#endif
			}
		}
	} else {
		// Send to specific peer
		auto it = peer_connections.find(target_peer);
		ERR_FAIL_COND_V_MSG(it == peer_connections.end(), ERR_INVALID_PARAMETER, "Target peer not found: " + itos(target_peer));
		
		print_line("Sending packet to peer " + itos(target_peer));
		EResult result = networking_sockets->SendMessageToConnection(it->value, p_buffer, p_buffer_size, send_flags, nullptr);
		if (result != k_EResultOK) {
			ERR_PRINT("Failed to send message to peer " + itos(target_peer) + ": " + itos(result));
			return ERR_CANT_CONNECT;
		} else {
			print_line("Successfully sent packet to peer " + itos(target_peer));
#ifdef DEBUG_ENABLED
			_profile_bandwidth("out", p_buffer_size);
#endif
		}
	}

	return OK;
}

int GameNetworkingSocketsPeer::get_max_packet_size() const {
	return 512 * 1024; // 512KB max packet size for GameNetworkingSockets
}

// GameNetworkingSockets-specific methods
Error GameNetworkingSocketsPeer::configure_connection_lanes(int p_num_lanes) {
	ERR_FAIL_COND_V_MSG(p_num_lanes <= 0, ERR_INVALID_PARAMETER, "Number of lanes must be greater than 0.");
	// TODO: Implement connection lanes configuration
	return ERR_UNAVAILABLE;
}

void GameNetworkingSocketsPeer::set_bandwidth_limit(int p_bytes_per_second) {
	ERR_FAIL_COND_MSG(p_bytes_per_second < 0, "Bandwidth limit cannot be negative.");
	// TODO: Implement bandwidth limiting
}

float GameNetworkingSocketsPeer::get_round_trip_time() const {
	if (!is_server() && !peer_connections.is_empty()) {
		HSteamNetConnection server_conn = peer_connections.begin()->value;
		SteamNetConnectionRealTimeStatus_t status;
		if (networking_sockets->GetConnectionRealTimeStatus(server_conn, &status, 0, nullptr) == k_EResultOK) {
			return status.m_nPing * 1000.0f;
		}
	}
	return 0.0f;
}

void GameNetworkingSocketsPeer::set_encryption_key(const PackedByteArray &p_key) {
	// TODO: Implement encryption key setting
}

// Helper method implementations
void GameNetworkingSocketsPeer::_process_connection_events() {
	ERR_FAIL_NULL(networking_sockets);

	// For servers: check for new incoming connections
	if (is_server() && listen_socket != k_HSteamListenSocket_Invalid) {
		_check_for_new_connections();
	}

	// Check connection status for all existing connections
	for (const KeyValue<int, HSteamNetConnection> &pair : peer_connections) {
		SteamNetConnectionInfo_t info;
		if (networking_sockets->GetConnectionInfo(pair.value, &info)) {
			// Debug: Print current connection state
			//print_line("Peer " + itos(pair.key) + " state: " + itos(info.m_eState) + " (" + String(info.m_szEndDebug) + ")");
			
			if (info.m_eState == k_ESteamNetworkingConnectionState_Connected) {
				if (connection_status == CONNECTION_CONNECTING) {
					connection_status = CONNECTION_CONNECTED;
					print_line("Connection established to peer " + itos(pair.key));
				}
			} else if (info.m_eState == k_ESteamNetworkingConnectionState_ClosedByPeer ||
					   info.m_eState == k_ESteamNetworkingConnectionState_ProblemDetectedLocally) {
				print_line("Connection lost to peer " + itos(pair.key));
				_remove_peer_connection(pair.value);
			}
		}
	}
}

void GameNetworkingSocketsPeer::_process_incoming_messages() {
	ERR_FAIL_NULL(networking_sockets);

	if (poll_group == k_HSteamNetPollGroup_Invalid) {
		return;
	}

	SteamNetworkingMessage_t *messages[32];
	int message_count = networking_sockets->ReceiveMessagesOnPollGroup(poll_group, messages, 32);
	
	if (message_count > 0) {
		print_line("DEBUG: _process_incoming_messages found " + itos(message_count) + " messages");
	}

	for (int i = 0; i < message_count; i++) {
		SteamNetworkingMessage_t *msg = messages[i];
		
		int peer_id = _get_peer_id_for_connection(msg->m_conn);
		print_line("DEBUG: _process_incoming_messages - connection " + itos(msg->m_conn) + " mapped to peer_id " + itos(peer_id));
		
		// CRITICAL FIX: Handle unmapped connections - clients should map server to peer_id 1
		if (peer_id == -1 && !is_server()) {
			peer_id = 1; // For clients, unknown connections are from server
		}
		
		if (peer_id != -1) {
			// Check if this is a handshake packet
			if (msg->m_cbSize >= 4 && 
				((uint8_t*)msg->m_pData)[0] == 0xFF && 
				((uint8_t*)msg->m_pData)[1] == 0xFE) {
				print_line("DEBUG: Detected handshake packet from peer " + itos(peer_id) + " size " + itos(msg->m_cbSize));
				_handle_handshake_packet((uint8_t*)msg->m_pData, msg->m_cbSize, msg->m_conn);
			} else {
				// Regular game packet
				print_line("DEBUG: Processing regular packet from peer " + itos(peer_id) + " size " + itos(msg->m_cbSize));
				print_line("DEBUG: This is " + String(is_server() ? "SERVER" : "CLIENT") + " processing packet");
				print_line("DEBUG: Connection " + itos(msg->m_conn) + " -> peer_id " + itos(peer_id));
				PendingPacket packet;
				packet.data.resize(msg->m_cbSize);
				memcpy(packet.data.ptrw(), msg->m_pData, msg->m_cbSize);
				packet.from_peer = peer_id;
				print_line("DEBUG: Set packet.from_peer = " + itos(peer_id));
				packet.channel = 0; // TODO: Handle channels
				packet.mode = TRANSFER_MODE_RELIABLE; // TODO: Detect transfer mode
				
				incoming_packets.push_back(packet);
#ifdef DEBUG_ENABLED
				_profile_bandwidth("in", msg->m_cbSize);
#endif
			}
		}
		
		msg->Release();
	}
}

int GameNetworkingSocketsPeer::_get_peer_id_for_connection(HSteamNetConnection connection) {
	auto it = connection_to_peer.find(connection);
	if (it != connection_to_peer.end()) {
		return it->value;
	}
	return -1;
}

void GameNetworkingSocketsPeer::_add_peer_connection(HSteamNetConnection connection) {
	int peer_id = next_peer_id++;
	peer_connections[peer_id] = connection;
	connection_to_peer[connection] = peer_id;
	
	if (poll_group != k_HSteamNetPollGroup_Invalid) {
		networking_sockets->SetConnectionPollGroup(connection, poll_group);
	}
	
	print_line("Added peer connection: " + itos(peer_id));
	
	// Emit peer_connected signal for SceneMultiplayer
	print_line("DEBUG: Emitting peer_connected signal for peer " + itos(peer_id));
	emit_signal("peer_connected", peer_id);
}

void GameNetworkingSocketsPeer::_remove_peer_connection(HSteamNetConnection connection) {
	int peer_id = _get_peer_id_for_connection(connection);
	if (peer_id != -1) {
		peer_connections.erase(peer_id);
		connection_to_peer.erase(connection);
		print_line("Removed peer connection: " + itos(peer_id));
		
		// Emit peer_disconnected signal for SceneMultiplayer
		emit_signal("peer_disconnected", peer_id);
	}
}

void GameNetworkingSocketsPeer::_check_for_new_connections() {
	ERR_FAIL_COND_MSG(listen_socket == k_HSteamListenSocket_Invalid, "Not a server or listen socket invalid.");
	
	// Process any pending connection status changes for this specific instance
	// This is needed because the global callback might miss server-specific events
	ISteamNetworkingUtils *utils = SteamNetworkingUtils();
	if (utils) {
		// Process callbacks specifically for this networking interface
		networking_sockets->RunCallbacks();
	}
}

// Static member definitions
HashMap<HSteamNetConnection, GameNetworkingSocketsPeer*> GameNetworkingSocketsPeer::connection_to_instance;
HashMap<HSteamListenSocket, GameNetworkingSocketsPeer*> GameNetworkingSocketsPeer::listen_socket_to_instance;

// Static callback function
void GameNetworkingSocketsPeer::_connection_status_changed_callback(SteamNetConnectionStatusChangedCallback_t *pInfo) {
	print_line("CALLBACK: Connection " + itos(pInfo->m_hConn) + " state changed: " + itos(pInfo->m_eOldState) + " -> " + itos(pInfo->m_info.m_eState));
	
	// Find the peer instance that owns this connection
	auto it = connection_to_instance.find(pInfo->m_hConn);
	if (it != connection_to_instance.end()) {
		print_line("CALLBACK: Found existing connection mapping, forwarding to instance");
		it->value->_handle_connection_status_changed(pInfo);
	} else if (pInfo->m_eOldState == k_ESteamNetworkingConnectionState_None && 
		       pInfo->m_info.m_eState == k_ESteamNetworkingConnectionState_Connecting) {
		// This is a new incoming connection - find any active server to handle it
		print_line("DEBUG: New incoming connection detected: " + itos(pInfo->m_hConn));
		print_line("DEBUG: Available servers in map: " + itos(listen_socket_to_instance.size()));
		
		GameNetworkingSocketsPeer *server_peer = nullptr;
		
		// Find the first active server instance
		for (auto &pair : listen_socket_to_instance) {
			GameNetworkingSocketsPeer *peer = pair.value;
			print_line("DEBUG: Checking server with listen_socket: " + itos(pair.key) + " is_server: " + (peer->is_server() ? "yes" : "no"));
			if (peer->is_server() && peer->listen_socket != k_HSteamListenSocket_Invalid) {
				server_peer = peer;
				print_line("DEBUG: Selected this server to handle connection");
				break;
			}
		}
		
		if (server_peer) {
			print_line("Found server to handle incoming connection: ", pInfo->m_hConn);
			connection_to_instance[pInfo->m_hConn] = server_peer;
			server_peer->_handle_connection_status_changed(pInfo);
		} else {
			print_line("ERROR: Incoming connection ", pInfo->m_hConn, " but no server found to handle it");
			print_line("DEBUG: Total servers registered: " + itos(listen_socket_to_instance.size()));
		}
	}
}

void GameNetworkingSocketsPeer::_handle_connection_status_changed(SteamNetConnectionStatusChangedCallback_t *pInfo) {
	HSteamNetConnection conn = pInfo->m_hConn;
	ESteamNetworkingConnectionState old_state = pInfo->m_eOldState;
	ESteamNetworkingConnectionState new_state = pInfo->m_info.m_eState;
	
	print_line("Connection " + itos(conn) + " state changed: " + itos(old_state) + " -> " + itos(new_state) + " (is_server: " + (is_server() ? "yes" : "no") + ")");
	
	switch (new_state) {
		case k_ESteamNetworkingConnectionState_Connecting:
			if (is_server() && old_state == k_ESteamNetworkingConnectionState_None) {
				// New incoming connection - accept it
				print_line("Server accepting incoming connection: " + itos(conn));
				EResult result = networking_sockets->AcceptConnection(conn);
				if (result == k_EResultOK) {
					connection_to_instance[conn] = this;
					_add_peer_connection(conn);
					print_line("Successfully accepted incoming connection: " + itos(conn));
				} else {
					print_line("Failed to accept connection " + itos(conn) + ": " + itos(result));
				}
			}
			break;
			
		case k_ESteamNetworkingConnectionState_Connected:
			print_line("Connection " + itos(conn) + " reached connected state (is_server: " + (is_server() ? "yes" : "no") + ")");
			if (connection_status == CONNECTION_CONNECTING) {
				connection_status = CONNECTION_CONNECTED;
				print_line("Peer connection status updated to CONNECTED");
				
				// For clients, assign the unique ID now that we're connected
				if (!is_server() && unique_id == 0) {
					unique_id = 2; // Temporary assignment until handshake
					print_line("Client assigned temporary unique_id: " + itos(unique_id));
					
					// CRITICAL FIX: For clients, ensure self-connection is mapped properly
					// This prevents get_packet_peer() from returning 0 when processing own RPC packets
					if (peer_connections.find(unique_id) == peer_connections.end()) {
						peer_connections[unique_id] = conn;
						connection_to_peer[conn] = unique_id;
						print_line("DEBUG: Client mapped own connection to peer_id " + itos(unique_id));
					}
					
					// CRITICAL FIX: Client should emit peer_connected for server (peer ID 1) 
					// when connection is established, not waiting for handshake
					print_line("DEBUG: Client connection established - emitting peer_connected(1) for server");
					emit_signal("peer_connected", 1);
				}
				
				// For servers, send handshake to newly connected client
				if (is_server()) {
					print_line("DEBUG: Server sending handshake to new client on connection " + itos(conn));
					_send_handshake_to_client(conn);
				}
			}
			break;
			
		case k_ESteamNetworkingConnectionState_ClosedByPeer:
		case k_ESteamNetworkingConnectionState_ProblemDetectedLocally:
			print_line("Connection closed: " + itos(conn) + " - " + String(pInfo->m_info.m_szEndDebug));
			_remove_peer_connection(conn);
			connection_to_instance.erase(conn);
			if (peer_connections.is_empty() && !is_server()) {
				connection_status = CONNECTION_DISCONNECTED;
				// Emit peer_disconnected signal for server (peer ID 1)
				emit_signal("peer_disconnected", 1);
			}
			break;
			
		default:
			break;
	}
}

// Network profiler integration
void GameNetworkingSocketsPeer::_profile_bandwidth(const String &p_direction, int p_bytes) {
	if (EngineDebugger::is_active()) {
		Array values;
		values.push_back(p_direction);
		values.push_back(p_bytes);
		values.push_back(OS::get_singleton()->get_ticks_msec());
		EngineDebugger::get_singleton()->send_message("network:bandwidth", values);
	}
}

Dictionary GameNetworkingSocketsPeer::get_connection_stats() {
	Dictionary stats;
	stats["peer_count"] = peer_connections.size();
	stats["connection_status"] = connection_status;
	stats["unique_id"] = unique_id;
	stats["is_server"] = is_server();
	return stats;
}

void GameNetworkingSocketsPeer::notify_connected_peers() {
	print_line("DEBUG: notify_connected_peers() called");
	
	// For clients, emit peer_connected for server (peer ID 1) if we're connected
	if (!is_server() && connection_status == CONNECTION_CONNECTED) {
		print_line("DEBUG: Client emitting peer_connected for server");
		emit_signal("peer_connected", 1);
	}
	
	// For servers, emit peer_connected for all connected clients
	if (is_server()) {
		for (const KeyValue<int, HSteamNetConnection> &pair : peer_connections) {
			print_line("DEBUG: Server emitting peer_connected for peer " + itos(pair.key));
			emit_signal("peer_connected", pair.key);
		}
	}
}

float GameNetworkingSocketsPeer::get_connection_quality() const {
	if (!is_server() && !peer_connections.is_empty()) {
		HSteamNetConnection server_conn = peer_connections.begin()->value;
		SteamNetConnectionRealTimeStatus_t status;
		if (networking_sockets->GetConnectionRealTimeStatus(server_conn, &status, 0, nullptr) == k_EResultOK) {
			return status.m_flConnectionQualityLocal;
		}
	}
	return 1.0f;
}

// Handshake implementation
void GameNetworkingSocketsPeer::_send_handshake_to_client(HSteamNetConnection connection) {
	// Create a simple handshake packet with the assigned peer ID
	int client_peer_id = _get_peer_id_for_connection(connection);
	if (client_peer_id != -1) {
		uint8_t handshake_data[8];
		handshake_data[0] = 0xFF; // Handshake marker
		handshake_data[1] = 0xFE; // Handshake marker  
		handshake_data[2] = 0x01; // Handshake type: ID assignment
		handshake_data[3] = 0x00; // Reserved
		// Store peer ID as 4 bytes
		handshake_data[4] = (client_peer_id >> 24) & 0xFF;
		handshake_data[5] = (client_peer_id >> 16) & 0xFF;
		handshake_data[6] = (client_peer_id >> 8) & 0xFF;
		handshake_data[7] = client_peer_id & 0xFF;
		
		EResult result = networking_sockets->SendMessageToConnection(connection, handshake_data, 8, k_nSteamNetworkingSend_Reliable, nullptr);
		if (result == k_EResultOK) {
			print_line("Sent handshake to client, assigned peer ID: " + itos(client_peer_id));
		} else {
			print_line("Failed to send handshake to client: " + itos(result));
		}
	}
}

void GameNetworkingSocketsPeer::_handle_handshake_packet(const uint8_t *data, int size, HSteamNetConnection from_connection) {
	if (size >= 8 && data[0] == 0xFF && data[1] == 0xFE) {
		if (data[2] == 0x01) { // ID assignment handshake
			// Extract assigned peer ID
			int assigned_id = (data[4] << 24) | (data[5] << 16) | (data[6] << 8) | data[7];
			unique_id = assigned_id;
			print_line("Client received handshake: assigned peer ID " + itos(assigned_id));
			
			// CRITICAL FIX: Client should now emit peer_connected for server (peer ID 1)
			// This ensures SceneMultiplayer knows about the server connection
			print_line("DEBUG: Client emitting peer_connected signal for server (peer ID 1)");
			emit_signal("peer_connected", 1);
			
			// Send acknowledgment back to server
			uint8_t ack_data[4];
			ack_data[0] = 0xFF; // Handshake marker
			ack_data[1] = 0xFE; // Handshake marker
			ack_data[2] = 0x02; // Handshake type: acknowledgment
			ack_data[3] = 0x00; // Reserved
			
			EResult result = networking_sockets->SendMessageToConnection(from_connection, ack_data, 4, k_nSteamNetworkingSend_Reliable, nullptr);
			if (result == k_EResultOK) {
				print_line("Sent handshake acknowledgment to server");
			} else {
				print_line("Failed to send handshake acknowledgment: " + itos(result));
			}
		} else if (data[2] == 0x02) { // Acknowledgment from client
			print_line("Server received handshake acknowledgment from client");
		}
	}
}