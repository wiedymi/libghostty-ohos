#version 450

layout(location = 0) in vec2 inPosition;
layout(location = 1) in vec2 inTexCoord;
layout(location = 2) in uint inFgColor;
layout(location = 3) in uint inBgColor;

layout(location = 0) out vec2 fragTexCoord;
layout(location = 1) out vec4 fragFgColor;
layout(location = 2) out vec4 fragBgColor;

vec4 unpackColor(uint packed) {
    float r = float((packed >> 16) & 0xFF) / 255.0;
    float g = float((packed >> 8) & 0xFF) / 255.0;
    float b = float(packed & 0xFF) / 255.0;
    float a = float((packed >> 24) & 0xFF) / 255.0;
    return vec4(r, g, b, a);
}

void main() {
    gl_Position = vec4(inPosition, 0.0, 1.0);
    fragTexCoord = inTexCoord;
    fragFgColor = unpackColor(inFgColor);
    fragBgColor = unpackColor(inBgColor);
}
