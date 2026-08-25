#version 450

// Depth-only pass; no color output. Kept as an explicit empty stage rather
// than a null fragment shader stage (GraphicsPipeline::Invalidate currently
// always compiles a vertex+fragment pair).
void main()
{
}
