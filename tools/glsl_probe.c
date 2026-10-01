/* fatgl dev tool: GL 3.3 GLSL -> SPIR-V (glslang, relaxed Vulkan rules: loose
 * uniforms in a default uniform block) -> fatmap; prints what is rejected */
#include <fatmap/fatmap.h>
#include <glslang/Include/glslang_c_interface.h>
#include <glslang/Public/resource_limits_c.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const char* vs_src = "#version 330 core\n"
                            "layout(location = 0) in vec3 aPos;\n"
                            "in vec2 aUv;\n"
                            "uniform mat4 uMvp;\n"
                            "uniform float uTime;\n"
                            "out vec2 vUv;\n"
                            "out float vShade;\n"
                            "float wave(float x) { return sin(x * 3.0 + uTime) * 0.5 + 0.5; }\n"
                            "void main() {\n"
                            "    vUv = aUv;\n"
                            "    vShade = wave(aPos.x);\n"
                            "    gl_Position = uMvp * vec4(aPos, 1.0);\n"
                            "}\n";
static const char* fs_src = "#version 330 core\n"
                            "in vec2 vUv;\n"
                            "in float vShade;\n"
                            "uniform sampler2D uTex;\n"
                            "uniform vec4 uTint;\n"
                            "layout(std140) uniform Lights { vec4 dir; vec4 color; } lights;\n"
                            "out vec4 fragColor;\n"
                            "vec3 shade(vec3 c) { return c * vShade * lights.color.rgb; }\n"
                            "void main() {\n"
                            "    vec4 t = texture(uTex, vUv);\n"
                            "    fragColor = vec4(shade(t.rgb * uTint.rgb), t.a);\n"
                            "}\n";

static glslang_shader_t* compile(glslang_stage_t stage, const char* src)
{
    glslang_input_t in;
    memset(&in, 0, sizeof(in));
    in.language                          = GLSLANG_SOURCE_GLSL;
    in.stage                             = stage;
    in.client                            = GLSLANG_CLIENT_VULKAN;
    in.client_version                    = GLSLANG_TARGET_VULKAN_1_0;
    in.target_language                   = GLSLANG_TARGET_SPV;
    in.target_language_version           = GLSLANG_TARGET_SPV_1_0;
    in.code                              = src;
    in.default_version                   = 330;
    in.default_profile                   = GLSLANG_CORE_PROFILE;
    in.force_default_version_and_profile = 0;
    in.forward_compatible                = 0;
    in.messages                          = GLSLANG_MSG_DEFAULT_BIT;
    in.resource                          = glslang_default_resource();
    glslang_shader_t* sh = glslang_shader_create(&in);
    glslang_shader_set_options(sh, GLSLANG_SHADER_AUTO_MAP_BINDINGS | GLSLANG_SHADER_AUTO_MAP_LOCATIONS |
                                       GLSLANG_SHADER_VULKAN_RULES_RELAXED);
    glslang_shader_set_default_uniform_block_name(sh, "gl_DefaultUniformBlock");
    glslang_shader_set_default_uniform_block_set_and_binding(sh, 0, 0);
    if (!glslang_shader_preprocess(sh, &in) || !glslang_shader_parse(sh, &in)) {
        printf("%s shader:\n%s\n", stage == GLSLANG_STAGE_VERTEX ? "vertex" : "fragment", glslang_shader_get_info_log(sh));
        return NULL;
    }
    return sh;
}

int main(void)
{
    glslang_initialize_process();
    glslang_shader_t* vs = compile(GLSLANG_STAGE_VERTEX, vs_src);
    glslang_shader_t* fs = compile(GLSLANG_STAGE_FRAGMENT, fs_src);
    if (!vs || !fs) return 1;
    glslang_program_t* prog = glslang_program_create();
    glslang_program_add_shader(prog, vs);
    glslang_program_add_shader(prog, fs);
    if (!glslang_program_link(prog, GLSLANG_MSG_SPV_RULES_BIT | GLSLANG_MSG_VULKAN_RULES_BIT) || !glslang_program_map_io(prog)) {
        printf("link:\n%s\n", glslang_program_get_info_log(prog));
        return 1;
    }
    uint32_t* words[2];
    size_t    n[2];
    for (int i = 0; i < 2; i++) {
        glslang_stage_t st = i ? GLSLANG_STAGE_FRAGMENT : GLSLANG_STAGE_VERTEX;
        glslang_program_SPIRV_generate(prog, st);
        n[i]     = glslang_program_SPIRV_get_size(prog);
        words[i] = (uint32_t*)malloc(n[i] * 4);
        glslang_program_SPIRV_get(prog, words[i]);
        printf("%s: %zu words\n", i ? "fs" : "vs", n[i]);
        char path[32];
        snprintf(path, sizeof(path), "probe_%s.spv", i ? "frag" : "vert");
        FILE* f = fopen(path, "wb");
        fwrite(words[i], 4, n[i], f);
        fclose(f);
    }
    fm3d_vertex_attrib attr[2] = { { 0, 3, 0 }, { 1, 2, 12 } };
    char               err[256];
    fm3d_spirv*        p = fm3d_spirv_create(words[0], n[0], words[1], n[1], attr, 2, err, sizeof(err));
    printf("fatmap: %s\n", p ? "accepted" : err);
    return 0;
}
