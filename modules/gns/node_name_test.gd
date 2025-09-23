extends SceneTree

class ProfilerTest:
	extends Node
	
	@rpc("any_peer", "call_local", "reliable")
	func test_rpc(message: String):
		print("RPC received by ProfilerTest: ", message)

var peer: GameNetworkingSocketsPeer
var is_server: bool = false

func _init():
	var args = OS.get_cmdline_user_args()
	is_server = args.has("server")
	print("Mode: ", "server" if is_server else "client")
	
	# Create the peer
	peer = GameNetworkingSocketsPeer.new()
	
	# Create a test node with the problematic long name
	var test_node = ProfilerTest.new()
	test_node.name = "ProfilerTest"  # This should cause truncation
	root.add_child(test_node)
	
	print("Test node name: '", test_node.name, "'")
	print("Test node path: '", test_node.get_path(), "'")
	
	# Create server or client
	if is_server:
		print("Creating server...")
		peer.create_server(12345, 10)
	else:
		print("Creating client...")
		peer.create_client("127.0.0.1", 12345)
	
	# Set up multiplayer with default path
	var multiplayer_api = SceneMultiplayer.new()
	set_multiplayer(multiplayer_api)
	multiplayer_api.multiplayer_peer = peer
	
	# Set up timer to test periodically
	var timer = Timer.new()
	timer.wait_time = 2.0
	timer.autostart = true
	timer.timeout.connect(_test_rpc)
	root.add_child(timer)
	
	# Set up quit timer
	var quit_timer = Timer.new()
	quit_timer.wait_time = 15.0
	quit_timer.autostart = true
	quit_timer.timeout.connect(quit)
	root.add_child(quit_timer)

func _test_rpc():
	print("\n=== RPC Test with ProfilerTest node ===")
	var test_node = root.get_node("ProfilerTest")
	if test_node:
		print("Found test node: ", test_node.name)
		print("Test node path: ", test_node.get_path())
		
		# Try to call RPC
		if is_server:
			print("Server calling RPC on client peer 2")
			test_node.test_rpc.rpc_id(2, "Hello from server to ProfilerTest")
		else:
			print("Client calling RPC on server peer 1")  
			test_node.test_rpc.rpc_id(1, "Hello from client to ProfilerTest")
	else:
		print("❌ ProfilerTest node not found!")
		
	# Also try to find it via root to see if there's a path issue
	var test_node_alt = root.get_node_or_null("ProfilerTest")
	if test_node_alt:
		print("✅ Found via root.get_node_or_null: ProfilerTest")
	else:
		print("❌ Not found via root.get_node_or_null: ProfilerTest")
		
	# Check if the name got truncated by listing all children
	print("Root children:")
	for child in root.get_children():
		print("  - '", child.name, "' (length: ", child.name.length(), ")")