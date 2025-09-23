extends Node

var peer: GameNetworkingSocketsPeer
var multiplayer_connected := false
var scene_multiplayer_ready := false

func _ready():
	print("=== FINAL RPC Test ===")
	
	# CRITICAL FIX: Ensure SceneMultiplayer is used as the default interface
	# This prevents the MultiplayerAPIExtension/SceneMultiplayer instance mismatch
	print("Current default interface: ", MultiplayerAPI.get_default_interface())
	MultiplayerAPI.set_default_interface("SceneMultiplayer")
	print("✅ Set default interface to SceneMultiplayer")
	
	# Create a unified SceneMultiplayer instance that will handle everything
	var scene_multiplayer = MultiplayerAPI.create_default_interface()
	print("Created SceneMultiplayer instance: ", scene_multiplayer.get_instance_id())
	print("Type: ", scene_multiplayer.get_class())
	
	# Register the SceneMultiplayer instance with SceneTree first
	get_tree().set_multiplayer(scene_multiplayer)
	print("✅ Registered unified SceneMultiplayer with SceneTree")
	scene_multiplayer_ready = true
	
	# Create peer - we'll connect it later when it's in CONNECTING/CONNECTED state
	peer = GameNetworkingSocketsPeer.new()
	print("Created GameNetworkingSocketsPeer")
	
	# Verify everything is aligned
	var final_multiplayer = get_tree().get_multiplayer()
	print("Final multiplayer instance: ", final_multiplayer.get_instance_id())
	print("Instances aligned: ", scene_multiplayer == final_multiplayer)
	
	print("S=Server, C=Client, R=RPC")
	
	# Connect to the UNIFIED SceneMultiplayer's signals, not the peer's signals
	# This ensures signal routing goes to the same instance handling RPC validation
	multiplayer.peer_connected.connect(_on_peer_connected)
	multiplayer.peer_connected.connect(whee.rpc.bind(multiplayer.get_unique_id()))

func _on_peer_connected(id: int):
	print("🔗 Peer connected: ", id, " | All peers: ", multiplayer.get_peers())
	print("🔍 SceneMultiplayer instance handling peer_connected: ", multiplayer.get_instance_id())
	print("🔍 GameNetworkingSockets peer unique ID: ", peer.get_unique_id())

@rpc("any_peer", "call_local", "reliable") 
func test_rpc(message: String):
	print("📡 RPC: ", message, " | Peers: ", multiplayer.get_peers())

func _input(event):
	if event is InputEventKey and event.pressed:
		match event.keycode:
			KEY_S:
				print("🔧 Starting server...")
				peer.create_server(8080, 32)
				print("Server started on port 8080")
			KEY_C:
				print("🔧 Connecting to server...")
				peer.create_client("127.0.0.1", 8080)
				print("Client connection initiated")
			KEY_R:
				if peer.get_connection_status() == MultiplayerPeer.CONNECTION_CONNECTED:
					var msg = "Hello from " + str(multiplayer.get_unique_id())
					print("📡 Sending RPC: ", msg)
					test_rpc.rpc(msg)

@rpc("authority", "call_remote", "reliable") 
func whee(_a, id):
	print("📡 whee() called with id: ", id)
	if multiplayer.is_server():
		# Check if MultiplayerSpawner exists before using it
		var spawner = get_node_or_null("Node2D/MultiplayerSpawner")
		if spawner:
			spawner.spawn(id)
			print("✅ Spawned for ID: ", id)
		else:
			print("⚠️ No MultiplayerSpawner found")

func _process(delta):
	peer.poll()
	
	# CRITICAL FIX: Set multiplayer_peer only when peer is CONNECTING or CONNECTED
	# SceneMultiplayer validates the connection status
	var connection_status = peer.get_connection_status()
	
	if (connection_status == MultiplayerPeer.CONNECTION_CONNECTING or connection_status == MultiplayerPeer.CONNECTION_CONNECTED) and not multiplayer_connected and scene_multiplayer_ready:
		print("🔗 Setting GameNetworkingSockets peer on SceneMultiplayer")
		print("Peer connection status: ", connection_status)
		
		# NOW we can safely set the peer since it's connecting/connected
		multiplayer.multiplayer_peer = peer
		multiplayer_connected = true
		
		print("✅ SceneMultiplayer connected (ID: ", multiplayer.get_unique_id(), ")")
		
		# Update connection status display if it exists
		var status_label = get_node_or_null("ConnectionStatus")
		if status_label:
			status_label.text = "✅ Connected (ID: " + str(multiplayer.get_unique_id()) + ")"
		
		# CRITICAL: Use notify_connected_peers() to handle SceneMultiplayer timing issues
		# This ensures peer_connected signals are emitted after SceneMultiplayer is ready
		peer.notify_connected_peers()
		print("📡 Notified connected peers - Final peers: ", multiplayer.get_peers())
		
		whee(0, multiplayer.get_unique_id())

	# Update player positions if Node2D exists
	var node2d = get_node_or_null("Node2D")
	if node2d:
		for p in node2d.get_children():
			var spawner = node2d.get_node_or_null("MultiplayerSpawner")
			if p == spawner:
				continue
			if p.has_method("get") and p.get("id") == peer.get_unique_id():
				if p.has_method("yippee"):
					p.yippee()

func _exit_tree():
	if peer:
		peer.close()
		print("🔌 GameNetworkingSockets peer closed")
