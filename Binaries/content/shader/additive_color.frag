uniform sampler2D tDiffuse;
uniform vec4      uColor;

void main()
{
    vec4 tex    = texture2D(tDiffuse, gl_TexCoord[0].xy);
    gl_FragColor = tex * uColor;
}
