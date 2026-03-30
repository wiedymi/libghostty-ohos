#version 450

layout(binding = 0) uniform sampler2D fontAtlas;

layout(location = 0) in vec2 fragTexCoord;
layout(location = 1) in vec4 fragFgColor;
layout(location = 2) in vec4 fragBgColor;

layout(location = 0) out vec4 outColor;

void main() {
    // The atlas stores high-resolution glyph coverage, so direct alpha sampling
    // produces cleaner text than treating the texture like a low-fidelity SDF.
    float alpha = texture(fontAtlas, fragTexCoord).r;

    // Blend foreground (glyph) over background
    vec3 color = mix(fragBgColor.rgb, fragFgColor.rgb, alpha);
    outColor = vec4(color, 1.0);
}
