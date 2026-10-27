#pragma once

struct InspectorHit { const Mesh* mesh=nullptr; float distance=FLT_MAX; };
static Vec3 subtract(Vec3 a,Vec3 b) { return {a.x-b.x,a.y-b.y,a.z-b.z}; }
static Vec3 cross(Vec3 a,Vec3 b) { return {a.y*b.z-a.z*b.y,a.z*b.x-a.x*b.z,a.x*b.y-a.y*b.x}; }
static float dot(Vec3 a,Vec3 b) { return a.x*b.x+a.y*b.y+a.z*b.z; }
static bool inspectorRay(const GameCamera& camera,Vec3& origin,Vec3& direction) {
    origin={camera.position[0],camera.position[1],camera.position[2]};
    // Intersection of the centre-screen clip X/Y planes also handles an
    // asymmetric projection; select its sign using the forward/W plane.
    direction=cross({camera.matrix[0],camera.matrix[1],camera.matrix[2]},
                    {camera.matrix[4],camera.matrix[5],camera.matrix[6]});
    float length=sqrtf(dot(direction,direction));
    if(!std::isfinite(length)||length<1e-6f) return false;
    float sign=dot(direction,{camera.matrix[12],camera.matrix[13],camera.matrix[14]})<0?-1.f:1.f;
    direction={direction.x*sign/length,direction.y*sign/length,direction.z*sign/length};
    return true;
}
static bool rayTriangle(Vec3 origin,Vec3 direction,Vec3 a,Vec3 b,Vec3 c,float& distance) {
    Vec3 e1=subtract(b,a),e2=subtract(c,a),p=cross(direction,e2);
    double determinant=dot(e1,p);
    if(fabs(determinant)<1e-9) return false; // Double-sided: works from inside volumes.
    Vec3 t=subtract(origin,a); double u=dot(t,p)/determinant;
    if(u<-1e-6||u>1.000001) return false;
    Vec3 q=cross(t,e1); double v=dot(direction,q)/determinant;
    if(v<-1e-6||u+v>1.000001) return false;
    double hit=dot(e2,q)/determinant;
    if(!std::isfinite(hit)||hit<.001||hit>=distance) return false;
    distance=static_cast<float>(hit); return true;
}
static bool rayBounds(Vec3 origin,Vec3 direction,const float* bounds,float limit) {
    float nearDistance=0,farDistance=limit;
    const float o[]{origin.x,origin.y,origin.z},d[]{direction.x,direction.y,direction.z};
    for(int axis=0;axis<3;++axis) {
        if(fabsf(d[axis])<1e-8f) { if(o[axis]<bounds[axis]||o[axis]>bounds[axis+3]) return false; continue; }
        float a=(bounds[axis]-o[axis])/d[axis],b=(bounds[axis+3]-o[axis])/d[axis];
        if(a>b) std::swap(a,b);
        nearDistance=std::max(nearDistance,a); farDistance=std::min(farDistance,b);
        if(nearDistance>farDistance) return false;
    }
    return true;
}
static InspectorHit pickInspectorVolume(const GameCamera& camera,const std::vector<Candidate>& visible,float limit) {
    InspectorHit result; result.distance=limit; Vec3 origin{},direction{};
    if(!inspectorRay(camera,origin,direction)) return result;
    for(const Candidate& candidate:visible) {
        const Mesh& mesh=*candidate.mesh;
        if(!mesh.entity||!mesh.geometry||!rayBounds(origin,direction,mesh.bounds,result.distance)) continue;
        const auto& points=mesh.geometry->triangles;
        for(size_t i=0;i+2<points.size();i+=3)
            if(rayTriangle(origin,direction,mesh.world(points[i]),mesh.world(points[i+1]),mesh.world(points[i+2]),result.distance)) result.mesh=&mesh;
    }
    return result;
}
