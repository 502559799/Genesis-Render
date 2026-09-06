vec2 envDirToFaceUV(vec3 v, out int face) {
    vec3 ad = abs(v);
    vec2 uv;
    if (ad.x >= ad.y && ad.x >= ad.z) {
        if (v.x > 0.0) { face = 0; uv = vec2(-v.z, v.y) / ad.x; }
        else           { face = 1; uv = vec2( v.z, v.y) / ad.x; }
    } else if (ad.y >= ad.z) {
        if (v.y > 0.0) { face = 2; uv = vec2( v.x,-v.z) / ad.y; }
        else           { face = 3; uv = vec2( v.x, v.z) / ad.y; }
    } else {
        if (v.z > 0.0) { face = 4; uv = vec2( v.x, v.y) / ad.z; }
        else           { face = 5; uv = vec2(-v.x, v.y) / ad.z; }
    }
    return uv * 0.5 + 0.5;
}
