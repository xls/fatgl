#version 330
// Doom 3 BFG Edition's interaction fragment program (renderprogs/glsl/interaction_fragment.glsl,
// GPL, id Software) as plain GLSL 3.30: bump, light falloff and projection, YCoCg diffuse, specular.
uniform vec4 _fa_[2];
uniform sampler2D samp0;
uniform sampler2D samp1;
uniform sampler2D samp2;
uniform sampler2D samp3;
uniform sampler2D samp4;

in vec4 vofi_TexCoord0;
in vec4 vofi_TexCoord1;
in vec4 vofi_TexCoord2;
in vec4 vofi_TexCoord3;
in vec4 vofi_TexCoord4;
in vec4 vofi_TexCoord5;
in vec4 vofi_TexCoord6;
in vec4 vofi_Color;

out vec4 fragColor;

float dot3(vec3 a, vec3 b) { return dot(a, b); }
float dot4(vec4 a, vec4 b) { return dot(a, b); }
const vec4 matrixCoCg1YtoRGB1X = vec4(1.0, -1.0, 0.0, 1.0);
const vec4 matrixCoCg1YtoRGB1Y = vec4(0.0, 1.0, -0.50196078, 1.0);
const vec4 matrixCoCg1YtoRGB1Z = vec4(-1.0, -1.0, 1.00392156, 1.0);
vec3 ConvertYCoCgToRGB(vec4 YCoCg) {
    vec3 rgbColor;
    YCoCg.z = (YCoCg.z * 31.875) + 1.0;
    YCoCg.z = 1.0 / YCoCg.z;
    YCoCg.xy *= YCoCg.z;
    rgbColor.x = dot4(YCoCg, matrixCoCg1YtoRGB1X);
    rgbColor.y = dot4(YCoCg, matrixCoCg1YtoRGB1Y);
    rgbColor.z = dot4(YCoCg, matrixCoCg1YtoRGB1Z);
    return rgbColor;
}

void main() {
    vec4 bumpMap = texture(samp0, vofi_TexCoord1.xy);
    vec4 lightFalloff = textureProj(samp1, vofi_TexCoord2.xyw);
    vec4 lightProj = textureProj(samp2, vofi_TexCoord3.xyw);
    vec4 YCoCG = texture(samp3, vofi_TexCoord4.xy);
    vec4 specMap = texture(samp4, vofi_TexCoord5.xy);
    vec3 lightVector = normalize(vofi_TexCoord0.xyz);
    vec3 diffuseMap = ConvertYCoCgToRGB(YCoCG);
    vec3 localNormal;
    localNormal.xy = bumpMap.wy - 0.5;
    localNormal.z = sqrt(abs(dot(localNormal.xy, localNormal.xy) - 0.25));
    localNormal = normalize(localNormal);
    float specularPower = 10.0;
    float hDotN = dot3(normalize(vofi_TexCoord6.xyz), localNormal);
    vec3 specularContribution = vec3(pow(max(hDotN, 0.0), specularPower));
    vec3 diffuseColor = diffuseMap * _fa_[0].xyz;
    vec3 specularColor = specMap.xyz * specularContribution * _fa_[1].xyz;
    vec3 lightColor = dot3(lightVector, localNormal) * lightProj.xyz * lightFalloff.xyz;
    fragColor.xyz = (diffuseColor + specularColor) * lightColor * vofi_Color.xyz;
    fragColor.w = 1.0;
}
