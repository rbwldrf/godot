/**************************************************************************/
/*  hddagi_sdf_scene.cpp                                                          */
/**************************************************************************/
/*                         This file is part of:                          */
/*                             GODOT ENGINE                               */
/*                        https://godotengine.org                         */
/**************************************************************************/
/* Copyright (c) 2014-present Godot Engine contributors (see AUTHORS.md). */
/* Copyright (c) 2007-2014 Juan Linietsky, Ariel Manzur.                  */
/*                                                                        */
/* Permission is hereby granted, free of charge, to any person obtaining  */
/* a copy of this software and associated documentation files (the        */
/* "Software"), to deal in the Software without restriction, including    */
/* without limitation the rights to use, copy, modify, merge, publish,    */
/* distribute, sublicense, and/or sell copies of the Software, and to     */
/* permit persons to whom the Software is furnished to do so, subject to  */
/* the following conditions:                                              */
/*                                                                        */
/* The above copyright notice and this permission notice shall be         */
/* included in all copies or substantial portions of the Software.        */
/*                                                                        */
/* THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,        */
/* EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF     */
/* MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. */
/* IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY   */
/* CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,   */
/* TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE      */
/* SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.                 */
/**************************************************************************/

#include "hddagi_sdf_scene.h"
#include "servers/rendering/rendering_device_binds.h"

void HDDAGISDFScene::_bind_methods() {
    ClassDB::bind_method(D_METHOD("build_from_scene", "root_node"), &HDDAGISDFScene::build_from_scene);
    ClassDB::bind_method(D_METHOD("get_scene_bounds"), &HDDAGISDFScene::get_scene_bounds);
	ClassDB::bind_method(D_METHOD("get_primitive_count"), &HDDAGISDFScene::get_primitive_count);
    ClassDB::bind_method(D_METHOD("initialize_gpu_resources"), &HDDAGISDFScene::initialize_gpu_resources);

    ADD_PROPERTY(PropertyInfo(Variant::INT, "primitive_count", PROPERTY_HINT_NONE, "", PROPERTY_USAGE_EDITOR | PROPERTY_USAGE_READ_ONLY),
                 "", "get_primitive_count");
}

HDDAGISDFScene::HDDAGISDFScene() {
    primitives.reserve(256);  // Initial reservation for performance
}

HDDAGISDFScene::~HDDAGISDFScene() {
    cleanup_gpu_resources();
}

void HDDAGISDFScene::build_from_scene(Node3D *root_node) {
    ERR_FAIL_NULL(root_node);

    // Clear existing data
    primitives.clear();
    scene_bounds = AABB();

    // Recursively process scene nodes
    TypedArray<Node> mesh_instances = root_node->find_children("*", "MeshInstance3D", true, false);

    for (int i = 0; i < mesh_instances.size(); i++) {
        MeshInstance3D *mesh_inst = Object::cast_to<MeshInstance3D>(mesh_instances[i]);
        if (mesh_inst && mesh_inst->get_mesh().is_valid()) {
            add_mesh_instance(mesh_inst);
        }
    }

    // Build spatial acceleration structure
    spatial_index.build(primitives);

    // Mark for GPU update
    gpu_buffer_dirty = true;

    print_line(vformat("HDDAGI: Built SDF scene with %d primitives", primitives.size()));
}

void HDDAGISDFScene::add_mesh_instance(const MeshInstance3D *mesh_inst) {
    ERR_FAIL_NULL(mesh_inst);

    Ref<Mesh> mesh = mesh_inst->get_mesh();
    ERR_FAIL_COND(!mesh.is_valid());

    Transform3D global_transform = mesh_inst->get_global_transform();
    AABB mesh_aabb = mesh->get_aabb();
    mesh_aabb = global_transform.xform(mesh_aabb);

    // For now, approximate complex meshes with oriented bounding boxes
    // TODO: Implement proper mesh SDF generation for complex geometry
    SDFPrimitive primitive;
    primitive.position = mesh_aabb.get_center();
    primitive.extents = mesh_aabb.get_size() * 0.5f;
    primitive.type = SDF_BOX;

    // Extract rotation from transform
    Basis basis = global_transform.basis.orthonormalized();
    primitive.rotation = basis.get_euler();

    // Default material properties (would extract from mesh material)
    primitive.albedo = Vector3(0.8f, 0.8f, 0.8f);
    primitive.roughness = 0.5f;
    primitive.metallic = 0.0f;
    primitive.emission = Vector3(0.0f, 0.0f, 0.0f);

    // Add to scene
    primitives.push_back(primitive);
    scene_bounds = scene_bounds.merge(mesh_aabb);
}

void HDDAGISDFScene::initialize_gpu_resources(RenderingDevice *p_rd) {
    ERR_FAIL_NULL(p_rd);
    rd = p_rd;

    // Create GPU buffer for primitives
    PackedByteArray buffer_data;
    buffer_data.resize(PRIMITIVE_BUFFER_SIZE);

    primitive_buffer = rd->storage_buffer_create(PRIMITIVE_BUFFER_SIZE, buffer_data);

    // Create uniform set for compute shaders
    RD::Uniform uniform;
    uniform.uniform_type = RenderingDevice::UNIFORM_TYPE_STORAGE_BUFFER;
    uniform.binding = 0;
    RID sampler = (primitive_buffer);
	uniform.append_id(sampler);
    // Note: In actual implementation, you'd get the shader from your compute shader
    // For now, using a placeholder
    primitive_uniform_set = rd->uniform_set_create({uniform}, RID(), 0);

    // Upload initial data
    if (!primitives.is_empty()) {
        update_gpu_buffer();
    }
}

void HDDAGISDFScene::update_gpu_buffer() {
    if (!gpu_buffer_dirty || !rd || !primitive_buffer.is_valid()) {
        return;
    }

    // Prepare data for GPU upload
    PackedByteArray buffer_data;
    buffer_data.resize(primitives.size() * sizeof(SDFPrimitive));

    memcpy(buffer_data.ptrw(), primitives.ptr(), primitives.size() * sizeof(SDFPrimitive));

    // Update GPU buffer
    rd->buffer_update(primitive_buffer, 0, primitives.size() * sizeof(SDFPrimitive), &buffer_data);

    gpu_buffer_dirty = false;
    dirty_primitives.clear();
}

void HDDAGISDFScene::cleanup_gpu_resources() {
    if (rd) {
        if (primitive_uniform_set.is_valid()) {
            rd->free(primitive_uniform_set);
        }
        if (primitive_buffer.is_valid()) {
            rd->free(primitive_buffer);
        }
    }
}

float HDDAGISDFScene::sample_sdf(const Vector3& position) const {
    float min_distance = FLT_MAX;

    // Query nearby primitives using spatial index
    LocalVector<uint32_t> nearby_primitives;
    spatial_index.query_sphere(position, 10.0f, nearby_primitives);  // 10m query radius

    for (uint32_t idx : nearby_primitives) {
        const SDFPrimitive& prim = primitives[idx];
        float distance = FLT_MAX;

        Vector3 local_pos = position - prim.position;

        // Apply inverse rotation for oriented primitives
        if (prim.rotation != Vector3()) {
            Basis rotation_basis = Basis::from_euler(prim.rotation);
            local_pos = rotation_basis.inverse().xform(local_pos);
        }

        switch (prim.type) {
            case SDF_SPHERE:
                distance = local_pos.length() - prim.radius;
                break;

            case SDF_BOX:
                {
                    Vector3 q = local_pos.abs() - prim.extents;
                    distance = Vector3(MAX(q.x, 0.0f), MAX(q.y, 0.0f), MAX(q.z, 0.0f)).length() +
                              MIN(MAX(q.x, MAX(q.y, q.z)), 0.0f);
                }
                break;

            case SDF_CAPSULE:
                {
                    float h = prim.extents.y;
                    Vector3 pa = local_pos - Vector3(0, CLAMP(local_pos.y, 0.0f, h), 0);
                    distance = pa.length() - prim.radius;
                }
                break;

            default:
                break;
        }

        min_distance = MIN(min_distance, distance);
    }

    return min_distance;
}

Vector3 HDDAGISDFScene::compute_gradient(const Vector3& position, float epsilon) const {
    // Finite difference gradient computation
    float dx = sample_sdf(position + Vector3(epsilon, 0, 0)) - sample_sdf(position - Vector3(epsilon, 0, 0));
    float dy = sample_sdf(position + Vector3(0, epsilon, 0)) - sample_sdf(position - Vector3(0, epsilon, 0));
    float dz = sample_sdf(position + Vector3(0, 0, epsilon)) - sample_sdf(position - Vector3(0, 0, epsilon));

    return Vector3(dx, dy, dz).normalized();
}

void SDFOctree::build(const LocalVector<SDFPrimitive>& primitives) {
    nodes.clear();
    if (primitives.is_empty()) return;

    // Calculate bounds
    AABB total_bounds;
    for (size_t i = 0; i < primitives.size(); i++) {
        Vector3 prim_min = primitives[i].position - primitives[i].extents;
        Vector3 prim_max = primitives[i].position + primitives[i].extents;
        AABB prim_bounds(prim_min, prim_max - prim_min);

        if (i == 0) {
            total_bounds = prim_bounds;
        } else {
            total_bounds = total_bounds.merge(prim_bounds);
        }
    }

    // Build root node
    Node root;
    root.bounds = total_bounds;
    for (uint32_t i = 0; i < primitives.size(); i++) {
        root.primitive_indices.push_back(i);
    }

    nodes.push_back(root);
    // Recursively subdivide (implementation details omitted for brevity)
}