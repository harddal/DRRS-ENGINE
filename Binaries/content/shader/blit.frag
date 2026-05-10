uniform sampler2D tScene;

varying vec2 vTexCoord;

void main()
{
    gl_FragColor = texture2D(tScene, vTexCoord);
}
