extends SceneTree

class TestNode:
	extends Node
	
	@rpc("any_peer", "call_local", "reliable")
	func test_rpc(message: String):
		print("RPC received: ", message)

var peer: GameNetworkingSocketsPeer
var is_server: bool = false

func _init():
	var args = OS.get_cmdline_user_args()
	is_server = args.has("server")
	print("Mode: ", "server" if is_server else "client")
	
	# Create the peer
	peer = GameNetworkingSocketsPeer.new()
	
	# Create a test node
	var test_node = TestNode.new()
	test_node.name = "Test"
	root.add_child(test_node)
	
	# Create server or client
	if is_server:
		print("Creating server...")
		peer.create_server(12345, 10)
	else:
		print("Creating client...")
		peer.create_client("127.0.0.1", 12345)
	
	# NUCLEAR OPTION: Force SceneMultiplayer as the default interface
	print("Current default interface: ", MultiplayerAPI.get_default_interface())
	MultiplayerAPI.set_default_interface("SceneMultiplayer")
	print("Set default interface to SceneMultiplayer")
	
	# Now create a fresh multiplayer system with SceneMultiplayer as default
	var scene_multiplayer = MultiplayerAPI.create_default_interface()
	print("Created default interface instance: ", scene_multiplayer.get_instance_id())
	print("Default interface type: ", scene_multiplayer.get_class())
	
	# Set the peer on this instance
	scene_multiplayer.multiplayer_peer = peer  
	print("Set GameNetworkingSockets peer on SceneMultiplayer")
	
	# Force SceneTree to use this specific instance
	set_multiplayer(scene_multiplayer)
	print("Set SceneTree to use our SceneMultiplayer instance")
	
	# Verify everything is aligned
	var final_multiplayer = get_multiplayer()
	print("Final multiplayer instance: ", final_multiplayer.get_instance_id())  
	print("All instances aligned: ", scene_multiplayer == final_multiplayer)
	
	# Check connected peers initially  
	# print("Initial connected peers: ", retrieved_multiplayer_root.get_peer_ids())
	
	# Set up timer to test periodically
	var timer = Timer.new()
	timer.wait_time = 2.0
	timer.autostart = true
	timer.timeout.connect(_test_rpc)
	root.add_child(timer)
	
	# Set up quit timer
	var quit_timer = Timer.new()
	quit_timer.wait_time = 20.0
	quit_timer.autostart = true
	quit_timer.timeout.connect(quit)
	root.add_child(quit_timer)

func _test_rpc():
	var test_node = root.get_node("Test")
	if test_node:
		var test_multiplayer_api = test_node.get_multiplayer()
		print("\n=== RPC Test ===")
		print("Test node path: ", test_node.get_path())
		print("Test node multiplayer instance: ", test_multiplayer_api.get_instance_id())
		print("Test node unique ID: ", test_multiplayer_api.get_unique_id())
		# Try to call RPC on all connected peers
		# var connected_peers = multiplayer_api.get_peer_ids()
		var connected_peers = [2]  # Hardcode for testing
		for peer_id in connected_peers:
			print("Calling RPC on peer ", peer_id)
			test_node.test_rpc.rpc_id(peer_id, "Hello from " + str(test_multiplayer_api.get_unique_id()))
	else:
		print("Test node not found")