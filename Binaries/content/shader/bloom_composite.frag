uniform sampler2D tBloom;
uniform sampler2D tScene;
uniform float     uBloomStrength;

varying vec2 vTexCoord;

void main()
{
    vec3 scene = texture2D(tScene, vTexCoord).rgb;
    vec3 bloom = texture2D(tBloom, vTexCoord).rgb;
    gl_FragColor = vec4(scene + bloom * uBloomStrength, 1.0);
}
