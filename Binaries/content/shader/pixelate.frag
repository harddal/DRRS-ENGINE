uniform sampler2D tScene;
uniform float uPixelSize;
uniform vec2 uRcpFrame;

varying vec2 vTexCoord;

void main()
{
    vec2 step    = uPixelSize * uRcpFrame;
    vec2 blockUV = (floor(vTexCoord / step) + 0.5) * step;
    gl_FragColor = texture2D(tScene, blockUV);
}
