#version 330 core

in vec2 frag_uv;
out vec4 out_color;

uniform sampler2D heatmap_tex;

void main() {
    out_color = texture(heatmap_tex, frag_uv);
}
