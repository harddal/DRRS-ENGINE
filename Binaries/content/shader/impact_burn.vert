// Plasma burn impact vertex shader
varying vec2 vTexCoord;
varying float vLinearDepth;

uniform float CamFar;

void main()
{
	gl_Position = gl_ModelViewProjectionMatrix * gl_Vertex;
	vTexCoord = gl_MultiTexCoord0.xy;
	
	// Calculate linear depth for depth testing (same as plasma shader)
	vec4 viewPos = gl_ModelViewMatrix * gl_Vertex;
	vLinearDepth = viewPos.z / CamFar;
}
