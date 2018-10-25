/*
	Author: Arne-Tobias Rak
	TU Darmstadt

	Based on work by
	Xavier Corbillon
	IMT Atlantique
*/

//internal includes
#include "ShaderTexture.hpp"
#include "VideoReader.hpp"

namespace IMT {
static const GLchar* fragmentShaderYUV = 
"#version 330 core\n"
"in vec2 UV;\n"
"out vec3 color;\n"
"uniform sampler2D tex_y;\n"
"uniform sampler2D tex_u;\n"
"uniform sampler2D tex_v;\n"
"const mat3 coeff = mat3(1.164,  1.164, 1.164, \n"
"                        1.596, -0.813,   0.0, \n"
"                          0.0, -0.391, 2.018);\n"
"const vec3 offset = vec3(0.0625, 0.5, 0.5);\n"
"void main()\n"
"{\n"
"	float y = texture2D(tex_y, UV).r;\n"
"	float cb = texture2D(tex_u, UV).r;\n"
"	float cr = texture2D(tex_v, UV).r;\n"
"	color = coeff * (vec3(y,cr,cb) - offset);\n"
"}\n";

class ShaderTextureVideo : public ShaderTexture
{
public:
	ShaderTextureVideo(VideoTileStream* inputStreams, size_t numInputStreams, size_t nbFrames = -1, size_t bufferSize = 10, float startOffsetInSecond = 0) : ShaderTexture(),
		m_videoReader(inputStreams, numInputStreams, bufferSize, startOffsetInSecond)
	{
		m_videoReader.Init(nbFrames);
	}
	virtual ~ShaderTextureVideo(void) = default;

	void init() override
	{
		if (!m_initialized)
		{
			GLuint vertexShaderId = glCreateShader(GL_VERTEX_SHADER);
			GLuint fragmentShaderId = glCreateShader(GL_FRAGMENT_SHADER);

			// vertex shader
			glShaderSource(vertexShaderId, 1, &vertexShader, NULL);
			glCompileShader(vertexShaderId);
			checkShaderError(vertexShaderId, "Vertex shader compilation failed.");

			// fragment shader
			glShaderSource(fragmentShaderId, 1, &fragmentShaderYUV, NULL);
			glCompileShader(fragmentShaderId);
			checkShaderError(fragmentShaderId, "Fragment shader compilation failed.");

			// linking program
			m_programId = glCreateProgram();
			glAttachShader(m_programId, vertexShaderId);
			glAttachShader(m_programId, fragmentShaderId);
			glLinkProgram(m_programId);
			checkProgramError(m_programId, "Shader program link failed.");

			// once linked into a program, we no longer need the shaders.
			glDeleteShader(vertexShaderId);
			glDeleteShader(fragmentShaderId);

			m_projectionUniformId = glGetUniformLocation(m_programId, "projection");
			m_modelViewUniformId = glGetUniformLocation(m_programId, "modelView");
			m_initialized = true;
		}
	}

	DisplayFrameInfo useProgram(const GLdouble projection[], const GLdouble modelView[], std::chrono::system_clock::time_point deadline) override
	{
		init();
		glUseProgram(m_programId);
		GLfloat projectionf[16];
		GLfloat modelViewf[16];
		convertMatrix(projection, projectionf);
		convertMatrix(modelView, modelViewf);
		glUniformMatrix4fv(m_projectionUniformId, 1, GL_FALSE, projectionf);
		glUniformMatrix4fv(m_modelViewUniformId, 1, GL_FALSE, modelViewf);

		auto frameInfo = UpdateTexture(std::move(deadline));

		return std::move(frameInfo);
	}

	DisplayFrameInfo UpdateTexture(std::chrono::system_clock::time_point deadline) override
	{
		static bool first = true;

		if (first)
		{
			glGenTextures(3, m_textureIds);
			glUniform1i(glGetUniformLocation(m_programId, "tex_y"), 0);
			glUniform1i(glGetUniformLocation(m_programId, "tex_u"), 1);
			glUniform1i(glGetUniformLocation(m_programId, "tex_v"), 2);
			first = false;
		}

		auto frameInfo = m_videoReader.SetNextPictureToOpenGLTexture(deadline, m_textureIds);

		glActiveTexture(GL_TEXTURE0);

		return std::move(frameInfo);
	}

private:
	LibAv::VideoReader m_videoReader;

	GLuint m_textureIds[3] = { 0, 0, 0 };
};
}
