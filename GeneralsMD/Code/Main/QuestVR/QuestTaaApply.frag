// SPDX-License-Identifier: GPL-3.0-or-later
#version 450
layout(location=0) out vec4 result;
layout(set=0,binding=2) uniform sampler2D history;
void main() {
    // Pipeline writes RGB only; retain the original game alpha exactly.
    result=vec4(texelFetch(history,ivec2(gl_FragCoord.xy),0).rgb,1);
}
