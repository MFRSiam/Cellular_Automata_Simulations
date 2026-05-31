#version 330

// ===========================================================================
// bloom.fs - final post-processing pass.
//
// Takes the fully composited scene and makes bright pixels (lava, fire, water
// foam, glowing edges) bleed light into their neighbours, then darkens the
// corners with a vignette for depth. This is the last shader in the chain.
// ===========================================================================

// Inputs coming from raylib's default vertex shader.
in vec2 fragTexCoord;   // 0..1 texture coordinate of this pixel
in vec4 fragColor;      // vertex tint (unused here)

out vec4 finalColor;    // the pixel we output

// Set by raylib automatically (texture0) and by us each frame (resolution).
uniform sampler2D texture0;
uniform vec2 resolution;

void main() {
    vec3 base = texture(texture0, fragTexCoord).rgb;

    // Sample a small 7x7 neighbourhood. Each sample only contributes if it is
    // bright (smoothstep gates out anything below ~0.55 luminance), so only
    // emissive things glow.
    vec3 bloom = vec3(0.0);
    for (int x = -3; x <= 3; x++) {
        for (int y = -3; y <= 3; y++) {
            vec2 off = vec2(float(x), float(y)) * 1.5 / resolution;
            vec3 s = texture(texture0, fragTexCoord + off).rgb;
            float brightness = max(s.r, max(s.g, s.b));
            // Threshold sits just above the cap used for non-emissive cells
            // (195/255), so only fire / lava / gold actually bloom.
            bloom += s * smoothstep(0.78, 1.0, brightness);
        }
    }
    bloom /= 49.0; // average over the kernel

    vec3 col = base + bloom * 1.4; // add the glow on top of the base image

    // Vignette: fade toward the edges of the screen.
    vec2 uv = fragTexCoord - 0.5;
    col *= mix(0.80, 1.0, smoothstep(0.95, 0.25, length(uv)));

    finalColor = vec4(col, 1.0);
}
