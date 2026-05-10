// Barrel Heat Vertex Shader

varying vec3 vViewPos;
varying vec3 vViewNormal;
varying vec2 vTexCoord;
varying vec2 vLightmapUV;
varying float vLocalZ;   // object-space Z passed to frag for barrel length gradient

void main()
{
    gl_Position = gl_ModelViewProjectionMatrix * gl_Vertex;

    vViewPos    = (gl_ModelViewMatrix * gl_Vertex).xyz;
    vViewNormal = normalize(gl_NormalMatrix * gl_Normal);

    vTexCoord   = gl_MultiTexCoord0.xy;
    vLightmapUV = gl_MultiTexCoord1.xy;

    // Z is unchanged by the barrel's spin (rotation around Z), so this
    // always encodes the correct "how far along the barrel" position.
    vLocalZ = gl_Vertex.z;
}
