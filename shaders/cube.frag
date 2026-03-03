#version 450

layout(location = 0) in vec3 fragColor;
layout(location = 1) in vec2 fragUV;
layout(location = 2) flat in uint fragTextureId;

layout(set = 0, binding = 0) uniform sampler2D texture0;
layout(set = 0, binding = 1) uniform sampler2D texture1;
layout(set = 0, binding = 2) uniform sampler2D texture2;
layout(set = 0, binding = 3) uniform sampler2D texture3;
layout(set = 0, binding = 4) uniform sampler2D texture4;
layout(set = 0, binding = 5) uniform sampler2D texture5;
layout(set = 0, binding = 6) uniform sampler2D texture6;
layout(set = 0, binding = 7) uniform sampler2D texture7;
layout(set = 0, binding = 8) uniform sampler2D texture8;
layout(set = 0, binding = 9) uniform sampler2D texture9;
layout(set = 0, binding = 10) uniform sampler2D texture10;
layout(set = 0, binding = 11) uniform sampler2D texture11;
layout(set = 0, binding = 12) uniform sampler2D texture12;
layout(set = 0, binding = 13) uniform sampler2D texture13;
layout(set = 0, binding = 14) uniform sampler2D texture14;
layout(set = 0, binding = 15) uniform sampler2D texture15;

layout(location = 0) out vec4 outColor;

void main() {
    // Sample the texture using the texture ID with a switch statement
    vec4 texColor;
    switch(fragTextureId) {
        case 0u: texColor = texture(texture0, fragUV); break;
        case 1u: texColor = texture(texture1, fragUV); break;
        case 2u: texColor = texture(texture2, fragUV); break;
        case 3u: texColor = texture(texture3, fragUV); break;
        case 4u: texColor = texture(texture4, fragUV); break;
        case 5u: texColor = texture(texture5, fragUV); break;
        case 6u: texColor = texture(texture6, fragUV); break;
        case 7u: texColor = texture(texture7, fragUV); break;
        case 8u: texColor = texture(texture8, fragUV); break;
        case 9u: texColor = texture(texture9, fragUV); break;
        case 10u: texColor = texture(texture10, fragUV); break;
        case 11u: texColor = texture(texture11, fragUV); break;
        case 12u: texColor = texture(texture12, fragUV); break;
        case 13u: texColor = texture(texture13, fragUV); break;
        case 14u: texColor = texture(texture14, fragUV); break;
        case 15u: texColor = texture(texture15, fragUV); break;
        default: texColor = vec4(1.0, 0.0, 1.0, 1.0); break; // Magenta for missing texture
    }

    // Modulate by vertex color (white by default)
    outColor = texColor * vec4(fragColor, 1.0);
}
