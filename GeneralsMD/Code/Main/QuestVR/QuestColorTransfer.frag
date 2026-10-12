// SPDX-License-Identifier: GPL-3.0-or-later
#version 450
layout(set = 0, binding = 0) uniform sampler2D sourceImage;
layout(location = 0) out vec4 destinationColor;
void main() {
    // BGRA image sampling returns canonical RGBA. No manual swizzle or gamma.
    destinationColor = texelFetch(sourceImage, ivec2(gl_FragCoord.xy), 0);
}
