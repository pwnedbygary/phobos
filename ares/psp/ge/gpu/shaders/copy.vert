//The vertex shader of the hardware renderer's copies (docs/psp-gpu-renderers.md, "Upscaling"): one triangle over
//the whole viewport, which the backend sets to the rectangle copied into, for copy.frag (memory's pixels put in a
//target drawn at a higher resolution) and present.frag (a picture on the screen).

void main() {
  vec2 corner = vec2((gl_VertexIndex << 1) & 2, gl_VertexIndex & 2);  //(0, 0), (2, 0), (0, 2)
  gl_Position = vec4(corner * 2.0 - 1.0, 0.0, 1.0);
}
