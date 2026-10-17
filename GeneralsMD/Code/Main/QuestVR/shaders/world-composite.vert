#version 450
// Fullscreen handoff, no camera or simulation change.
void main(){vec2 p=vec2((gl_VertexIndex<<1)&2,gl_VertexIndex&2);gl_Position=vec4(p*2.-1.,0.,1.);}
