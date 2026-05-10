// Quake-esque water vertex shader.
// World-space XZ position is passed to the fragment shader so UV tiling
// is independent of the node's scale — 64 world-units always map to one tile.

uniform mat4 mWorld;  // object → world transform (set by WaterShaderCallback)

varying vec2 vWorldUV;

void main()
{
    gl_Position = gl_ModelViewProjectionMatrix * gl_Vertex;

    // Transform vertex to world space.  We use XZ as the 2-D tile coordinate
    // so horizontal water surfaces tile naturally and vertical faces also tile
    // in a consistent world-aligned grid.
    vec4 worldPos = mWorld * gl_Vertex;
    vWorldUV = worldPos.xz;
}
