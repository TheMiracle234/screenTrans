#include <Server.h>
#include <Client.h>
#include <H264Decoder.h>
#include <AudioPlay.h>

#include <iostream>
#include <mutex>

#define println(x) std::cout<<x<<"\n"
#define printv(x) std::cout<<#x<<": "<<x<<"\n"
#define PERROR(x) std::cerr<<__FILE__<<"\nline "<<__LINE__<<": "<<x<<"\n"
#define PAUSE (void)getchar()

#include <thread>

using TM::Server, TM::Client, TM::Socket;
using ST::H264Decoder, ST::AudioPlay;

#include "imgui.h"
#include "imgui_impl_glfw.h"
#include "imgui_impl_opengl3.h"

#if defined(_MSC_VER) && (_MSC_VER >= 1900) && !defined(IMGUI_DISABLE_WIN32_FUNCTIONS)
#pragma comment(lib, "legacy_stdio_definitions")
#endif

#include <GL/glew.h>
#include <GLFW/glfw3.h>

///////////////////////////////////////////////////////////////
// Shader
///////////////////////////////////////////////////////////////

const char* vertexShaderSrc = R"(
#version 330 core

layout(location = 0) in vec2 aPos;
layout(location = 1) in vec2 aUV;

out vec2 UV;

void main()
{
    gl_Position = vec4(aPos, 0.0, 1.0);

    UV = aUV;
}
)";

const char* fragmentShaderSrc = R"(
#version 330 core

in vec2 UV;

out vec4 FragColor;

uniform sampler2D uTexture;

void main()
{
    FragColor = texture(uTexture, UV);
}
)";

GLFWwindow* window;
GLuint vao, vbo, ebo;
GLuint program;
GLuint texture;

///////////////////////////////////////////////////////////////

GLuint CompileShader(GLenum type, const char* src)
{
    GLuint shader = glCreateShader(type);

    glShaderSource(shader, 1, &src, nullptr);

    glCompileShader(shader);

    GLint success;

    glGetShaderiv(
        shader,
        GL_COMPILE_STATUS,
        &success
    );

    if (!success) {

        char log[1024];

        glGetShaderInfoLog(
            shader,
            1024,
            nullptr,
            log
        );

        std::cout << log << std::endl;
    }

    return shader;
}

///////////////////////////////////////////////////////////////

GLuint CreateProgram()
{
    GLuint vs =
        CompileShader(
            GL_VERTEX_SHADER,
            vertexShaderSrc
        );

    GLuint fs =
        CompileShader(
            GL_FRAGMENT_SHADER,
            fragmentShaderSrc
        );

    GLuint program = glCreateProgram();

    glAttachShader(program, vs);

    glAttachShader(program, fs);

    glLinkProgram(program);

    GLint success;

    glGetProgramiv(
        program,
        GL_LINK_STATUS,
        &success
    );

    if (!success) {

        char log[1024];

        glGetProgramInfoLog(
            program,
            1024,
            nullptr,
            log
        );

        std::cout << log << std::endl;
    }

    glDeleteShader(vs);

    glDeleteShader(fs);

    return program;
}

std::mutex mtx_close;
std::mutex mtx_video_frames;
std::queue<ST::DecodedFrame> total_video_frames;

void Receive(Client& client, H264Decoder& decoder, AudioPlay& ad_player) {
    for (;;) {
        {
			std::lock_guard lock(mtx_close);
            if (client.Closed())
                break;
        }
        
        auto frames = client.ReceiveVec<int16_t>();
        if (!frames)
            break;

        ad_player.PushFrames(*frames);

        auto pk_size = client.ReceiveParseTo<int>();

        if (!pk_size)
            break;

        // each packet
        for (int i = 0;i < *pk_size;++i) {
            auto pk = client.Receive();
            if (!pk)
                return;

            if (pk->size() == 0) {
                println("receive pk error");
            }
            auto frames = decoder.DecodePacket(*pk);
            for (auto& frame : frames) {
                std::lock_guard<std::mutex> lock(mtx_video_frames);
                total_video_frames.push(std::move(frame));
            }
        }

    }
}

void Draw(H264Decoder& decoder) {
    int last_width = -1;
    int last_height = -1;
    while (!glfwWindowShouldClose(window)) {
        ST::DecodedFrame frame;
        {
            std::lock_guard<std::mutex> lock(mtx_video_frames);
            if (total_video_frames.empty()) {
                continue;
            }

            frame = std::move(total_video_frames.front());
            total_video_frames.pop();
        }

        int width = frame.width;
        int height = frame.height;
        auto& data = frame.rgba;

        if (data.size() != width * height * 4) {
            continue;
        }
#if 1

        // Start the Dear ImGui frame
        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();


        glClearColor(0.1f, 0.1f, 0.1f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);

        glBindTexture(GL_TEXTURE_2D, texture);
        if (width != last_width || height != last_height) {
            last_width = width;
            last_height = height;
            glTexImage2D(
                GL_TEXTURE_2D,
                0,
                GL_RGBA8,
                width,
                height,
                0,
                GL_BGRA,
                GL_UNSIGNED_BYTE,
                data.data()
            );
        }
        else {
            glTexSubImage2D(
                GL_TEXTURE_2D,
                0,
                0,
                0,
                width,
                height,
                GL_BGRA,
                GL_UNSIGNED_BYTE,
                data.data()
            );
        }
        glUseProgram(program);
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, texture);
        glBindVertexArray(vao);
        glDrawElements(
            GL_TRIANGLES,
            6,
            GL_UNSIGNED_INT,
            nullptr
        );


        ImGui::Begin("FPS");
        ImGui::Text("FPS: %.3f", ImGui::GetIO().Framerate);
		ImGui::SliderFloat("delay_sec", &ST::AudioPlay::DELAY_SEC, 0.01f, 1.0f);
        //ImGui::SliderInt("fps", &fps, 1, 200);
        //ImGui::SliderInt("bit_rate", &bitrate, 1000000, 20000000);
        ImGui::End();

        // Rendering
        ImGui::Render();
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());

        glfwSwapBuffers(window);
        glfwPollEvents();


#endif
    }

}

int main() {
    Server server(Socket::Protocol::TCP, Socket::Ip::IPV4, 8080);
    server.Listen();

    println("waiting for new client...");
    auto client = server.Accept();
    if (!client) {
        PERROR("Failed to accept new client");
    }
    println("new client connected: " << client->id);


    ///////////////////////////////////////////////////////////
    // GLFW init
    ///////////////////////////////////////////////////////////

    if (!glfwInit()) {

        std::cout << "glfw init failed\n";

        return -1;
    }

    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);

    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);

    glfwWindowHint(
        GLFW_OPENGL_PROFILE,
        GLFW_OPENGL_CORE_PROFILE
    );

    float main_scale = ImGui_ImplGlfw_GetContentScaleForMonitor(glfwGetPrimaryMonitor()); // Valid on GLFW 3.3+ only


    window =
        glfwCreateWindow(
            (int)(1280 * main_scale),
            (int)(720 * main_scale),
            "Desktop Duplication",
            nullptr,
            nullptr
        );

    if (!window) {

        glfwTerminate();

        return -1;
    }

    glfwMakeContextCurrent(window);

    glfwSwapInterval(1);

    ///////////////////////////////////////////////////////////
    // GLEW
    ///////////////////////////////////////////////////////////

    glewExperimental = GL_TRUE;

    if (glewInit() != GLEW_OK) {

        std::cout << "glew init failed\n";

        return -1;
    }

    ///////////////////////////////////////////////////////////
    // viewport
    ///////////////////////////////////////////////////////////

    int winW, winH;

    glfwGetFramebufferSize(
        window,
        &winW,
        &winH
    );

    glfwSetWindowSizeCallback(window, [](GLFWwindow* window, int width, int height) {
        glViewport(0, 0, width, height);
        });

    glViewport(0, 0, winW, winH);

    ///////////////////////////////////////////////////////////
    // shader
    ///////////////////////////////////////////////////////////

    program = CreateProgram();

    ///////////////////////////////////////////////////////////
    // fullscreen quad
    ///////////////////////////////////////////////////////////

    float vertices[] = {

        // pos        // uv

        -1.0f,  1.0f, 0.0f, 0.0f,
         1.0f,  1.0f, 1.0f, 0.0f,
         1.0f, -1.0f, 1.0f, 1.0f,
        -1.0f, -1.0f, 0.0f, 1.0f
    };

    unsigned int indices[] = {
        0,1,2,
        2,3,0
    };


    glGenVertexArrays(1, &vao);

    glGenBuffers(1, &vbo);

    glGenBuffers(1, &ebo);

    ///////////////////////////////////////////////////////////

    glBindVertexArray(vao);

    ///////////////////////////////////////////////////////////

    glBindBuffer(GL_ARRAY_BUFFER, vbo);

    glBufferData(
        GL_ARRAY_BUFFER,
        sizeof(vertices),
        vertices,
        GL_STATIC_DRAW
    );

    ///////////////////////////////////////////////////////////

    glBindBuffer(
        GL_ELEMENT_ARRAY_BUFFER,
        ebo
    );

    glBufferData(
        GL_ELEMENT_ARRAY_BUFFER,
        sizeof(indices),
        indices,
        GL_STATIC_DRAW
    );

    ///////////////////////////////////////////////////////////
    // position
    ///////////////////////////////////////////////////////////

    glVertexAttribPointer(
        0,
        2,
        GL_FLOAT,
        GL_FALSE,
        4 * sizeof(float),
        (void*)0
    );

    glEnableVertexAttribArray(0);

    ///////////////////////////////////////////////////////////
    // uv
    ///////////////////////////////////////////////////////////

    glVertexAttribPointer(
        1,
        2,
        GL_FLOAT,
        GL_FALSE,
        4 * sizeof(float),
        (void*)(2 * sizeof(float))
    );

    glEnableVertexAttribArray(1);

    ///////////////////////////////////////////////////////////
    // texture
    ///////////////////////////////////////////////////////////

    texture;

    glGenTextures(1, &texture);

    glBindTexture(GL_TEXTURE_2D, texture);

    ///////////////////////////////////////////////////////////

    glTexParameteri(
        GL_TEXTURE_2D,
        GL_TEXTURE_MIN_FILTER,
        GL_LINEAR
    );

    glTexParameteri(
        GL_TEXTURE_2D,
        GL_TEXTURE_MAG_FILTER,
        GL_LINEAR
    );

    glTexParameteri(
        GL_TEXTURE_2D,
        GL_TEXTURE_WRAP_S,
        GL_CLAMP_TO_EDGE
    );

    glTexParameteri(
        GL_TEXTURE_2D,
        GL_TEXTURE_WRAP_T,
        GL_CLAMP_TO_EDGE
    );

    ///////////////////////////////////////////////////////////
    // ±ÜÃâ alignment ÎÊÌâ
    ///////////////////////////////////////////////////////////

    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);

    ///////////////////////////////////////////////////////////
    // shader uniform
    ///////////////////////////////////////////////////////////

    glUseProgram(program);

    glUniform1i(
        glGetUniformLocation(program, "uTexture"),
        0
    );




    // Setup Dear ImGui context
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO(); (void)io;
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;     // Enable Keyboard Controls
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableGamepad;      // Enable Gamepad Controls

    // Setup Dear ImGui style
    ImGui::StyleColorsDark();
    //ImGui::StyleColorsLight();

    // Setup scaling
    ImGuiStyle& style = ImGui::GetStyle();
    style.ScaleAllSizes(main_scale);        // Bake a fixed style scale. (until we have a solution for dynamic style scaling, changing this requires resetting Style + calling this again)
    style.FontScaleDpi = main_scale;        // Set initial font scale. (in docking branch: using io.ConfigDpiScaleFonts=true automatically overrides this for every window depending on the current monitor)

    // Setup Platform/Renderer backends
    ImGui_ImplGlfw_InitForOpenGL(window, true);
#ifdef __EMSCRIPTEN__
    ImGui_ImplGlfw_InstallEmscriptenCallbacks(window, "#canvas");
#endif
    ImGui_ImplOpenGL3_Init("#version 330");


    H264Decoder decoder;
    AudioPlay ad_player(48000, 1, 256);
	ad_player.Start();

    std::jthread receive_thread(Receive, std::ref(*client), std::ref(decoder), std::ref(ad_player));
    Draw(decoder);
    
    {
		std::lock_guard lock(mtx_close);
        client->Close();
    }

    //system("pause");
    return 0;
}