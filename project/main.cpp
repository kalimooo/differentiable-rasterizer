#ifdef _WIN32
extern "C" _declspec(dllexport) unsigned int NvOptimusEnablement = 0x00000001;
#endif

#include <GL/glew.h>
#include <cmath>
#include <cstdlib>
#include <algorithm>
#include <chrono>

#include <labhelper.h>
#include <imgui.h>

#include <perf.h>

#include <glm/glm.hpp>
#include <glm/gtx/transform.hpp>
using namespace glm;

#include <Model.h>
#include "hdr.h"
#include "fbo.h"
#include <SOIL2/SOIL2.h>


///////////////////////////////////////////////////////////////////////////////
// Various globals
///////////////////////////////////////////////////////////////////////////////
SDL_Window* g_window = nullptr;
float currentTime = 0.0f;
float previousTime = 0.0f;
float deltaTime = 0.0f;
int windowWidth, windowHeight;

// Mouse input
ivec2 g_prevMouseCoords = { -1, -1 };
bool g_isMouseDragging = false;

// Toggle for rendering which texture
bool renderOriginalPerturbed = true;

///////////////////////////////////////////////////////////////////////////////
// Shader programs
///////////////////////////////////////////////////////////////////////////////
GLuint shaderProgram;       // Shader for rendering the final image
GLuint simpleShaderProgram; // Shader used to draw the shadow map
GLuint fullScreenQuadShaderProgram; // Shader for rendering the full screen quad

///////////////////////////////////////////////////////////////////////////////
// Environment
///////////////////////////////////////////////////////////////////////////////
float environment_multiplier = 1.5f;
const std::string envmap_base_name = "001";

///////////////////////////////////////////////////////////////////////////////
// Light source
///////////////////////////////////////////////////////////////////////////////
vec3 lightPosition;
vec3 point_light_color = vec3(1.f, 1.f, 1.f);

float point_light_intensity_multiplier = 10000.0f;


///////////////////////////////////////////////////////////////////////////////
// Camera parameters.
///////////////////////////////////////////////////////////////////////////////
vec3 cameraPosition(0.0f, 0.0f, 0.0f);
vec3 cameraDirection = normalize(vec3(0.0f, 0.0f, -1.0f)); 
float cameraSpeed = 10.f;

vec3 worldUp(0.0f, 1.0f, 0.0f);

///////////////////////////////////////////////////////////////////////////////
// Models
///////////////////////////////////////////////////////////////////////////////
labhelper::Model* sphereModel = nullptr;
labhelper::Model* sphereModelPerturbedOpposite = nullptr;

mat4 roomModelMatrix;

///////////////////////////////////////////////////////////////////////////////
// Compute shaders
///////////////////////////////////////////////////////////////////////////////
GLuint originalVertexInputSSBO;
GLuint perturbedOutputSSBO;
GLuint perturbedOppositeOutputSSBO;
GLuint computeShaderProgram;

float perturbMag = 0.01f;
bool perturb = false;
bool perturbOnce = true;
bool hasBeenPerturbed = false;

///////////////////////////////////////////////////////////////////////////////
// Framebuffer Objects
///////////////////////////////////////////////////////////////////////////////
FboInfo* fbo1 = nullptr; // FBO for original perturbed sphere
FboInfo* fbo2 = nullptr; // FBO for oppositely perturbed sphere


void loadShaders(bool is_reload)
{
	GLuint shader = labhelper::loadShaderProgram("../project/simple.vert", "../project/simple.frag", is_reload);
	if(shader != 0)
	{
		simpleShaderProgram = shader;
	}

	shader = labhelper::loadShaderProgram("../project/shading.vert", "../project/shading.frag", is_reload);
	if(shader != 0)
	{
		shaderProgram = shader;
	}

	shader = labhelper::loadComputeShaderProgram("../project/perturb.comp", is_reload);
	if(shader != 0)
	{
		computeShaderProgram = shader;
	}

    shader = labhelper::loadShaderProgram("../project/fullscreenquad.vert", "../project/fullscreenquad.frag", is_reload);
    if (shader != 0)
    {
        fullScreenQuadShaderProgram = shader;
    }
}


///////////////////////////////////////////////////////////////////////////////
/// This function is called once at the start of the program and never again
///////////////////////////////////////////////////////////////////////////////
void initialize()
{
	ENSURE_INITIALIZE_ONLY_ONCE();

	///////////////////////////////////////////////////////////////////////
	//		Load Shaders
	///////////////////////////////////////////////////////////////////////
	loadShaders(false);

	///////////////////////////////////////////////////////////////////////
	// Load models and set up model matrices
	///////////////////////////////////////////////////////////////////////
	sphereModel = labhelper::loadModelFromOBJ("../scenes/sphere.obj");
    sphereModelPerturbedOpposite = labhelper::loadModelFromOBJ("../scenes/sphere.obj");

	vec3 initialSphereCenter = cameraPosition + cameraDirection * 100.0f;
    lightPosition = initialSphereCenter + vec3(0.0f, 20.0f, 0.0f); 

	roomModelMatrix = mat4(1.0f);

	// Create and bind SSBO for original vertex positions (input to compute shader)
	glGenBuffers(1, &originalVertexInputSSBO);
	glBindBuffer(GL_SHADER_STORAGE_BUFFER, originalVertexInputSSBO);
	glBufferData(GL_SHADER_STORAGE_BUFFER, sphereModel->m_positions.size() * sizeof(vec3),
			  sphereModel->m_positions.data(), GL_STATIC_DRAW);
	glBindBuffer(GL_SHADER_STORAGE_BUFFER, 0);

	// Create and bind SSBO for positively perturbed vertex positions (output from compute shader)
	glGenBuffers(1, &perturbedOutputSSBO);
	glBindBuffer(GL_SHADER_STORAGE_BUFFER, perturbedOutputSSBO);
	glBufferData(GL_SHADER_STORAGE_BUFFER, sphereModel->m_positions.size() * sizeof(vec3),
			  sphereModel->m_positions.data(), GL_DYNAMIC_COPY);
	glBindBuffer(GL_SHADER_STORAGE_BUFFER, 0);

	// Create and bind SSBO for oppositely perturbed vertex positions (output from compute shader)
	glGenBuffers(1, &perturbedOppositeOutputSSBO);
	glBindBuffer(GL_SHADER_STORAGE_BUFFER, perturbedOppositeOutputSSBO);
	glBufferData(GL_SHADER_STORAGE_BUFFER, sphereModelPerturbedOpposite->m_positions.size() * sizeof(vec3),
			  sphereModelPerturbedOpposite->m_positions.data(), GL_DYNAMIC_COPY);
	glBindBuffer(GL_SHADER_STORAGE_BUFFER, 0);

    // Initialize FBOs
    fbo1 = new FboInfo();
    fbo2 = new FboInfo();

	glEnable(GL_DEPTH_TEST); // enable Z-buffering
	glEnable(GL_CULL_FACE);  // enables backface culling
}


void perturbVertices() {
	glUseProgram(computeShaderProgram);

	// For some reason the labhelper version doesn't work??
	//labhelper::setUniformSlow(shaderProgram, "currentTime", currentTime);
    glUniform1f(glGetUniformLocation(computeShaderProgram, "currentTime"), currentTime);
	glUniform1f(glGetUniformLocation(computeShaderProgram, "perturbMag"), perturbMag);

	size_t numVertices = sphereModel->m_positions.size();

	// Bind the original vertex data as input
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, originalVertexInputSSBO);
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 0, originalVertexInputSSBO);

    // Bind the output buffers
	glBindBuffer(GL_SHADER_STORAGE_BUFFER, perturbedOutputSSBO);
	glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 1, perturbedOutputSSBO);
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, perturbedOppositeOutputSSBO);
	glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 2, perturbedOppositeOutputSSBO);

	glDispatchCompute(numVertices, 1, 1);

	glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT);

	// Read back modified vertex data for the first sphere (positively perturbed)
	std::vector<vec3> updatedPositions(sphereModel->m_positions.size());
	glBindBuffer(GL_SHADER_STORAGE_BUFFER, perturbedOutputSSBO);
	void* ptr = glMapBuffer(GL_SHADER_STORAGE_BUFFER, GL_READ_ONLY);
	memcpy(updatedPositions.data(), ptr, updatedPositions.size() * sizeof(vec3));
	glUnmapBuffer(GL_SHADER_STORAGE_BUFFER);
	glBindBuffer(GL_SHADER_STORAGE_BUFFER, 0);

	glBindBuffer(GL_ARRAY_BUFFER, sphereModel->m_positions_bo);
	glBufferSubData(GL_ARRAY_BUFFER, 0, updatedPositions.size() * sizeof(vec3), updatedPositions.data());
	glBindBuffer(GL_ARRAY_BUFFER, 0);

	// Read back modified vertex data for the second sphere (negatively perturbed)
    std::vector<vec3> updatedPositionsOpposite(sphereModelPerturbedOpposite->m_positions.size());
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, perturbedOppositeOutputSSBO);
    ptr = glMapBuffer(GL_SHADER_STORAGE_BUFFER, GL_READ_ONLY);
    memcpy(updatedPositionsOpposite.data(), ptr, updatedPositionsOpposite.size() * sizeof(vec3));
    glUnmapBuffer(GL_SHADER_STORAGE_BUFFER);
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, 0);

    glBindBuffer(GL_ARRAY_BUFFER, sphereModelPerturbedOpposite->m_positions_bo);
    glBufferSubData(GL_ARRAY_BUFFER, 0, updatedPositionsOpposite.size() * sizeof(vec3), updatedPositionsOpposite.data());
    glBindBuffer(GL_ARRAY_BUFFER, 0);


	// TODO probably change this stuff to work with the error from the paper?
	glBindBuffer(GL_COPY_READ_BUFFER, perturbedOutputSSBO); // Source buffer
	glBindBuffer(GL_COPY_WRITE_BUFFER, originalVertexInputSSBO); // Destination buffer
	
	glCopyBufferSubData(GL_COPY_READ_BUFFER, GL_COPY_WRITE_BUFFER, 0, 0, sphereModel->m_positions.size() * sizeof(vec3));
	
	glBindBuffer(GL_COPY_READ_BUFFER, 0);
	glBindBuffer(GL_COPY_WRITE_BUFFER, 0);
}

///////////////////////////////////////////////////////////////////////////////
/// This function is used to draw the main objects on the scene
///////////////////////////////////////////////////////////////////////////////
void drawScene(GLuint currentShaderProgram,
               const mat4& viewMatrix,
               const mat4& projectionMatrix,
               labhelper::Model* modelToRender)
{
	glUseProgram(currentShaderProgram);
	// Light source
	vec4 viewSpaceLightPosition = viewMatrix * vec4(lightPosition, 1.0f);
	labhelper::setUniformSlow(currentShaderProgram, "point_light_color", point_light_color);
	labhelper::setUniformSlow(currentShaderProgram, "point_light_intensity_multiplier",
	                          point_light_intensity_multiplier);
	labhelper::setUniformSlow(currentShaderProgram, "viewSpaceLightPosition", vec3(viewSpaceLightPosition));
	labhelper::setUniformSlow(currentShaderProgram, "viewSpaceLightDir",
	                          normalize(vec3(viewMatrix * vec4(-lightPosition, 0.0f))));


	// Environment
	labhelper::setUniformSlow(currentShaderProgram, "environment_multiplier", environment_multiplier);

	// camera
	labhelper::setUniformSlow(currentShaderProgram, "viewInverse", inverse(viewMatrix));
    
	mat4 modelMatrix = glm::translate(vec3(0.0f, 0.0f, -7.0f)); 
    // Render the specified model
    labhelper::setUniformSlow(currentShaderProgram, "modelViewProjectionMatrix",
                              projectionMatrix * viewMatrix * modelMatrix * mat4(1.0f));
    labhelper::render(modelToRender);
}


///////////////////////////////////////////////////////////////////////////////
/// This function will be called once per frame, so the code to set up
/// the scene for rendering should go here
///////////////////////////////////////////////////////////////////////////////
void display(void)
{
	labhelper::perf::Scope s( "Display" );

	///////////////////////////////////////////////////////////////////////////
	// Check if window size has changed and resize buffers as needed
	///////////////////////////////////////////////////////////////////////////
	{
		int w, h;
		SDL_GetWindowSize(g_window, &w, &h);
		if(w != windowWidth || h != windowHeight)
		{
			windowWidth = w;
			windowHeight = h;
            fbo1->resize(windowWidth, windowHeight);
            fbo2->resize(windowWidth, windowHeight);
		}
	}

	///////////////////////////////////////////////////////////////////////////
	// setup matrices
	///////////////////////////////////////////////////////////////////////////
	mat4 projMatrix = perspective(radians(45.0f), float(windowWidth) / float(windowHeight), 5.0f, 2000.0f);
	mat4 viewMatrix = lookAt(cameraPosition, cameraPosition + cameraDirection, worldUp);

	///////////////////////////////////////////////////////////////////////////
	// Render to FBO 1 (original perturbed sphere)
	///////////////////////////////////////////////////////////////////////////
    glBindFramebuffer(GL_FRAMEBUFFER, fbo1->framebufferId);
    glViewport(0, 0, windowWidth, windowHeight);
    glClearColor(0.2f, 0.2f, 0.8f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    drawScene(shaderProgram, viewMatrix, projMatrix, sphereModel);

    ///////////////////////////////////////////////////////////////////////////
    // Render to FBO 2 (oppositely perturbed sphere)
    ///////////////////////////////////////////////////////////////////////////
    glBindFramebuffer(GL_FRAMEBUFFER, fbo2->framebufferId);
    glViewport(0, 0, windowWidth, windowHeight);
    glClearColor(0.8f, 0.2f, 0.2f, 1.0f); // Different clear color to distinguish
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    drawScene(shaderProgram, viewMatrix, projMatrix, sphereModelPerturbedOpposite);


	///////////////////////////////////////////////////////////////////////////
	// Draw to screen using full screen quad (toggleable)
	///////////////////////////////////////////////////////////////////////////
	glBindFramebuffer(GL_FRAMEBUFFER, 0);
	glViewport(0, 0, windowWidth, windowHeight);
	glClearColor(0.2f, 0.2f, 0.8f, 1.0f);
	glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    glUseProgram(fullScreenQuadShaderProgram);
    glActiveTexture(GL_TEXTURE0);

    if (renderOriginalPerturbed) {
        glBindTexture(GL_TEXTURE_2D, fbo1->colorTextureTargets[0]);
    } else {
        glBindTexture(GL_TEXTURE_2D, fbo2->colorTextureTargets[0]);
    }
    labhelper::setUniformSlow(fullScreenQuadShaderProgram, "colorTexture", 0);
    labhelper::drawFullScreenQuad();

}

///////////////////////////////////////////////////////////////////////////////
/// This function is used to update the scene according to user input
///////////////////////////////////////////////////////////////////////////////
bool handleEvents(void)
{
	// check events (keyboard among other)
	SDL_Event event;
	bool quitEvent = false;
	while(SDL_PollEvent(&event))
	{
		labhelper::processEvent( &event );

		if(event.type == SDL_QUIT || (event.type == SDL_KEYUP && event.key.keysym.sym == SDLK_ESCAPE))
		{
			quitEvent = true;
		}
		if(event.type == SDL_KEYUP && event.key.keysym.sym == SDLK_g)
		{
			if ( labhelper::isGUIvisible() )
			{
				labhelper::hideGUI();
			}
			else
			{
				labhelper::showGUI();
			}
		}
        if(event.type == SDL_KEYUP && event.key.keysym.sym == SDLK_p)
        {
            renderOriginalPerturbed = !renderOriginalPerturbed;
        }
		if(event.type == SDL_KEYUP && event.key.keysym.sym == SDLK_r)
		{
			hasBeenPerturbed = false;
		}
		if(event.type == SDL_MOUSEBUTTONDOWN && event.button.button == SDL_BUTTON_LEFT
		   && (!labhelper::isGUIvisible() || !ImGui::GetIO().WantCaptureMouse))
		{
			g_isMouseDragging = true;
			int x;
			int y;
			SDL_GetMouseState(&x, &y);
			g_prevMouseCoords.x = x;
			g_prevMouseCoords.y = y;
		}

		if(!(SDL_GetMouseState(NULL, NULL) & SDL_BUTTON(SDL_BUTTON_LEFT)))
		{
			g_isMouseDragging = false;
		}

		if(event.type == SDL_MOUSEMOTION && g_isMouseDragging)
		{
			// More info at https://wiki.libsdl.org/SDL_MouseMotionEvent
			int delta_x = event.motion.x - g_prevMouseCoords.x;
			int delta_y = event.motion.y - g_prevMouseCoords.y;
			float rotationSpeed = 0.1f;
			mat4 yaw = rotate(rotationSpeed * deltaTime * -delta_x, worldUp);
			mat4 pitch = rotate(rotationSpeed * deltaTime * -delta_y,
			                    normalize(cross(cameraDirection, worldUp)));
			cameraDirection = vec3(pitch * yaw * vec4(cameraDirection, 0.0f));
			g_prevMouseCoords.x = event.motion.x;
			g_prevMouseCoords.y = event.motion.y;
		}
	}

	// check keyboard state (which keys are still pressed)
	const uint8_t* state = SDL_GetKeyboardState(nullptr);
	vec3 cameraRight = cross(cameraDirection, worldUp);

	if(state[SDL_SCANCODE_W])
	{
		cameraPosition += cameraSpeed * deltaTime * cameraDirection;
	}
	if(state[SDL_SCANCODE_S])
	{
		cameraPosition -= cameraSpeed * deltaTime * cameraDirection;
	}
	if(state[SDL_SCANCODE_A])
	{
		cameraPosition -= cameraSpeed * deltaTime * cameraRight;
	}
	if(state[SDL_SCANCODE_D])
	{
		cameraPosition += cameraSpeed * deltaTime * cameraRight;
	}
	if(state[SDL_SCANCODE_Q])
	{
		cameraPosition -= cameraSpeed * deltaTime * worldUp;
	}
	if(state[SDL_SCANCODE_E])
	{
		cameraPosition += cameraSpeed * deltaTime * worldUp;
	}
	return quitEvent;
}


///////////////////////////////////////////////////////////////////////////////
/// This function is to hold the general GUI logic
///////////////////////////////////////////////////////////////////////////////
void gui()
{
	// ----------------- Set variables --------------------------
	ImGui::Text("Application average %.3f ms/frame (%.1f FPS)", 1000.0f / ImGui::GetIO().Framerate,
	            ImGui::GetIO().Framerate);
    ImGui::Text("Press 'P' to toggle between perturbed spheres.");
	ImGui::Text("Press 'R' to reset peturb count.");
	ImGui::SliderFloat("perturbMag", &perturbMag, 0.0f, 1.0f);
	ImGui::Checkbox("Perturb on", &perturb);
	ImGui::Checkbox("Perturb only once", &perturbOnce);
	// ----------------------------------------------------------


	////////////////////////////////////////////////////////////////////////////////
	////////////////////////////////////////////////////////////////////////////////

	labhelper::perf::drawEventsWindow();
}

int main(int argc, char* argv[])
{
	g_window = labhelper::init_window_SDL("OpenGL Project");

	initialize();

	bool stopRendering = false;
	auto startTime = std::chrono::system_clock::now();

	while(!stopRendering)
	{
		//update currentTime
		std::chrono::duration<float> timeSinceStart = std::chrono::system_clock::now() - startTime;
		previousTime = currentTime;
		currentTime = timeSinceStart.count();
		deltaTime = currentTime - previousTime;

		// check events (keyboard among other)
		stopRendering = handleEvents();

		// Inform imgui of new frame
		labhelper::newFrame( g_window );

		// if (count == 100) perturbVertices();
		// count++;

		if (perturb) {
			if (perturbOnce) {
				if (!hasBeenPerturbed) {
					perturbVertices();
					hasBeenPerturbed = true;
				}
			}
			else {
				perturbVertices();
			}
		}
		// render to window
		display();

		// Render overlay GUI.
		gui();

		// Finish the frame and render the GUI
		labhelper::finishFrame();

		// Swap front and back buffer. This frame will now been displayed.
		SDL_GL_SwapWindow(g_window);
	}
	// Free Models
	labhelper::freeModel(sphereModel);
    labhelper::freeModel(sphereModelPerturbedOpposite);

    // Delete FBOs
    delete fbo1;
    delete fbo2;

	// Shut down everything. This includes the window and all other subsystems.
	labhelper::shutDown(g_window);
	return 0;
}