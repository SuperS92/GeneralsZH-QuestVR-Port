#version 450
layout(set=0,binding=0)uniform sampler2D worldColor;
layout(set=0,binding=1)uniform sampler2D worldCoverage;
layout(location=0)out vec4 finalColor;
void main(){ivec2 p=ivec2(gl_FragCoord.xy);finalColor=vec4(texelFetch(worldColor,p,0).rgb,texelFetch(worldCoverage,p,0).r);}
