#version 330 compatibility
// Depth-aware 3x3 blur for the half-res SSAO buffer. Depth weighting prevents
// AO bleeding across silhouette edges.

uniform sampler2D tAO;        // unit 0: raw AO (half res)
uniform sampler2D tPrepass;   // unit 1: full-res prepass (depth in .a)
uniform vec2      uRcpFrame;  // 1 / AO buffer size

varying vec2 vTexCoord;

void main()
{
    float centerDepth = texture2D(tPrepass, vTexCoord).a;
    if (centerDepth <= 0.001)
    {
        gl_FragColor = vec4(1.0);
        return;
    }

    float sum  = 0.0;
    float wsum = 0.0;
    for (int y = -1; y <= 1; ++y)
        for (int x = -1; x <= 1; ++x)
        {
            vec2  uv = vTexCoord + vec2(x, y) * uRcpFrame;
            float d  = texture2D(tPrepass, uv).a;
            // Weight falls off with view-depth difference — relative threshold
            // so far geometry doesn't over-reject.
            float w  = exp(-abs(d - centerDepth) / max(centerDepth * 0.05, 0.1));
            sum  += texture2D(tAO, uv).r * w;
            wsum += w;
        }

    gl_FragColor = vec4(vec3(sum / max(wsum, 0.001)), 1.0);
}
