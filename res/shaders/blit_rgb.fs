// agent: grok-4.6 | 2026-08-21 | blit rgb shader A=1 | f71029
/* Copy RGB; drop packed metal/rough in A so debug isn't fake-lit. */
in vec2 fragTexCoord;
uniform sampler2D texture0;
out vec4 finalColor;

void main() {
  finalColor = vec4(texture(texture0, fragTexCoord).rgb, 1.0);
}
// agent: grok-4.6 | 2026-08-21 | blit rgb shader A=1 | f71029
