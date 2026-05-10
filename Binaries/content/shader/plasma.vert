// Plasma Ball Vertex Shader
// Using built-in matrices since custom uniforms aren't passed

varying vec3 vNormal;
varying vec3 vPosition;
varying vec2 vTexCoord;
varying vec3 vViewDir;
varying float vDepth;  // Linear depth for manual depth testing

uniform float CamFar;

void main()
{
    // Transform to view space for depth (using built-in matrix)
    vec4 viewPos = gl_ModelViewMatrix * gl_Vertex;
    vDepth = viewPos.z / CamFar;  // Normalized linear depth
    
    // Transform to clip space (using built-in matrix)
    gl_Position = gl_ModelViewProjectionMatrix * gl_Vertex;
    
    // Pass data to fragment shader
    vNormal = normalize(gl_NormalMatrix * gl_Normal);
    vPosition = gl_Vertex.xyz;
    vTexCoord = gl_MultiTexCoord0.xy;
    vViewDir = -normalize(viewPos.xyz);
}
