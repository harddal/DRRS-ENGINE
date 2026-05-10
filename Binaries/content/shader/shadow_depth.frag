// Shadow map depth pass fragment shader.
// Writes gl_FragCoord.z (NDC depth in [0,1]) to the R channel.
// For an orthographic projection this is linear, which is what we want.
void main()
{
    gl_FragColor = vec4(gl_FragCoord.z, 0.0, 0.0, 1.0);
}
