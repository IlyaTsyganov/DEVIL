#version 450
layout(location=0) in vec4 vertexColor;
layout(location=1) in float viewDistance;
layout(location=0) out vec4 outColor;
layout(set=0,binding=0) uniform sampler2D screenDepth;
layout(push_constant) uniform Camera { vec4 row0; vec4 row1; vec4 row2; vec4 row3; vec4 options; vec4 depthParams; vec4 uvParams; vec4 debug; } camera;
void main() {
    if(camera.debug.x>0.5) {
        uint bits=floatBitsToUint(texture(screenDepth,gl_FragCoord.xy*camera.uvParams.xy+camera.uvParams.zw).r);
        outColor=vec4(uvec4(bits,bits>>8,bits>>16,bits>>24)&255u)/255.0;
        return;
    }
    if(camera.options.w>0.5) {
        vec2 uv=gl_FragCoord.xy*camera.uvParams.xy+camera.uvParams.zw;
        float sampled=texture(screenDepth,uv).r;
        float ndc=(sampled-camera.depthParams.x)/(camera.depthParams.y-camera.depthParams.x);
        if(camera.depthParams.z==1.0) ndc=ndc*2.0-1.0;
        if(camera.depthParams.z==2.0) ndc=1.0-ndc;
        float distanceToSurface=camera.options.z/(ndc-camera.options.y);
        float tolerance=camera.depthParams.w+max(distanceToSurface,0.0)*0.001;
        if(distanceToSurface>0.0&&viewDistance>distanceToSurface+tolerance) discard;
    }
    outColor=vertexColor;
}
