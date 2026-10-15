// SPDX-License-Identifier: MIT
// Adapted from RDR1 Quest taa_lite.frag, commit 423788baa4959d21d5057243ad31f5c8b40f9f28.
// Copyright (c) 2026 the Red Dead Redemption VR Quest port contributors.
// Full notice: docs/WORKDIR/quest/licenses/RDR1-Quest-MIT.txt.
// GeneralsX @feature Codex 16/10/2026 Full-frame, same-eye camera/depth TAA; no jitter or near-field exclusion.
#version 450
precision highp float;
layout(location=0) out vec4 result;
layout(set=0,binding=0) uniform sampler2D currentColor;
layout(set=0,binding=1) uniform sampler2D currentDepth;
layout(set=0,binding=2) uniform sampler2D history;
layout(push_constant) uniform Push { mat4 reproject; uvec4 info; vec4 tune; } pc;
vec3 toYCoCg(vec3 c){return vec3(dot(c,vec3(.25,.5,.25)),dot(c,vec3(.5,0,-.5)),dot(c,vec3(-.25,.5,-.25)));}
vec3 toRgb(vec3 c){return vec3(c.x+c.y-c.z,c.x+c.z,c.x-c.y-c.z);}
vec4 historyCubic(vec2 pos,vec2 rcp) {
    vec2 t1=floor(pos-.5)+.5,f=pos-t1;
    vec2 w0=f*(-.5+f*(1.0-.5*f)),w1=1.0+f*f*(-2.5+1.5*f);
    vec2 w2=f*(.5+f*(2.0-1.5*f)),w3=f*f*(-.5+.5*f),w12=w1+w2;
    vec2 t0=(t1-1)*rcp,t3=(t1+2)*rcp,t12=(t1+w2/w12)*rcp;
    vec4 center=textureLod(history,t12,0);
    vec3 c=textureLod(history,vec2(t12.x,t0.y),0).rgb*(w12.x*w0.y)
        +textureLod(history,vec2(t0.x,t12.y),0).rgb*(w0.x*w12.y)
        +center.rgb*(w12.x*w12.y)
        +textureLod(history,vec2(t3.x,t12.y),0).rgb*(w3.x*w12.y)
        +textureLod(history,vec2(t12.x,t3.y),0).rgb*(w12.x*w3.y);
    float sum=w12.x*w0.y+w0.x*w12.y+w12.x*w12.y+w3.x*w12.y+w12.x*w3.y;
    return vec4(max(c/sum,vec3(0)),center.a);
}
void main() {
    ivec2 p=ivec2(gl_FragCoord.xy),size=ivec2(pc.info.xy),last=size-1;
    vec2 rcp=1.0/vec2(size);
    vec3 raw=texelFetch(currentColor,p,0).rgb,cur=toYCoCg(raw);
    vec3 mean=cur,mean2=cur*cur;
    const ivec2 cross[4]=ivec2[4](ivec2(-1,0),ivec2(1,0),ivec2(0,-1),ivec2(0,1));
    for(int k=0;k<4;++k){vec3 c=toYCoCg(texelFetch(currentColor,clamp(p+cross[k],ivec2(0),last),0).rgb);mean+=c;mean2+=c*c;}
    mean*=.2;mean2*=.2;
    vec3 box=max(1.25*sqrt(max(mean2-mean*mean,vec3(0))),vec3(1.0/255.0));
    vec3 lo=min(mean-box,cur),hi=max(mean+box,cur),hist=cur;
    float nHist=0,speed=0,subpixel=0;
    if(pc.info.z==0u) {
        vec4 g=textureGather(currentDepth,vec2(p+1)*rcp,0);
        float z=min(min(g.x,g.y),min(g.z,g.w));
        vec4 prev=pc.reproject*vec4((vec2(p)+.5)*rcp*2.0-1.0,z,1);
        if(prev.w>1e-6 && prev.z>=0 && prev.z<=prev.w) {
            vec2 hp=(prev.xy/prev.w*.5+.5)*vec2(size);
            if(all(greaterThanEqual(hp,vec2(.5)))&&all(lessThanEqual(hp,vec2(size)-.5))) {
                vec4 h=historyCubic(hp,rcp);
                hist=toYCoCg(h.rgb);nHist=h.a*16;
                vec3 center=.5*(lo+hi),extent=max(.5*(hi-lo),vec3(1e-6));
                vec3 delta=hist-center,ratio=abs(delta/extent);
                float maximum=max(ratio.x,max(ratio.y,ratio.z));
                if(maximum>1)hist=center+delta/maximum;
                nHist*=clamp(1.5-.5*maximum,0.0,1.0);
                speed=distance(hp,vec2(p)+.5);
                vec2 f=fract(hp-.5);subpixel=4*(f.x*(1-f.x)+f.y*(1-f.y));
            }
        }
    }
    float nMax=max(mix(pc.tune.x,pc.tune.y,clamp(speed/6.0,0.0,1.0))/(1+pc.tune.z*subpixel),2.0);
    float weight=min(nHist+1,nMax),blend=clamp((weight-1)*.5,0.0,1.0);
    vec3 accumulated=mix(hist,cur,1.0/weight);
    vec3 shown=blend>0?clamp(toRgb(mix(cur,accumulated,blend)),0.0,1.0):raw;
    result=vec4(shown,weight/16.0);
}
