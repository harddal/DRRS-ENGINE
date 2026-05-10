uniform sampler2D tScene;
uniform vec2      uRcpFrame;
uniform float     uStrength;

varying vec2 vTexCoord;

void main()
{
    vec3 c  = texture2D(tScene, vTexCoord                                    ).rgb;
    vec3 n  = texture2D(tScene, vTexCoord + vec2( 0.0,            uRcpFrame.y)).rgb;
    vec3 s  = texture2D(tScene, vTexCoord + vec2( 0.0,           -uRcpFrame.y)).rgb;
    vec3 e  = texture2D(tScene, vTexCoord + vec2( uRcpFrame.x,    0.0        )).rgb;
    vec3 w  = texture2D(tScene, vTexCoord + vec2(-uRcpFrame.x,    0.0        )).rgb;

    // Unsharp mask: push centre away from its 4-neighbour average
    vec3 sharp = c + (c - (n + s + e + w) * 0.25) * uStrength;

    gl_FragColor = vec4(clamp(sharp, 0.0, 1.0), 1.0);
}
