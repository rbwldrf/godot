//
// Created by RB Waldorff on 30.8.2025.
//

#ifndef SDF_RADIANCE_CASCADES_H
#define SDF_RADIANCE_CASCADES_H

#include "core/io/resource.h"
#include "core/math/transform_3d.h"
#include "servers/rendering/rendering_device.h"
#include "modules/hddagi/hddagi_sdf_scene.h" // Reuse our SDF implementation

// Radiance Cascade configuration
struct RadianceCascadeConfig {
    // Cascade parameters
    uint32_t num_cascades = 5;
    uint32_t base_probe_count = 64;  // Per dimension
    uint32_t base_ray_count = 4;     // Rays per probe at cascade 0
    float cascade_scale_factor = 4.0f; // Spatial scaling between cascades

    // Mobile optimizations
    bool use_fp16_storage = true;    // Half precision for mobile
    bool temporal_filtering = false;  // Avoid temporal accumulation on mobile
    uint32_t max_ray_steps = 32;     // Reduced for mobile GPUs

    // Quality settings
    float ray_march_epsilon = 0.01f;
    float probe_hysteresis = 0.95f;  // Temporal stability
};

// Single cascade level data
class RadianceCascade {
public:
    struct ProbeData {
        Vector3 position;
        float radius;  // Influence radius

        // Compact radiance storage (RGB + padding)
        uint16_t radiance[4];  // FP16 for mobile

        // Directional information (optional for higher cascades)
        uint8_t dominant_direction[4];  // Quantized direction
    };

	LocalVector<ProbeData> probes;

	RID probe_buffer;

private:
    uint32_t cascade_level;
    uint32_t probe_resolution;  // Probes per dimension
    uint32_t ray_count;         // Rays per probe
    float spatial_extent;       // World space size

    RID radiance_texture;  // 3D texture for trilinear filtering

public:
    void initialize(uint32_t level, const RadianceCascadeConfig& config){}
    void update_probes(const HDDAGISDFScene* sdf_scene, RenderingDevice* rd);
	Vector3 sample_radiance(const Vector3& world_pos) const;
};

// Main SDF Radiance Cascades system
class SDFRadianceCascades : public Resource {
    GDCLASS(SDFRadianceCascades, Resource);

private:
    RadianceCascadeConfig config;
    LocalVector<RadianceCascade> cascades;
    HDDAGISDFScene* sdf_scene;

    // GPU resources
    RenderingDevice* rd = nullptr;
    RID compute_shader;
    RID merge_shader;

    // Unified radiance field
    RID final_radiance_texture;

    // Performance monitoring
    float last_update_time_ms = 0.0f;

protected:
    static void _bind_methods() {
    	ClassDB::bind_method(D_METHOD("initialize"), &SDFRadianceCascades::initialize);
    	ClassDB::bind_method(D_METHOD("update"), &SDFRadianceCascades::update);

    }

public:
    SDFRadianceCascades(){}
    ~SDFRadianceCascades(){}

    // Initialization
    void initialize(HDDAGISDFScene* p_sdf_scene, RenderingDevice* p_rd);
    void set_config(const RadianceCascadeConfig& p_config);

	void merge_cascades();
	auto create_cascade_params_buffer(uint32_t index);
	// Main update - this is where the magic happens
    void update(float delta_time);

	void update_cascade(uint32_t cascade_index);

    // Query interface
    Color get_radiance_at_point(const Vector3& world_pos) const;
    Vector3 get_dominant_light_direction(const Vector3& world_pos) const;

    // Mobile-specific optimizations
    void adapt_to_thermal_state(float thermal_factor);
    void set_quality_tier(int tier);

    // Debug
    float get_last_update_time() const { return last_update_time_ms; }
    Array get_cascade_debug_info() const;
};

// Implementation details
void SDFRadianceCascades::initialize(HDDAGISDFScene* p_sdf_scene, RenderingDevice* p_rd) {
    sdf_scene = p_sdf_scene;
    rd = p_rd;

    // Initialize cascades with exponential spacing
    cascades.resize(config.num_cascades);

    for (uint32_t i = 0; i < config.num_cascades; i++) {
        cascades[i].initialize(i, config);

        // Key insight: Higher cascades have more rays but fewer probes
        // This matches the penumbra hypothesis
        uint32_t rays_at_level = config.base_ray_count * pow(4, i);
        uint32_t probes_at_level = config.base_probe_count / pow(2, i);

        print_line(vformat("Cascade %d: %d probes, %d rays each",
                          i, probes_at_level * probes_at_level * probes_at_level,
                          rays_at_level));
    }

    // Load compute shaders
    //load_compute_shaders();
}

inline void SDFRadianceCascades::merge_cascades() {
}

inline auto SDFRadianceCascades::create_cascade_params_buffer(uint32_t index) {
	return RID();
}
inline void SDFRadianceCascades::update_cascade(uint32_t cascade_index) {
    RadianceCascade& cascade = cascades[cascade_index];

    // Dispatch compute shader for this cascade
    // Each thread handles one probe
    uint32_t probe_count = cascade.probes.size();
    uint32_t workgroup_size = 64;  // Mobile-optimized
    uint32_t dispatch_count = (probe_count + workgroup_size - 1) / workgroup_size;

    // Bind resources
    RD::Uniform sdf_uniform;
    sdf_uniform.uniform_type = RenderingDevice::UNIFORM_TYPE_STORAGE_BUFFER;
    sdf_uniform.binding = 0;
    sdf_uniform.append_id(sdf_scene->get_primitive_buffer());

    RD::Uniform probe_uniform;
    probe_uniform.uniform_type = RenderingDevice::UNIFORM_TYPE_STORAGE_BUFFER;
    probe_uniform.binding = 1;
    probe_uniform.append_id(cascade.probe_buffer);

    RD::Uniform params_uniform;
    params_uniform.uniform_type = RenderingDevice::UNIFORM_TYPE_UNIFORM_BUFFER;
    params_uniform.binding = 2;
    params_uniform.append_id(create_cascade_params_buffer(cascade_index));

    auto uniform_set = rd->uniform_set_create(Vector<RenderingDevice::Uniform>{sdf_uniform, probe_uniform, params_uniform},
                                              compute_shader, 0);

    // Dispatch cascade update
    auto compute_list = rd->compute_list_begin();
    rd->compute_list_bind_compute_pipeline(compute_list, compute_shader);
    rd->compute_list_bind_uniform_set(compute_list, uniform_set, 0);
    rd->compute_list_dispatch(compute_list, dispatch_count, 1, 1);
    rd->compute_list_end();

    rd->submit();
    rd->sync();
}


void SDFRadianceCascades::update(float delta_time) {
	//auto start_time = Time::get_singleton()->get_ticks_usec();

	// Update each cascade independently (can be parallelized)
	for (uint32_t i = 0; i < config.num_cascades; i++) {
		update_cascade(i);
	}

	// Merge cascades into final radiance field
	merge_cascades();

	//last_update_time_ms = (Time::get_singleton()->get_ticks_usec() - start_time) / 1000.0f;
}
// Compute shader implementation
const char* sdf_radiance_cascade_compute = R"(
#version 450

// Mobile-optimized SDF Radiance Cascades compute shader
// Combines SDF ray marching with Radiance Cascades probe updates

layout(local_size_x = 64) in;

// SDF primitives (from our existing implementation)
struct SDFPrimitive {
    vec3 position;
    float radius;
    vec3 extents;
    uint type;
    vec3 rotation;
    uint operation;
    vec3 albedo;
    float roughness;
    vec3 emission;
    float metallic;
    float padding[2];
};

layout(set = 0, binding = 0, std430) readonly buffer SDFData {
    SDFPrimitive primitives[];
} sdf_data;

// Probe data for this cascade
struct Probe {
    vec3 position;
    float radius;
    uvec2 radiance_packed;  // FP16 RGB packed
};

layout(set = 0, binding = 1, std430) buffer ProbeData {
    Probe probes[];
} probe_data;

// Cascade parameters
layout(set = 0, binding = 2, std140) uniform CascadeParams {
    uint cascade_level;
    uint ray_count;
    float ray_max_distance;
    float ray_march_epsilon;
    uint max_ray_steps;
    uint probe_count;
    float spatial_extent;
    float temporal_blend;
} params;

// Include SDF evaluation functions from previous implementation
float evaluate_sdf(vec3 pos);  // Already defined in previous artifacts

// Optimized ray marching for mobile using SDF
vec3 trace_ray_sdf(vec3 origin, vec3 direction, float max_dist) {
    float t = 0.0;
    vec3 radiance = vec3(0.0);

    // Mobile-optimized stepping
    for (uint step = 0; step < params.max_ray_steps; step++) {
        vec3 pos = origin + direction * t;
        float dist = evaluate_sdf(pos);

        // Hit surface?
        if (dist < params.ray_march_epsilon) {
            // Sample material at hit point
            vec3 albedo;
            float roughness, metallic;
            sample_sdf_material(pos, albedo, roughness, metallic);

            // Simple lighting model for mobile
            vec3 normal = compute_sdf_gradient(pos, params.ray_march_epsilon);
            float ndotl = max(dot(normal, vec3(0.577, 0.577, 0.577)), 0.0);  // Fake sun

            radiance = albedo * ndotl;
            break;
        }

        // Advance ray (conservative stepping for mobile)
        t += dist * 0.8;

        if (t > max_dist) {
            // Sky contribution (simplified)
            radiance = vec3(0.1, 0.15, 0.3);
            break;
        }
    }

    return radiance;
}

// Generate ray directions for cascade level
vec3 generate_ray_direction(uint ray_index, uint total_rays) {
    // Use golden ratio spiral for better distribution
    float golden_ratio = (1.0 + sqrt(5.0)) / 2.0;
    float theta = 2.0 * 3.14159 * float(ray_index) / golden_ratio;
    float phi = acos(1.0 - 2.0 * float(ray_index) / float(total_rays));

    return vec3(
        sin(phi) * cos(theta),
        sin(phi) * sin(theta),
        cos(phi)
    );
}

void main() {
    uint probe_id = gl_GlobalInvocationID.x;
    if (probe_id >= params.probe_count) return;

    Probe probe = probe_data.probes[probe_id];
    vec3 accumulated_radiance = vec3(0.0);

    // Cast rays from this probe
    for (uint ray = 0; ray < params.ray_count; ray++) {
        vec3 direction = generate_ray_direction(ray, params.ray_count);
        vec3 radiance = trace_ray_sdf(probe.position, direction, params.ray_max_distance);
        accumulated_radiance += radiance;
    }

    // Average radiance
    accumulated_radiance /= float(params.ray_count);

    // Temporal filtering (optional for mobile)
    if (params.temporal_blend > 0.0) {
        vec3 old_radiance = unpack_fp16_radiance(probe.radiance_packed);
        accumulated_radiance = mix(accumulated_radiance, old_radiance, params.temporal_blend);
    }

    // Pack and store
    probe_data.probes[probe_id].radiance_packed = pack_fp16_radiance(accumulated_radiance);
}

// FP16 packing for mobile memory efficiency
uvec2 pack_fp16_radiance(vec3 radiance) {
    uint r = packHalf2x16(vec2(radiance.r, 0.0));
    uint g = packHalf2x16(vec2(radiance.g, 0.0));
    uint b = packHalf2x16(vec2(radiance.b, 0.0));
    return uvec2(r | (g << 16), b);
}

vec3 unpack_fp16_radiance(uvec2 packed) {
    float r = unpackHalf2x16(packed.x).x;
    float g = unpackHalf2x16(packed.x >> 16).x;
    float b = unpackHalf2x16(packed.y).x;
    return vec3(r, g, b);
}
)";

#endif