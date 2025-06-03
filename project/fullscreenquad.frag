#version 330

out vec4 color;

uniform sampler2D colorTexture;

void main()
{
    color = texture(colorTexture, gl_FragCoord.xy / textureSize(colorTexture, 0));
}