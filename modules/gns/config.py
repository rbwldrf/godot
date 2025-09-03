def can_build(env, platform):
    return True

def configure(env):
    pass

def get_opts(platform):
    return [
        ("builtin_gamenetworkingsockets", "Use the builtin GameNetworkingSockets", True),
    ]


def get_doc_classes():
    return [
        "GameNetworkingSocketsPeer",
    ]


def get_doc_path():
    return "doc_classes"