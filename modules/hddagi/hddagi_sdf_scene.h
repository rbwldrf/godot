/**************************************************************************/
/*  hddagi_sdf_scene.h                                                          */
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


#ifndef HDDAGI_SDF_SCENE_H
#define HDDAGI_SDF_SCENE_H

#include "core/io/resource.h"
#include "core/math/aabb.h"
#include "core/templates/local_vector.h"
#include "servers/rendering/rendering_device.h"
#include "scene/3d/mesh_instance_3d.h"

// Mobile-optimized SDF primitive types
enum SDFPrimitiveType : uint32_t {
    SDF_SPHERE = 0,
    SDF_BOX = 1,
    SDF_CAPSULE = 2,
    SDF_MESH = 3,  // For complex geometry
    SDF_COMBINED = 4  // Boolean operations
};

// GPU-friendly primitive data structure (16-byte aligned)
struct alignas(16) SDFPrimitive {
    Vector3 position;
    float radius;  // Also used as primary dimension

    Vector3 extents;  // For box/capsule
    uint32_t type;

    Vector3 rotation;  // Euler angles for simplicity
    uint32_t operation;  // For combined SDFs (union/intersection/subtraction)

    // Material properties for GI
    Vector3 albedo;
    float roughness;

    Vector3 emission;
    float metallic;

    // Padding for 16-byte alignment
    float padding[2];
};

// Acceleration structure for spatial queries
class SDFOctree {
public:
    struct Node {
        AABB bounds;
        LocalVector<uint32_t> primitive_indices;
        int32_t children[8];  // -1 if leaf

        Node() {
            for (int i = 0; i < 8; i++) {
                children[i] = -1;
            }
        }
    };

private:
    LocalVector<Node> nodes;
    uint32_t max_depth = 8;
    uint32_t min_primitives_per_node = 4;

public:
    void build(const LocalVector<SDFPrimitive>& primitives);
    void query_frustum(const Projection& frustum, LocalVector<uint32_t>& out_indices) const;
    void query_sphere(const Vector3& center, float radius, LocalVector<uint32_t>& out_indices) const;
};

// Main SDF scene representation class
class HDDAGISDFScene : public Resource {
    GDCLASS(HDDAGISDFScene, Resource);

private:
    // CPU-side primitive storage
    LocalVector<SDFPrimitive> primitives;
    SDFOctree spatial_index;

    // GPU resources
    RID primitive_buffer;
    RID primitive_uniform_set;
    RenderingDevice *rd = nullptr;

    // Scene bounds for cascade setup
    AABB scene_bounds;

    // Memory management
    static constexpr size_t MAX_PRIMITIVES = 4096;  // Mobile-friendly limit
    static constexpr size_t PRIMITIVE_BUFFER_SIZE = sizeof(SDFPrimitive) * MAX_PRIMITIVES;

    // Dirty tracking for GPU updates
    bool gpu_buffer_dirty = true;
    LocalVector<uint32_t> dirty_primitives;

protected:
    static void _bind_methods();

public:
    HDDAGISDFScene();
    ~HDDAGISDFScene();

    // Scene building from Godot nodes
    void build_from_scene(Node3D *root_node);
    void add_mesh_instance(const MeshInstance3D *mesh_inst);
    uint32_t add_primitive(const SDFPrimitive& primitive);
    void update_primitive(uint32_t index, const SDFPrimitive& primitive);
    void remove_primitive(uint32_t index);

    // GPU resource management
    void initialize_gpu_resources(RenderingDevice *p_rd);
    void update_gpu_buffer();
    void cleanup_gpu_resources();

    // Query interface for probe updates
    float sample_sdf(const Vector3& position) const;
    Vector3 compute_gradient(const Vector3& position, float epsilon = 0.01f) const;
    LocalVector<uint32_t> get_primitives_in_cascade(const AABB& cascade_bounds) const;

    // Getters
    RID get_primitive_buffer() const { return primitive_buffer; }
    RID get_uniform_set() const { return primitive_uniform_set; }
    const AABB& get_scene_bounds() const { return scene_bounds; }
    uint32_t get_primitive_count() const { return primitives.size(); }

    // Debug visualization
    Array get_debug_lines() const;
};

#endif