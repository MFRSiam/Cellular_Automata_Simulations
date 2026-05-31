#version 330

// ===========================================================================
// heat.fs - hot-air distortion pass (fire & lava).
//
// Adds a rising shimmer to hot/bright-warm pixels, like air distorting above
// a flame. Like water.fs it identifies its target by colour (warm + bright),
// so no extra data is needed from the simulation.
// ===========================================================================

in vec2 fragTexCoord;
in vec4 fragColor;
out vec4 finalColor;

uniform sampler2D texture0;
uniform vec2 resolution;
uniform float time;

void main() {
    vec2 uv = fragTexCoord;
    vec3 c = texture(texture0, uv).rgb;

    // Heat mask: warm (red > blue) AND bright.
    float warm = clamp((c.r - c.b), 0.0, 1.0);
    float heat = warm * max(c.r, c.g);

    // Vertical travelling wave -> horizontal wobble that rises over time.
    float wave = sin(uv.y * resolution.y * 0.10 + time * 6.0);
    vec2 offset = vec2(wave * heat * 2.0 / resolution.x, 0.0);

    vec3 distorted = texture(texture0, uv + offset).rgb;
    c = mix(c, distorted, clamp(heat * 0.6, 0.0, 1.0));

    finalColor = vec4(c, 1.0);
}
