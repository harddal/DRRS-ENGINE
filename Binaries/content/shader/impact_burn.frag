// Plasma burn impact fragment shader
varying vec2 vTexCoord;
varying float vLinearDepth;

uniform sampler2D texture1;      // Scorch texture
uniform float uBurnTime;         // Time since impact (0-1, where 1 = fully cooled)

void main()
{
	
	// Sample the scorch texture
	vec4 texColor = texture2D(texture1, vTexCoord);
	
	// Create burn effect that transitions from hot to cool
	// uBurnTime goes from 0 (just spawned) to 1 (fully cooled)
	
	// Hot phase (0.0 - 0.3): Bright orange/yellow glow
	// Cooling phase (0.3 - 0.7): Orange to red to dark
	// Cold phase (0.7 - 1.0): Dark scorch mark, fading out
	
	vec3 hotColor = vec3(1.0, 0.8, 0.3);      // Bright yellow-orange
	vec3 warmColor = vec3(1.0, 0.4, 0.1);     // Deep orange
	vec3 coolColor = vec3(0.6, 0.1, 0.05);    // Dark red
	vec3 coldColor = vec3(0.2, 0.2, 0.2);     // Dark gray scorch
	
	vec3 burnColor;
	float glowIntensity;
	
	if (uBurnTime < 0.3)
	{
		// Hot phase - bright glow
		float t = uBurnTime / 0.3;
		burnColor = mix(hotColor, warmColor, t);
		glowIntensity = 1.5 - (t * 0.5); // Start very bright, dim slightly
	}
	else if (uBurnTime < 0.7)
	{
		// Cooling phase - transition to dark
		float t = (uBurnTime - 0.3) / 0.4;
		burnColor = mix(warmColor, coolColor, t);
		glowIntensity = 1.0 - (t * 0.8); // Dim significantly
	}
	else
	{
		// Cold phase - scorch mark fading out
		float t = (uBurnTime - 0.7) / 0.3;
		burnColor = mix(coolColor, coldColor, t);
		glowIntensity = 0.2 - (t * 0.2); // Very dim, fading to nothing
	}
	
	// Apply burn color intensity
	vec3 finalColor = burnColor * glowIntensity;
	
	// Use texture alpha for shape, but modulate it with burn time
	float alpha = texColor.a * (1.0 - uBurnTime);
	
	// Add extra glow in the center during hot phase
	if (uBurnTime < 0.5)
	{
		// Calculate distance from center
		vec2 centered = vTexCoord - vec2(0.5, 0.5);
		float dist = length(centered);
		float centerGlow = (1.0 - dist * 2.0) * (1.0 - uBurnTime * 2.0);
		centerGlow = max(0.0, centerGlow);
		
		finalColor += hotColor * centerGlow * 0.5;
	}
	
	gl_FragColor = vec4(finalColor, alpha);
}
