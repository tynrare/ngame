// agent: grok-4.6 | 2026-08-30 | MSDF FS instance color outline | a20a0a
in vec2 fragTexCoord;
in vec3 v_color;
in float v_outline;

uniform sampler2D texture0;

out vec4 finalColor;

float median(float r, float g, float b) {
  return max(min(r, g), min(max(r, g), b));
}

void main() {
  vec3 s = texture(texture0, fragTexCoord).rgb;
  float d = median(s.r, s.g, s.b);
  float w = fwidth(d);
  float fill = smoothstep(0.5 - w, 0.5 + w, d);
  float alpha = fill;
  vec3 rgb = v_color;
  if (v_outline > 0.0) {
    float out_a = smoothstep(0.5 - v_outline - w, 0.5 - v_outline + w, d);
    rgb = mix(vec3(0.0), v_color, fill);
    alpha = max(out_a, fill);
  }
  finalColor = vec4(rgb, alpha);
}
// agent: grok-4.6 | 2026-08-30 | MSDF FS instance color outline | a20a0a
