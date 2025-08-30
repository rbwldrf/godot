#[compute]

#version 450

#VERSION_DEFINES

// hddagi_sdf_functions.glsl
// Mobile-optimized SDF evaluation for compute shaders

// Match the C++ SDFPrimitive structure exactly
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

// Primitive type constants
const uint SDF_SPHERE = 0;
const uint SDF_BOX = 1;
const uint SDF_CAPSULE = 2;
const uint SDF_MESH = 3;
const uint SDF_COMBINED = 4;

// Storage buffer for SDF primitives
layout(set = 0, binding = 0, std430) restrict readonly buffer SDFPrimitiveBuffer {
    SDFPrimitive primitives[];
} sdf_data;

// Uniform buffer for scene parameters
layout(set = 0, binding = 1, std140) uniform SDFSceneParams {
    uint primitive_count;
    float scene_scale;
    vec2 padding;
    vec4 scene_bounds_min;
    vec4 scene_bounds_max;
} scene_params;

// Rotation matrix from Euler angles (mobile-optimized)
mat3 rotation_from_euler(vec3 euler) {
    float cx = cos(euler.x);
    float sx = sin(euler.x);
    float cy = cos(euler.y);
    float sy = sin(euler.y);
    float cz = cos(euler.z);
    float sz = sin(euler.z);

    // YXZ rotation order (Godot default)
    return mat3(
        cy * cz + sx * sy * sz, cz * sx * sy - cy * sz, cx * sy,
        cx * sz, cx * cz, -sx,
        cy * sx * sz - cz * sy, cy * cz * sx + sy * sz, cx * cy
    );
}

// Individual SDF evaluators
float sdf_sphere(vec3 p, float radius) {
    return length(p) - radius;
}

float sdf_box(vec3 p, vec3 extents) {
    vec3 q = abs(p) - extents;
    return length(max(q, 0.0)) + min(max(q.x, max(q.y, q.z)), 0.0);
}

float sdf_capsule(vec3 p, float radius, float height) {
    p.y = clamp(p.y, 0.0, height);
    return length(p) - radius;
}

// Smooth min for SDF combination (mobile-friendly k value)
float sdf_smooth_union(float d1, float d2, float k) {
    float h = max(k - abs(d1 - d2), 0.0) / k;
    return min(d1, d2) - h * h * k * 0.25;
}

// Main SDF evaluation function
float evaluate_sdf(vec3 world_pos) {
    float min_distance = 1e10;

    // Early out if outside scene bounds
    if (any(lessThan(world_pos, scene_params.scene_bounds_min.xyz)) ||
        any(greaterThan(world_pos, scene_params.scene_bounds_max.xyz))) {
        return 100.0;  // Large distance for out-of-bounds
    }

    // Evaluate all primitives (consider spatial culling for optimization)
    for (uint i = 0; i < scene_params.primitive_count; i++) {
        SDFPrimitive prim = sdf_data.primitives[i];

        // Transform to local space
        vec3 local_pos = world_pos - prim.position;
        if (any(notEqual(prim.rotation, vec3(0.0)))) {
            mat3 inv_rot = transpose(rotation_from_euler(prim.rotation));
            local_pos = inv_rot * local_pos;
        }

        float distance = 1e10;

        switch (prim.type) {
            case SDF_SPHERE:
                distance = sdf_sphere(local_pos, prim.radius);
                break;

            case SDF_BOX:
                distance = sdf_box(local_pos, prim.extents);
                break;

            case SDF_CAPSULE:
                distance = sdf_capsule(local_pos, prim.radius, prim.extents.y);
                break;
        }

        min_distance = min(min_distance, distance);
    }

    return min_distance;
}

// Gradient computation using central differences
vec3 compute_sdf_gradient(vec3 world_pos, float epsilon) {
    float dx = evaluate_sdf(world_pos + vec3(epsilon, 0.0, 0.0)) -
               evaluate_sdf(world_pos - vec3(epsilon, 0.0, 0.0));
    float dy = evaluate_sdf(world_pos + vec3(0.0, epsilon, 0.0)) -
               evaluate_sdf(world_pos - vec3(0.0, epsilon, 0.0));
    float dz = evaluate_sdf(world_pos + vec3(0.0, 0.0, epsilon)) -
               evaluate_sdf(world_pos - vec3(0.0, 0.0, epsilon));

    return normalize(vec3(dx, dy, dz));
}

// Sample material properties at a position
void sample_sdf_material(vec3 world_pos, out vec3 albedo, out float roughness, out float metallic) {
    float min_distance = 1e10;
    uint closest_primitive = 0;

    // Find closest primitive
    for (uint i = 0; i < scene_params.primitive_count; i++) {
        SDFPrimitive prim = sdf_data.primitives[i];
        vec3 local_pos = world_pos - prim.position;

        if (any(notEqual(prim.rotation, vec3(0.0)))) {
            mat3 inv_rot = transpose(rotation_from_euler(prim.rotation));
            local_pos = inv_rot * local_pos;
        }

        float distance = 1e10;
        switch (prim.type) {
            case SDF_SPHERE:
                distance = sdf_sphere(local_pos, prim.radius);
                break;
            case SDF_BOX:
                distance = sdf_box(local_pos, prim.extents);
                break;
            case SDF_CAPSULE:
                distance = sdf_capsule(local_pos, prim.radius, prim.extents.y);
                break;
        }

        if (distance < min_distance) {
            min_distance = distance;
            closest_primitive = i;
        }
    }

    // Return material of closest primitive
    SDFPrimitive closest = sdf_data.primitives[closest_primitive];
    albedo = closest.albedo;
    roughness = closest.roughness;
    metallic = closest.metallic;
}

// Optimized ray marching for mobile GPUs
struct RayMarchResult {
    bool hit;
    float distance;
    vec3 position;
    vec3 normal;
};

RayMarchResult ray_march_sdf(vec3 ray_origin, vec3 ray_dir, float max_distance) {
    RayMarchResult result;
    result.hit = false;
    result.distance = 0.0;

    const int MAX_STEPS = 64;  // Reduced for mobile
    const float EPSILON = 0.001;
    const float STEP_SCALE = 0.9;  // Conservative stepping

    float t = 0.0;

    for (int i = 0; i < MAX_STEPS; i++) {
        vec3 pos = ray_origin + ray_dir * t;
        float dist = evaluate_sdf(pos);

        if (dist < EPSILON) {
            result.hit = true;
            result.distance = t;
            result.position = pos;
            result.normal = compute_sdf_gradient(pos, EPSILON * 2.0);
            break;
        }

        t += dist * STEP_SCALE;

        if (t > max_distance) {
            break;
        }
    }

    return result;
}