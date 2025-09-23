extends SceneTree

class TestNode:
	extends Node
	
	@rpc("any_peer", "call_local", "reliable")
	func test_rpc(message: String):
		var sender_id = get_multiplayer().get_remote_sender_id()
		var receiver_id = get_multiplayer().multiplayer_peer.get_unique_id()
		var net_peer = get_multiplayer().multiplayer_peer as GameNetworkingSocketsPeer
		var actual_peer_id = net_peer.get_unique_id() if net_peer else "unknown"
		print("📡 RPC received at ID ", receiver_id, " (actual: ", actual_peer_id, "): ", message, " (from ", sender_id, ")")
		
		# Also print some debug info about the connection
		if net_peer:
			var stats = net_peer.get_connection_stats()
			print("Connection stats: ", stats)

var peer: GameNetworkingSocketsPeer
var is_server: bool = false

func _init():
	var args = OS.get_cmdline_user_args()
	is_server = args.has("server")
	print("User args: ", args)
	print("Detected mode: ", "server" if is_server else "client")
	
	# Create the peer
	peer = GameNetworkingSocketsPeer.new()
	
	# Create a simple test node with a shorter name
	var test_node = TestNode.new()
	test_node.name = "Test"  # Short name to avoid truncation
	root.add_child(test_node)
	
	if is_server:
		print("Starting server...")
		var error = peer.create_server(12345, 10)
		if error != OK:
			print("Failed to create server: ", error)
			quit()
			return
		print("Server created successfully")
	else:
		print("Starting client...")
		var error = peer.create_client("127.0.0.1", 12345)
		if error != OK:
			print("Failed to create client: ", error)
			quit()
			return
		print("Client connection initiated")
	
	# Set up multiplayer
	var multiplayer_api = SceneMultiplayer.new()
	set_multiplayer(multiplayer_api, "/root")
	
	# Set up multiplayer - SceneMultiplayer will automatically connect to peer signals
	get_multiplayer().multiplayer_peer = peer
	
	# Connect to SceneMultiplayer's signals instead for debugging
	get_multiplayer().peer_connected.connect(_on_peer_connected)
	get_multiplayer().peer_disconnected.connect(_on_peer_disconnected)
	
	# Set up a timer to notify about existing connections after connection is established
	var notify_timer = Timer.new()
	notify_timer.wait_time = 0.5
	notify_timer.autostart = true
	notify_timer.one_shot = true
	notify_timer.timeout.connect(_notify_connections)
	root.add_child(notify_timer)
	
	# Print connection status
	print("Connection status: ", peer.get_connection_status())
	print("Unique ID: ", peer.get_unique_id())
	
	# Set up a timer to test RPCs periodically
	var timer = Timer.new()
	timer.wait_time = 2.0
	timer.autostart = true
	timer.timeout.connect(_test_rpc)
	root.add_child(timer)
	
	# Set up a quit timer
	var quit_timer = Timer.new()
	quit_timer.wait_time = 30.0
	quit_timer.autostart = true
	quit_timer.timeout.connect(quit)
	root.add_child(quit_timer)

func _on_peer_connected(id: int):
	print("🔗 SIGNAL: peer_connected(", id, ") - SceneMultiplayer should add this peer now")
	print("🔗 SceneMultiplayer connected peers: ", get_multiplayer().get_peers())

func _on_peer_disconnected(id: int):
	print("🔗 SIGNAL: peer_disconnected(", id, ") - SceneMultiplayer should remove this peer now")
	print("🔗 SceneMultiplayer connected peers: ", get_multiplayer().get_peers())

func _notify_connections():
	print("=== Notifying connections ===")
	print("Connection status: ", peer.get_connection_status())
	peer.notify_connected_peers()

func _test_rpc():
	print("\n=== RPC Test ===")
	print("Connection status: ", peer.get_connection_status())
	var my_id = peer.get_unique_id()
	print("My ID: ", my_id)
	var scene_multiplayer_peers = get_multiplayer().get_peers()
	var our_peer_list = []
	if is_server:
		# Server: should list all connected clients (we'll see this from debug output)
		# For now, just note this is server
		our_peer_list = []  # We can't access peer_connections from GDScript
	else:
		# Client: should list the server (peer 1)
		if peer.get_connection_status() == 2:  # CONNECTION_CONNECTED
			our_peer_list.append(1)
	
	print("SceneMultiplayer peers: ", scene_multiplayer_peers)
	print("Our peer tracking: ", our_peer_list)
	
	# Convert to strings for comparison since one is PackedInt32Array
	var scene_peers_str = str(scene_multiplayer_peers)
	var our_peers_str = str(our_peer_list)
	if scene_peers_str != our_peers_str:
		print("⚠️  MISMATCH: SceneMultiplayer and our peer tracking don't agree!")
	
	# Use SceneMultiplayer's list for RPC calls since that's what matters
	var connected_peers = scene_multiplayer_peers
	
	var test_node = root.get_node("Test")
	if test_node and test_node.has_method("test_rpc"):
		print("📡 Sending RPC from ID ", my_id, " to connected peers: ", connected_peers)
		# Use rpc_id to explicitly target all connected peers
		for peer_id in connected_peers:
			print("📡 Calling RPC on peer ", peer_id)
			test_node.test_rpc.rpc_id(peer_id, "Hello from " + str(my_id))
		# Also call locally if call_local is enabled
		if connected_peers.size() > 0:  # Only if we have peers (to mimic call_local behavior)
			print("📡 Calling RPC locally on ID ", my_id)
			test_node.test_rpc.call("Hello from " + str(my_id) + " (local)")
	else:
		print("❌ Test node or method not found")