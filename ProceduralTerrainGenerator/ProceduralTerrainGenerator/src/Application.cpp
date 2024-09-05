#include <GL/glew.h>
#include <GLFW/glfw3.h>

#include <iostream>
#include <fstream>
#include <string>
#include <sstream>

#include "Renderer.h"
#include "VertexBuffer.h"
#include "IndexBuffer.h"
#include "VertexArray.h"
#include "Shader.h"
#include "VertexBufferLayout.h"
#include "Texture.h"

#include "glm/glm.hpp"
#include "glm/gtc/matrix_transform.hpp"

#include "imgui/imgui.h"
#include "imgui/imgui_impl_glfw_gl3.h"

#include "tests/TestClearColor.h"
#include "tests/TestTexture2D.h"

#include "Noise.h"
#include "image.h"

int main(void)
{
    GLFWwindow* window;

    if (!glfwInit())
        return -1;

    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);

    window = glfwCreateWindow(1000, 1000, "Procedural terrain generator", NULL, NULL);

    if (!window)
    {
        glfwTerminate();
        return -1;
    }

    glfwMakeContextCurrent(window);
    glfwSwapInterval(1);

    if (glewInit() != GLEW_OK) {
        std::cout << "error" << std::endl;
    }

    {
        int height = 1000;
        int width = 1000;


        //Noise Generation and converting to grayscale image
        unsigned char* image = new unsigned char[height * width];
        float* noiseMap = new float[height * width];

        noise::getNoiseMap(noiseMap, height, width, 400, 8, 1.2f);
        ConvertToGrayscaleImage(noiseMap, image, height, width);
        //

        //Temporary vertices in order to show 2d greyscale image od Perlin Noise
        float vertices[] = {
            -1.0f,  1.0f,  0.0f, 1.0f,
            -1.0f, -1.0f,  0.0f, 0.0f,
             1.0f, -1.0f,  1.0f, 0.0f,
             1.0f,  1.0f,  1.0f, 1.0f
        };
        unsigned int indices[] = {
            0, 1, 2,
            2, 3, 0
        };
        //

        VertexArray va;
        VertexBuffer vb(vertices, 4 * 4 * sizeof(float));
        IndexBuffer ib(indices, 6);

        VertexBufferLayout layout;
        layout.Push<float>(2);
        layout.Push<float>(2);
        va.AddBuffer(vb, layout);

        glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void*)0);
        glEnableVertexAttribArray(0);
        glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void*)(2 * sizeof(float)));
        glEnableVertexAttribArray(1);

        Texture texture(height, width, image);
        Shader mapShader("res/shaders/map_vertex.shader", "res/shaders/map_fragment.shader");
        Renderer renderer;

        while (!glfwWindowShouldClose(window))
        {
            renderer.Clear();

            texture.Bind();
            renderer.Draw(va, ib, mapShader);

            glfwSwapBuffers(window);
            glfwPollEvents();
        }

        delete[] noiseMap;
        delete[] image;
    }
    ImGui_ImplGlfwGL3_Shutdown();
    ImGui::DestroyContext();
    glfwTerminate();
    return 0;
}
