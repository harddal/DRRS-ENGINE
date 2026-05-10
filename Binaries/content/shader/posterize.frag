// Posterize — quantizes colors into discrete steps, simulating a limited palette.
// Gives the banded color look of 8-bit rendering typical of Unreal 1997.

uniform sampler2D tScene;
uniform float uLevels;   // color steps per channel (e.g. 24.0 ~ 4-bit per channel feel)
uniform float uStrength; // blend 0.0=off .. 1.0=full banding

varying vec2 vTexCoord;

void main()
{
    vec3 c      = texture2D(tScene, vTexCoord).rgb;
    vec3 banded = floor(c * uLevels + 0.5) / uLevels;
    gl_FragColor = vec4(mix(c, banded, uStrength), 1.0);
}
