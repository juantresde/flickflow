#define _USE_MATH_DEFINES

// Standard includes
#include <stdlib.h>
#include <cstdio>
#include <cmath>
#include "Leap.h"

// OpenGL includes
#include <GL/glew.h>
#include <GLFW/glfw3.h>

#include "fluidDynamics.h"
#include "shaderUtils.h"


#define DEFAULT_WIDTH 1280
#define DEFAULT_HEIGHT 720

// Simulation parameters
#define INK_SPLAT_SIZE 400.0f
#define VELOCITY_SPLAT_SIZE 22.0f
#define FREQUENCY .3f
#define COLOR_STEP_SIZE .02f

int viewportWidth, viewportHeight;
Slab velocity, density, pressure, diffusion, divergence, vorticity;


enum DisplayMode {
    DENSITY,
    VELOCITY,
    PRESSURE,
    DIVERGENCE,
    VORTICITY
};

DisplayMode mode = DENSITY;


using namespace Leap;


void resize(GLFWwindow* window, int width, int height) {
    viewportWidth = width;
    viewportHeight = height;
}


void keyCallback(GLFWwindow* window, int key, int scancode, int action, int mods) {
    if (action == GLFW_PRESS) {
        switch(key) {
            case(GLFW_KEY_1):
                std::cout << "Visualizing ink" << std::endl;
                mode = DENSITY;
                break;
            case(GLFW_KEY_2):
                std::cout << "Visualizing velocity" << std::endl;
                mode = VELOCITY;
                break;
            case(GLFW_KEY_3):
                std::cout << "Visualizing pressure" << std::endl;
                mode = PRESSURE;
                break;
            case(GLFW_KEY_4):
                std::cout << "Visualizing divergence" << std::endl;
                mode = DIVERGENCE;
                break;
            case(GLFW_KEY_5):
                std::cout << "Visualizing vorticity" << std::endl;
                mode = VORTICITY;
                break;
            default:
                break;
        }
    }
}


int main(int argc, char** argv) {
    GLFWwindow* window;

    if (!glfwInit()) {
        exit(EXIT_FAILURE);
    }

    // Make sure we're running the highest version of OpenGL possible
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 2);
    glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GL_TRUE);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);

    window = glfwCreateWindow(DEFAULT_WIDTH, DEFAULT_HEIGHT, "FlickFlow", NULL, NULL);
    if (!window) {
        glfwTerminate();
        exit(EXIT_FAILURE);
    }

    glfwMakeContextCurrent(window);
    glfwSwapInterval(1);
    glfwGetWindowSize(window, &viewportWidth, &viewportHeight);

    glewExperimental = GL_TRUE;  // This is necessary because GLEW is shit
    if (glewInit() != GLEW_OK) {
        return -1;
    }

    std::printf("Using GL Version: %s\n", glGetString(GL_VERSION));

    glfwSetKeyCallback(window, keyCallback);

    // Set up the Leap motion
    Controller controller;

    // Set up all shaders
    initializeShaders();
    createQuad();
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    GLuint visualize = loadShaders("../shaders/all.vert", "../shaders/visualize.frag");

    // Initialize all our texture "slabs"
    density = createSlab(viewportWidth, viewportHeight, 3);
    velocity = createSlab(viewportWidth, viewportHeight, 2);
    pressure = createSlab(viewportWidth, viewportHeight, 1);
    diffusion = createSlab(viewportWidth, viewportHeight, 1);
    divergence = createSlab(viewportWidth, viewportHeight, 3);
    vorticity = createSlab(viewportWidth, viewportHeight, 2);

    int xposPrev, yposPrev;

    float colorCounter = 0, colorBase;
    float r, g, b;

    while (!glfwWindowShouldClose(window)) {
        // Run a step of the simulation
        simulate(velocity, density, pressure, diffusion, divergence, vorticity, viewportWidth, viewportHeight);

        // Render the textures to the window framebuffer
        glUseProgram(visualize);

        GLint biasLoc = glGetUniformLocation(visualize, "bias");
        GLint scaleLoc = glGetUniformLocation(visualize, "scale");
        GLint maxValLoc = glGetUniformLocation(visualize, "maxVal");

        glEnable(GL_BLEND);

        glViewport(0, 0, viewportWidth, viewportHeight);
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        glClearColor(0, 0, 0, 1);
        glClear(GL_COLOR_BUFFER_BIT);

        // TRIGGER WARNING
        swapVectorFields(&density);

        switch (mode) {
            case DENSITY:
                glBindTexture(GL_TEXTURE_2D, density.write.textureHandle);
                glUniform1f(maxValLoc, 1.0);
                glUniform3f(biasLoc, 0.0, 0.0, 0.0);
                break;
            case VELOCITY:
                glBindTexture(GL_TEXTURE_2D, velocity.read.textureHandle);
                glUniform1f(maxValLoc, 32.0);
                glUniform3f(biasLoc, 0.5, 0.5, 0.5);
                break;
            case PRESSURE:
                glBindTexture(GL_TEXTURE_2D, pressure.read.textureHandle);
                glUniform1f(maxValLoc, 64.0);
                glUniform3f(biasLoc, 0.5, 0.5, 0.5);
                break;
            case DIVERGENCE:
                glBindTexture(GL_TEXTURE_2D, divergence.read.textureHandle);
                glUniform1f(maxValLoc, 1.0);
                glUniform3f(biasLoc, 0.5, 0.5, 0.5);
                break;
            case VORTICITY:
                glBindTexture(GL_TEXTURE_2D, vorticity.read.textureHandle);
                glUniform1f(maxValLoc, 4.0);
                glUniform3f(biasLoc, 0.5, 0.5, 0.5);
                break;
        }

        glUniform2f(scaleLoc, 1.0f / viewportWidth, 1.0f / viewportHeight);
        glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
        glDisable(GL_BLEND);

        glfwSwapBuffers(window);
        glfwPollEvents();

        // Update the color counter
        colorCounter += COLOR_STEP_SIZE;
        colorBase = colorCounter * FREQUENCY;

        // Take Leap motion input and create splats at the fingers
        Frame frame = controller.frame();

        for (Finger finger : frame.fingers()) {
            if (finger.isExtended()) {
                Vector tipPosition = finger.stabilizedTipPosition();
                Vector tipVelocity = finger.tipVelocity();

                int xpos = (int) ((viewportWidth / 2.0) + (tipPosition.x * 4));
                int ypos = (int) (tipPosition.y * 4 - 300);

                float fingerBase = colorBase + 0.5 * ((int) finger.type());

                r = 0.5 + sin(fingerBase);
                g = 0.5 + sin(fingerBase + 2);
                b = 0.5 + sin(fingerBase + 4);

                // Splat the ink onto the screen by updating density field
                gaussianSplat(density.read, density.write, xpos, ypos, INK_SPLAT_SIZE, r, g, b);
                swapVectorFields(&density);

                // Also make the fluid move with the ink just added by updating velocity field in the same way
                splat(velocity.read, velocity.write, xpos, ypos, VELOCITY_SPLAT_SIZE, tipVelocity.x * 4, tipVelocity.y * 4, 0);
                swapVectorFields(&velocity);

                // As always, enforce boundary conditions on the velocity field after changing it
                checkBoundary(velocity.read, velocity.write, viewportWidth, viewportHeight, true);
                swapVectorFields(&velocity);
            }
        }

        // Disable the mouse if the Leap motion is being used.
        int fingerCount = frame.fingers().count();
        if (fingerCount == 0) {
            double xpos, ypos;
            glfwGetCursorPos(window, &xpos, &ypos);

            // Correct ypos for GLFW screen coordinate system
            ypos = viewportHeight - ypos;

            double xVel = xpos - xposPrev;
            double yVel = ypos - yposPrev;

            xposPrev = xpos;
            yposPrev = ypos;

            r = 0.5 + sin(colorBase);
            g = 0.5 + sin(colorBase + 2);
            b = 0.5 + sin(colorBase + 4);

            // Splat the ink into both density and velocity field (show the ink and also inject a velocity)
            gaussianSplat(density.read, density.write, xpos, ypos, INK_SPLAT_SIZE, r, g, b);
            swapVectorFields(&density);

            splat(velocity.read, velocity.write, xpos, ypos, VELOCITY_SPLAT_SIZE, xVel * 4, yVel * 4, 0);
            swapVectorFields(&velocity);

            checkBoundary(velocity.read, velocity.write, viewportWidth, viewportHeight, true);
            swapVectorFields(&velocity);
        }
    }

    glfwDestroyWindow(window);
    glfwTerminate();
    return EXIT_SUCCESS;
}
