// Shadow map depth pass vertex shader.
// Relies on Irrlicht setting gl_ModelViewProjectionMatrix from the light's
// view/projection transforms — identical to the main camera path.
void main()
{
    gl_Position = gl_ModelViewProjectionMatrix * gl_Vertex;
}
