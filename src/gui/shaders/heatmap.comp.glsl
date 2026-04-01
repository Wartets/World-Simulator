#version 430 core

layout(local_size_x = 8, local_size_y = 8) in;

layout(std430, binding = 0) readonly buffer InputBuffer {
    float data[];
} input_buffer;

layout(rgba8, binding = 1) uniform image2D output_image;

uniform float data_min;
uniform float data_max;

vec3 colormap_turbo(float t) {
    vec3 c0 = vec3(0.190, 0.072, 0.232);
    vec3 c1 = vec3(0.235, 0.368, 0.754);
    vec3 c2 = vec3(0.273, 0.751, 0.436);
    vec3 c3 = vec3(0.935, 0.785, 0.184);
    vec3 c4 = vec3(0.630, 0.071, 0.004);

    t = clamp(t, 0.0, 1.0);
    if (t < 0.25) return mix(c0, c1, t / 0.25);
    if (t < 0.50) return mix(c1, c2, (t - 0.25) / 0.25);
    if (t < 0.75) return mix(c2, c3, (t - 0.50) / 0.25);
    return mix(c3, c4, (t - 0.75) / 0.25);
}

void main() {
    uvec2 pos = gl_GlobalInvocationID.xy;
    ivec2 size = imageSize(output_image);
    if (int(pos.x) >= size.x || int(pos.y) >= size.y) return;

    uint idx = pos.y * uint(size.x) + pos.x;
    float v = input_buffer.data[idx];

    vec4 color = vec4(0.5, 0.5, 0.5, 1.0);
    if (isfinite(v)) {
        float span = max(1e-6, data_max - data_min);
        float t = clamp((v - data_min) / span, 0.0, 1.0);
        color = vec4(colormap_turbo(t), 1.0);
    }

    imageStore(output_image, ivec2(pos), color);
}
