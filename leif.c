#include "leif.h"
#include <glad/glad.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#define LF_TRACE(...) { printf("Leif: [TRACE]: "); printf(__VA_ARGS__); printf("\n"); } 
#ifdef LF_DEBUG
#define LF_DEBUG(...) { printf("Leif: [DEBUG]: "); printf(__VA_ARGS__); printf("\n"); } 
#else
#define LF_DEBUG(...) 
#endif // LF_DEBUG 
#define LF_INFO(...) { printf("Leif: [INFO]: "); printf(__VA_ARGS__); printf("\n"); } 
#define LF_WARN(...) { printf("Leif: [WARN]: "); printf(__VA_ARGS__); printf("\n"); } 
#define LF_ERROR(...) { printf("[LEIF ERROR]: "); printf(__VA_ARGS__); printf("\n"); } 

#ifdef _MSC_VER 
#define D_BREAK __debugbreak
#elif defined(__clang__) || defined(__GNUC__)
#define D_BREAK __builtin_trap
#else 
#define D_BREAK
#endif 

#ifdef _DEBUG 
#define LF_ASSERT(cond, ...) { if(cond) {} else { printf("[LEIF]: Assertion failed: '"); printf(__VA_ARGS__); printf("' in file '%s' on line %i.\n", __FILE__, __LINE__); D_BREAK(); }}
#else 
#define LF_ASSERT(cond, ...)
#endif // _DEBUG

// -- Struct Defines ---
typedef struct {
    uint32_t id;
} LfShader;

typedef struct {
    LfVec2f pos;
    LfVec4f color;
} Vertex;

typedef struct {
    LfShader shader;
    uint32_t vao, vbo;
    uint32_t vert_count, vert_cap;
    Vertex* verts;
    LfVec4f vert_pos[6];
} RenderState;

typedef struct {
    bool init;
    uint32_t dsp_w, dsp_h;
    RenderState render;
} LfState;

// --- Renderer ---
static uint32_t                 shader_create(GLenum type, const char* src);
static LfShader                 shader_prg_create(char* vert_src, char* frag_src);
static void                     shader_set_mat(LfShader prg, const char* name, LfMat4 mat); 
static void                     renderer_init();

// --- Linear Algebra - Math ---
static LfMat4                   mat_identity(); 
static LfMat4                   orth_mat(float left, float right, float bottom, float top);
static LfMat4                   translate_mat(LfMat4 mat, LfVec3f translation);
static LfMat4                   scale_mat(LfMat4 mat, LfVec3f scale);
static LfMat4                   mat_mul(LfMat4 m1, LfMat4 m2);
static LfVec4f                  vec4_mul_mat(LfVec4f vec, LfMat4 mat);

static void                     mat_set_row_val(LfMat4* mat, uint32_t row_index, uint32_t vec_index, float val);
static float                    mat_get_row_val(LfMat4 mat, uint32_t row_index, uint32_t vec_index);

static void                     mat_set_row(LfMat4* mat, uint32_t row_index, LfVec4f val);
static LfVec4f                  mat_get_row(LfMat4 mat, uint32_t row_index);

static LfVec4f                  vec4f_create(float x, float y, float z, float w);
static LfVec3f                  vec3f_create(float x, float y, float z);
static LfVec2f                  vec2f_create(float x, float y);

static LfVec4f                  vec4f_add(LfVec4f a, LfVec4f b);
static LfVec4f                  vec4f_mul_scaler(LfVec4f v, float k);
static LfVec4f                  vec4f_mul(LfVec4f v1, LfVec4f v2);


// --- Static Functions --- 
uint32_t shader_create(GLenum type, const char* src) {
    uint32_t shader = glCreateShader(type);
    glShaderSource(shader, 1, &src, NULL);
    glCompileShader(shader);

    int32_t compiled; 
    glGetShaderiv(shader, GL_COMPILE_STATUS, &compiled);

    if(!compiled) {
        LF_ERROR("Failed to compile %s shader.", type == GL_VERTEX_SHADER ? "vertex" : "fragment");
        char info[512];
        glGetShaderInfoLog(shader, 512, NULL, info);
        LF_INFO("%s", info);
        glDeleteShader(shader);
    }
    return shader;
}

LfShader shader_prg_create(char* vert_src, char* frag_src) {
    uint32_t vertex_shader = shader_create(GL_VERTEX_SHADER, vert_src);
    uint32_t fragment_shader = shader_create(GL_FRAGMENT_SHADER, frag_src);

    LfShader prg;

    prg.id = glCreateProgram();
    glAttachShader(prg.id, vertex_shader);
    glAttachShader(prg.id, fragment_shader);
    glLinkProgram(prg.id);

    int32_t linked;
    glGetProgramiv(prg.id, GL_LINK_STATUS, &linked);

    if(!linked) {
        LF_ERROR("Failed to link shader program.");
        char info[512];
        glGetProgramInfoLog(prg.id, 512, NULL, info);
        LF_INFO("%s", info);
        glDeleteShader(vertex_shader);
        glDeleteShader(fragment_shader);
        glDeleteProgram(prg.id);
        return prg;
    }
    
    glDeleteShader(vertex_shader);
    glDeleteShader(fragment_shader);
    return prg;
}

static LfState state;

void renderer_init() {
    state.render.vert_cap = 10000;
    state.render.vert_count = 0;
    state.render.verts = malloc(sizeof(Vertex) * state.render.vert_cap);

    glCreateVertexArrays(1, &state.render.vao);
    glBindVertexArray(state.render.vao);
    
    glCreateBuffers(1, &state.render.vbo);
    glBindBuffer(GL_ARRAY_BUFFER, state.render.vbo);
    glBufferData(GL_ARRAY_BUFFER, sizeof(Vertex) * state.render.vert_cap, NULL, 
        GL_DYNAMIC_DRAW);
    
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, sizeof(Vertex), NULL);
    glEnableVertexAttribArray(0);
    
    glVertexAttribPointer(1, 4, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void*)(intptr_t*)(sizeof(float) * 2));
    glEnableVertexAttribArray(1);

    char* vert_src = 
        "#version 460 core\n"
        "layout (location = 0) in vec2 a_pos;\n"
        "layout (location = 1) in vec4 a_color;\n"
        "uniform mat4 u_proj;\n"
        "out vec4 v_color;\n"
        "void main() {\n"
            "v_color = a_color;\n"
            "gl_Position = u_proj * vec4(a_pos.x, a_pos.y, 0.0f, 1.0);\n"
        "}\n";

    char* frag_src = 
        "#version 460 core\n"
        "out vec4 o_color;\n"
        "in vec4 v_color;\n"
        "void main() {\n"
        "   o_color = v_color;\n"
        "}\n";

    state.render.shader = shader_prg_create(vert_src, frag_src);

    state.render.vert_pos[0] = vec4f_create(-0.5f, -0.5f, 0.0f, 1.0f);
    state.render.vert_pos[1] = vec4f_create(-0.5f,  0.5f, 0.0f, 1.0f);
    state.render.vert_pos[2] = vec4f_create( 0.5f,  0.5f, 0.0f, 1.0f);

    state.render.vert_pos[3] = vec4f_create(-0.5f, -0.5f, 0.0f, 1.0f);
    state.render.vert_pos[4] = vec4f_create(0.5f,  0.5f, 0.0f, 1.0f);
    state.render.vert_pos[5] = vec4f_create(0.5f,  -0.5f, 0.0f, 1.0f);
    

    glUseProgram(state.render.shader.id);
    LfMat4 proj = orth_mat(0.0f, (float)state.dsp_w, (float)state.dsp_h, 0.0f);
    shader_set_mat(state.render.shader, "u_proj", proj);
}

void shader_set_mat(LfShader prg, const char* name, LfMat4 mat) {
    glUniformMatrix4fv(glGetUniformLocation(prg.id, name), 1, GL_FALSE, (float*)&mat.values);
}
void lf_resize_display(uint32_t display_width, uint32_t display_height);

LfMat4 mat_identity() {
    LfMat4 mat;
    mat.values[0] = 1;
    mat.values[1] = 0;
    mat.values[2] = 0;
    mat.values[3] = 0;

    mat.values[4] = 0;
    mat.values[5] = 1;
    mat.values[6] = 0;
    mat.values[7] = 0;

    mat.values[8] = 0;
    mat.values[9] = 0;
    mat.values[10] = 1;
    mat.values[11] = 0;
    
    mat.values[12] = 0;
    mat.values[13] = 0;
    mat.values[14] = 0;
    mat.values[15] = 1;
    return mat;
}

LfMat4 translate_mat(LfMat4 mat, LfVec3f translation) {
    LfMat4 ret = mat;
    mat_set_row(&ret, 4, 
        vec4f_add(
            vec4f_add(
                vec4f_mul_scaler(mat_get_row(mat, 1), translation.values[0]),
                vec4f_mul_scaler(mat_get_row(mat, 2), translation.values[1])
            ),
            vec4f_add(
                vec4f_mul_scaler(mat_get_row(mat, 3), translation.values[2]), 
                mat_get_row(mat, 4))));
    return ret;
}

LfMat4 scale_mat(LfMat4 mat, LfVec3f scale) {
    LfMat4 ret = mat;
    mat_set_row(&ret, 1, vec4f_mul_scaler(mat_get_row(mat, 1), scale.values[0]));
    mat_set_row(&ret, 2, vec4f_mul_scaler(mat_get_row(mat, 2), scale.values[1]));
    mat_set_row(&ret, 3, vec4f_mul_scaler(mat_get_row(mat, 3), scale.values[2]));
    return ret;
}

LfMat4 mat_mul(LfMat4 m1, LfMat4 m2) {
    LfVec4f row_A1 = mat_get_row(m1, 1);
    LfVec4f row_A2 = mat_get_row(m1, 2);
    LfVec4f row_A3 = mat_get_row(m1, 3);
    LfVec4f row_A4 = mat_get_row(m1, 4);

    LfVec4f row_B1 = mat_get_row(m2, 1);
    LfVec4f row_B2 = mat_get_row(m2, 2);
    LfVec4f row_B3 = mat_get_row(m2, 3);
    LfVec4f row_B4 = mat_get_row(m2, 4);

    LfMat4 ret = mat_identity();

    mat_set_row(&ret, 1, vec4f_add(
        vec4f_add(
            vec4f_mul_scaler(row_A1, row_B1.values[0]),
            vec4f_mul_scaler(row_A2, row_B1.values[1])
        ), 
        vec4f_add(
            vec4f_mul_scaler(row_A3, row_B1.values[2]),
            vec4f_mul_scaler(row_A4, row_B1.values[3])
        ) 
    ));
    mat_set_row(&ret, 2, vec4f_add(
        vec4f_add(
            vec4f_mul_scaler(row_A1, row_B2.values[0]),
            vec4f_mul_scaler(row_A2, row_B2.values[1])
        ), 
        vec4f_add(
            vec4f_mul_scaler(row_A3, row_B2.values[2]),
            vec4f_mul_scaler(row_A4, row_B2.values[3])
        ) 
    ));
    mat_set_row(&ret, 3, vec4f_add(
        vec4f_add(
            vec4f_mul_scaler(row_A1, row_B3.values[0]),
            vec4f_mul_scaler(row_A2, row_B3.values[1])
        ), 
        vec4f_add(
            vec4f_mul_scaler(row_A3, row_B3.values[2]),
            vec4f_mul_scaler(row_A4, row_B3.values[3])
        ) 
    ));
    mat_set_row(&ret, 4, mat_get_row(m2, 4));
    return ret;
}


LfVec4f vec4_mul_mat(LfVec4f vec, LfMat4 mat) {
    LfVec4f mov_0 = vec4f_create(vec.values[0], vec.values[0], vec.values[0], vec.values[0]);
    LfVec4f mov_1 = vec4f_create(vec.values[1], vec.values[1], vec.values[1], vec.values[1]);
    LfVec4f mul_0 = vec4f_mul(mat_get_row(mat, 1), mov_0);
    LfVec4f mul_1 = vec4f_mul(mat_get_row(mat, 2), mov_1);
    LfVec4f add_0 = vec4f_add(mul_0, mul_1);

    LfVec4f mov_2 = vec4f_create(vec.values[2], vec.values[2], vec.values[2], vec.values[2]);
    LfVec4f mov_3 = vec4f_create(vec.values[3], vec.values[3], vec.values[3], vec.values[3]);
    LfVec4f mul_2 = vec4f_mul(mat_get_row(mat, 3), mov_2);
    LfVec4f mul_3 = vec4f_mul(mat_get_row(mat, 4), mov_3);
    LfVec4f add_1 = vec4f_add(mul_2, mul_3);
    LfVec4f add_2 = vec4f_add(add_0, add_1);
    return add_2;
}

LfMat4 orth_mat(float left, float right, float bottom, float top) {
    LfMat4 mat = mat_identity();
    mat_set_row_val(&mat, 1, 1, 2 / (right - left));
    mat_set_row_val(&mat, 2, 2, 2 / (top - bottom));
    mat_set_row_val(&mat, 3, 3, -1);
    mat_set_row_val(&mat, 4, 1, -(right + left) / (right - left));
    mat_set_row_val(&mat, 4, 2, -(top + bottom) / (top - bottom));
    return mat;
}

void mat_set_row_val(LfMat4* mat, uint32_t row_index, uint32_t vec_index, float val) {
    if((row_index > 4 || row_index < 1) || (vec_index > 4 || vec_index < 1)) return;
    row_index--;
    vec_index--;
    mat->values[row_index * 4 + vec_index] = val;
}

float mat_get_row_val(LfMat4 mat, uint32_t row_index, uint32_t vec_index) {
    if((row_index > 4 || row_index < 1) || (vec_index > 4 || vec_index < 1)) return 0;
    return mat.values[row_index * 4 + vec_index];
}

void mat_set_row(LfMat4* mat, uint32_t row_index, LfVec4f val) {
    if(row_index > 4 || row_index < 1) return;
    row_index--;
    mat->values[row_index * 4 + 0] = val.values[0];
    mat->values[row_index * 4 + 1] = val.values[1];
    mat->values[row_index * 4 + 2] = val.values[2];
    mat->values[row_index * 4 + 3] = val.values[3];
}
LfVec4f mat_get_row(LfMat4 mat, uint32_t row_index) {
    if(row_index > 4 || row_index < 1) return (LfVec4f){0};
    row_index--;
    LfVec4f val;
    val.values[0] = mat.values[row_index * 4 + 0];
    val.values[1] = mat.values[row_index * 4 + 1];
    val.values[2] = mat.values[row_index * 4 + 2];
    val.values[3] = mat.values[row_index * 4 + 3];
    return val;
    LfMat4 proj = orth_mat(0.0f, (float)state.dsp_w, (float)state.dsp_h, 0.0f);
}
LfVec4f vec4f_create(float x, float y, float z, float w) {
    LfVec4f vec;
    vec.values[0] = x;
    vec.values[1] = y;
    vec.values[2] = z;
    vec.values[3] = w;
    return vec;
}
LfVec3f vec3f_create(float x, float y, float z) {
    LfVec3f vec;
    vec.values[0] = x;
    vec.values[1] = y;
    vec.values[2] = z;
    return vec;
}
LfVec2f vec2f_create(float x, float y) {
    LfVec2f vec;
    vec.values[0] = x;
    vec.values[1] = y;
    return vec;
}

LfVec4f vec4f_add(LfVec4f a, LfVec4f b) {
    LfVec4f vec;
    vec.values[0] = a.values[0] + b.values[0];
    vec.values[1] = a.values[1] + b.values[1];
    vec.values[2] = a.values[2] + b.values[2];
    vec.values[3] = a.values[3] + b.values[3];
    return vec;
}

LfVec4f vec4f_mul_scaler(LfVec4f v, float k) {
    LfVec4f vec;
    vec.values[0] = v.values[0] * k;
    vec.values[1] = v.values[1] * k;
    vec.values[2] = v.values[2] * k;
    vec.values[3] = v.values[3] * k;
    return vec;
}

LfVec4f vec4f_mul(LfVec4f v1, LfVec4f v2) {
    return vec4f_create(v1.values[0] * v2.values[0], 
                        v1.values[1] * v2.values[1],
                        v1.values[2] * v2.values[2], 
                        v1.values[3] * v2.values[3]);
}


 
// --- Public API Functions ---
//
void lf_init(uint32_t display_width, uint32_t display_height) {
    state.init = true;
    state.dsp_w = display_width;
    state.dsp_h = display_height;

    renderer_init();
}

void lf_resize_display(uint32_t display_width, uint32_t display_height) {
    state.dsp_w = display_width;
    state.dsp_h = display_height;
    LfMat4 proj = orth_mat(0.0f, (float)state.dsp_w, (float)state.dsp_h, 0.0f);
    shader_set_mat(state.render.shader, "u_proj", proj);
}


void lf_draw_rect(LfVec2i pos, LfVec2i size, LfVec4f color) {
    LfMat4 transform = mat_mul(
        scale_mat(mat_identity(), vec3f_create(size.values[0], size.values[1], 0.0f)),
        translate_mat(mat_identity(), vec3f_create((float)pos.values[0], (float)pos.values[1], 0.0f))
    );
    for(uint32_t i = 0; i < 6; i++) {
        LfVec4f pos_mat = vec4_mul_mat(state.render.vert_pos[i], transform);
        state.render.verts[state.render.vert_count].pos = vec2f_create(pos_mat.values[0], 
            pos_mat.values[1]);
        state.render.verts[state.render.vert_count].color = color;
        state.render.vert_count++;
    } 
}

void lf_flush() {
    if(state.render.vert_count <= 0) return;

    glUseProgram(state.render.shader.id);
    glBindBuffer(GL_ARRAY_BUFFER, state.render.vbo);
    glBufferSubData(GL_ARRAY_BUFFER, 0, sizeof(Vertex) * state.render.vert_count, 
                    state.render.verts);

    glBindVertexArray(state.render.vao);
    glDrawArrays(GL_TRIANGLES, 0, state.render.vert_count);

    state.render.vert_count = 0;
}
