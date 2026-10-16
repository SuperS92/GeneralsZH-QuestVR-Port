// SPDX-License-Identifier: MIT
// Adapted from RDR1 Quest taa_lite.frag, commit 423788baa4959d21d5057243ad31f5c8b40f9f28.
// Copyright (c) 2026 the Red Dead Redemption VR Quest port contributors.
// Full notice: docs/WORKDIR/quest/licenses/RDR1-Quest-MIT.txt.
// GeneralsX @performance Codex 16/10/2026 Preserve source alpha; store confidence separately for copy-compatible output.
#version 450
precision highp float;
layout(location=0) out vec4 result;
layout(location=1) out float resultConfidence;
layout(set=0,binding=0) uniform sampler2D currentColor;
layout(set=0,binding=1) uniform sampler2D currentDepth;
layout(set=0,binding=2) uniform sampler2D history;
layout(set=0,binding=3) uniform sampler2D historyConfidence;
layout(push_constant) uniform Push { mat4 reproject; uvec4 info; vec4 tune; } pc;
mediump vec3 toYCoCg(mediump vec3 c){return vec3(dot(c,vec3(.25,.5,.25)),dot(c,vec3(.5,0,-.5)),dot(c,vec3(-.25,.5,-.25)));}
mediump vec3 toRgb(mediump vec3 c){return vec3(c.x+c.y-c.z,c.x+c.z,c.x-c.y-c.z);}
void main() {
    ivec2 p=ivec2(gl_FragCoord.xy),size=ivec2(pc.info.xy),last=size-1;
    vec2 rcp=1.0/vec2(size);
    vec4 raw=texelFetch(currentColor,p,0);
    // Reset/rejected history has weight one: no neighborhood or history work needed.
    if(pc.info.z!=0u){result=raw;resultConfidence=1.0/16.0;return;}
    vec4 g=textureGather(currentDepth,vec2(p+1)*rcp,0);
    float z=min(min(g.x,g.y),min(g.z,g.w));
    vec4 prev=pc.reproject*vec4((vec2(p)+.5)*rcp*2.0-1.0,z,1);
    if(!(prev.w>1e-6 && prev.z>=0 && prev.z<=prev.w)){
        result=raw;resultConfidence=1.0/16.0;return;
    }
    vec2 hp=(prev.xy/prev.w*.5+.5)*vec2(size);
    vec2 margin=vec2(pc.info.w&0xffffu,pc.info.w>>16u);
    // RDR's two-pixel inset excludes every uninitialized peripheral history texel.
    vec2 loPixel=margin+2.0,hiPixel=vec2(size)-margin-2.0;
    if(!(all(greaterThanEqual(hp,loPixel))&&all(lessThanEqual(hp,hiPixel)))){
        result=raw;resultConfidence=1.0/16.0;return;
    }
    vec4 h=textureLod(history,hp*rcp,0);
    mediump vec3 cur=toYCoCg(raw.rgb),hist=toYCoCg(h.rgb);
    mediump vec3 mean=cur,mean2=cur*cur;
    const ivec2 cross[4]=ivec2[4](ivec2(-1,0),ivec2(1,0),ivec2(0,-1),ivec2(0,1));
    for(int k=0;k<4;++k){mediump vec3 c=toYCoCg(texelFetch(currentColor,clamp(p+cross[k],ivec2(0),last),0).rgb);mean+=c;mean2+=c*c;}
    mean*=.2;mean2*=.2;
    mediump vec3 box=max(1.25*sqrt(max(mean2-mean*mean,vec3(0))),vec3(1.0/255.0));
    mediump vec3 lo=min(mean-box,cur),hi=max(mean+box,cur);
    mediump vec3 center=.5*(lo+hi),extent=max(.5*(hi-lo),vec3(1e-6));
    mediump vec3 delta=hist-center,ratio=abs(delta/extent);
    float maximum=max(ratio.x,max(ratio.y,ratio.z));
    if(maximum>1)hist=center+delta/maximum;
    float nHist=textureLod(historyConfidence,hp*rcp,0).r*16*clamp(1.5-.5*maximum,0.0,1.0);
    float speed=distance(hp,vec2(p)+.5);
    vec2 f=fract(hp-.5);
    float subpixel=4*(f.x*(1-f.x)+f.y*(1-f.y));
    float nMax=max(mix(pc.tune.x,pc.tune.y,clamp(speed/6.0,0.0,1.0))/(1+pc.tune.z*subpixel),2.0);
    float weight=min(nHist+1,nMax),blend=clamp((weight-1)*.5,0.0,1.0);
    mediump vec3 accumulated=mix(hist,cur,1.0/weight);
    mediump vec3 shown=blend>0?clamp(toRgb(mix(cur,accumulated,blend)),0.0,1.0):raw.rgb;
    result=vec4(shown,raw.a);
    resultConfidence=weight/16.0;
}
