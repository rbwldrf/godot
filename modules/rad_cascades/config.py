def can_build(env, platform):
    return True  # Can build on all platforms

def configure(env):
    pass  # No special configuration needed

def get_doc_classes():
    return [
        "SDFRadianceCascades",
    ]

def get_doc_path():
    return "doc_classes"