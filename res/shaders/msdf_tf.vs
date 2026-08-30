// agent: grok-4.6 | 2026-08-30 | MSDF TF compact to mat4 | c76b62
layout(location = 0) in vec2 a_pos;
layout(location = 1) in vec2 a_size;

out vec4 out_col0;
out vec4 out_col1;
out vec4 out_col2;
out vec4 out_col3;

void main() {
  mat4 model = mat4(vec4(a_size.x, 0.0, 0.0, 0.0), vec4(0.0, a_size.y, 0.0, 0.0),
                    vec4(0.0, 0.0, 1.0, 0.0), vec4(a_pos, 0.0, 1.0));
  out_col0 = model[0];
  out_col1 = model[1];
  out_col2 = model[2];
  out_col3 = model[3];
  gl_Position = vec4(0.0);
}
// agent: grok-4.6 | 2026-08-30 | MSDF TF compact to mat4 | c76b62
