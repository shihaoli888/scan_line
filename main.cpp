//#include "scan_zbuffer.hpp"
#include "glm/glm.hpp"
#include "glm/gtc/matrix_transform.hpp"
#include "renderer.hpp"
//#include "stb_image_write.h"
#include "tiny_obj_loader.h"
#include <glad/glad.h>
#include <GLFW/glfw3.h>
#include <iostream>
#include <string>
#include <chrono>
namespace {
	//窗口显示相关
	GLuint vao, ebo, texture, program;
	const float quad_v_uv[] = { -1, -1, 1, 0, 0, 1, -1, 1, 1, 0, 1, 1, 1, 1, 1, -1, 1, 1, 0, 1 };
	const uint32_t quad_idx[] = { 0, 1, 2, 0, 2, 3 };
	const char* vs = R"(#version 330
	layout (location = 0) in vec3 vPosition;
	layout(location = 1) in vec2 vTexcoord;
	out vec2 texcoord;
	void main()
	{
		texcoord = vec2(vTexcoord.x,1-vTexcoord.y);
		gl_Position = vec4(vPosition, 1.0);
	}
	)";
	const char* fs = R"(#version 330
	in vec2 texcoord;
	out vec4 fragment_color;
	uniform sampler2D raw_img;
	void main(){
		vec3 color = texture(raw_img,texcoord).rgb;
		fragment_color = vec4(color,1.0);
	}
	)";
	//算法和模型相关
	uint resx = 512, resy = 512;
	std::string model_root = "models/";
	std::unique_ptr<Renderer> renderer;
	vec3 bound_min(INFINITY);
	vec3 bound_max(-INFINITY);
	float bound_radius;
	vec3 bound_center;
	std::vector<vec3> vertexes;
	std::vector<vec3> normals;
	std::vector<uint> faces;
	std::unique_ptr<byte[]> frame = nullptr;
	//控制相关
	bool use_zbuffer = true;
	float theta = 1.57;
	float phi = 0;
	double glfw_time = 0;
}

bool load_obj(const std::string& path) {
	vertexes.clear(); normals.clear(); faces.clear();
	tinyobj::attrib_t attrib;
	std::vector<tinyobj::shape_t> shapes;
	std::vector<tinyobj::material_t> materials;
	std::string warn;
	std::string err;
	bool ret =
		tinyobj::LoadObj(&attrib, &shapes, &materials, &warn, &err, path.c_str(), NULL, false);
	if (!err.empty()) {
		std::cerr << err << std::endl;
	}
	if (!ret) {
		return false;
	}
	auto get_normal = [](const vec3& p1, const vec3& p2, const vec3& p3) {
		vec3 n;
		n.x = (p2.y - p1.y) * (p3.z - p1.z) - (p2.z - p1.z) * (p3.y - p1.y);
		n.y = (p2.z - p1.z) * (p3.x - p1.x) - (p2.x - p1.x) * (p3.z - p1.z);
		n.z = (p2.x - p1.x) * (p3.y - p1.y) - (p2.y - p1.y) * (p3.x - p1.x);
		return glm::normalize(n);
	};
	// Loop over shapes
	for (size_t s = 0; s < shapes.size(); s++) {
		// Loop over faces(polygon)
		size_t index_offset = 0;
		for (size_t f = 0; f < shapes[s].mesh.num_face_vertices.size(); f++) {
			int fv = shapes[s].mesh.num_face_vertices[f];
			faces.push_back(fv);
			// Loop over vertices in the face.
			int begin = vertexes.size();
			for (size_t v = 0; v < fv; v++) {
				// access to vertex
				tinyobj::index_t idx = shapes[s].mesh.indices[index_offset + v];
				float vx = attrib.vertices[3 * idx.vertex_index + 0];
				float vy = attrib.vertices[3 * idx.vertex_index + 1];
				float vz = attrib.vertices[3 * idx.vertex_index + 2];
				vertexes.emplace_back(vx, vy, vz);
				bound_min.x = std::min(bound_min.x, vx);
				bound_min.y = std::min(bound_min.y, vy);
				bound_min.z = std::min(bound_min.z, vz);
				bound_max.x = std::max(bound_max.x, vx);
				bound_max.y = std::max(bound_max.y, vy);
				bound_max.z = std::max(bound_max.z, vz);
			}
			//使用前三点计算法线
			normals.push_back(get_normal(vertexes[begin], vertexes[begin + 1],
				vertexes[begin + 2]));
			index_offset += fv;
		}
	}
	bound_radius = glm::length(bound_max - bound_min) / 2;
	bound_center = (bound_max + bound_min) / 2.f;
	return true;
}
void key_callback(GLFWwindow* window, int key, int scancode, int action, int mods)
{
	if (glfwGetKey(window, GLFW_KEY_ESCAPE) == GLFW_PRESS)
		glfwSetWindowShouldClose(window, true);
	else if (glfwGetKey(window, GLFW_KEY_SPACE) == GLFW_PRESS) {
		if (use_zbuffer) {
			std::cout << "switch to intervel scanline" << std::endl;
		}
		else {
			std::cout << "switch to scanline zbuffer" << std::endl;
		}
		use_zbuffer = !use_zbuffer;
	}
	else if (glfwGetKey(window, GLFW_KEY_W) == GLFW_PRESS) {
		theta -= 0.157;
		theta = std::max(0.1f, theta);
	}
	else if (glfwGetKey(window, GLFW_KEY_S) == GLFW_PRESS) {
		theta += 0.157;
		theta = std::min(3.13f, theta);
	}
}

void init_shader() {
	GLuint v_shader = glCreateShader(GL_VERTEX_SHADER);
	glShaderSource(v_shader, 1, &vs, NULL);
	glCompileShader(v_shader);
	int success = 0;
	char infoLog[512];
	glGetShaderiv(v_shader, GL_COMPILE_STATUS, &success);

	if (!success) {
		glGetShaderInfoLog(v_shader, 512, NULL, infoLog);
		std::cout << "ERROR::SHADER::VERTEX::COMPILATION_FAILED\n" << infoLog << std::endl;
	}

	GLuint f_shader = glCreateShader(GL_FRAGMENT_SHADER);
	glShaderSource(f_shader, 1, &fs, NULL);
	glCompileShader(f_shader);
	glGetShaderiv(f_shader, GL_COMPILE_STATUS, &success);

	if (!success) {
		glGetShaderInfoLog(f_shader, 512, NULL, infoLog);
		std::cout << "ERROR::SHADER::FRAGMENT::COMPILATION_FAILED\n" << infoLog << std::endl;
	}

	program = glCreateProgram();
	glAttachShader(program, v_shader);
	glAttachShader(program, f_shader);
	glLinkProgram(program);
	glGetShaderiv(program, GL_LINK_STATUS, &success);

	if (!success) {
		glGetShaderInfoLog(program, 512, NULL, infoLog);
		std::cout << "ERROR::SHADER::PROGRAM::LINK_FAILED\n" << infoLog << std::endl;
	}

	//create vao vbo ebo
	glGenVertexArrays(1, &vao);
	glBindVertexArray(vao);
	GLuint vbo;
	glGenBuffers(1, &vbo);
	glBindBuffer(GL_ARRAY_BUFFER, vbo);
	glBufferData(GL_ARRAY_BUFFER, sizeof(quad_v_uv), quad_v_uv, GL_STATIC_DRAW);
	glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 5 * sizeof(float), (void*)0);
	glEnableVertexAttribArray(0);
	glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 5 * sizeof(float), (void*)(3 * sizeof(float)));
	glEnableVertexAttribArray(1);
	glBindVertexArray(0);

	glGenBuffers(1, &ebo);
	glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ebo);
	glBufferData(GL_ELEMENT_ARRAY_BUFFER, sizeof(quad_idx), quad_idx, GL_STATIC_DRAW);

	//create texture for post effective
	glGenTextures(1, &texture);
	glBindTexture(GL_TEXTURE_2D, texture);
	glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, resx, resy, 0, GL_RGB, GL_UNSIGNED_BYTE, NULL);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
	glBindTexture(GL_TEXTURE_2D, 0);

}

void render() {
	//随时间旋转相机
	double now = glfwGetTime();
	auto duration = now - glfw_time;
	glfw_time = now;
	phi += 0.5 * duration;
	if (phi > 6.28) phi -= 6.28;
	float cos_theta = std::cos(theta);
	float cos_phi = std::cos(phi);
	float sin_theta = std::sin(theta);
	float sin_phi = std::sin(phi);
	vec3 dir{ sin_theta * sin_phi,cos_theta,sin_theta * cos_phi };
	vec3 eye = bound_center + 2.f * bound_radius * dir;
	float tnear = 0.5f * bound_radius, tfar = 3.5f * bound_radius;
	auto view = glm::lookAt(eye, bound_center, { 0.f, 1.f, 0.f });
	auto perspective = glm::perspective(glm::radians(60.f), resx * 1.f / resy, tnear, tfar);
	std::vector<vec3> tvs;
	for (auto&& v : vertexes) {
		glm::vec4 vp(v.x, v.y, v.z, 1.f);
		vp = perspective * view * vp;
		tvs.emplace_back(vp.x / vp.w, vp.y / vp.w, vp.z / vp.w);
	}
	memset(frame.get(), 0, sizeof(frame[0]) * resx * resy * 3);
	auto time_start = std::chrono::steady_clock::now();
	if (use_zbuffer)
		renderer->render_scanline_zbuffer(tvs, faces, normals, frame.get());
	else
		renderer->render_interval_scanline(tvs, faces, normals, frame.get());
	auto time_end = std::chrono::steady_clock::now();
	auto microseconds =
		std::chrono::duration_cast<std::chrono::microseconds>(time_end - time_start)
		.count();
	glUseProgram(program);
	glActiveTexture(GL_TEXTURE0);
	glBindTexture(GL_TEXTURE_2D, texture);
	glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, resx, resy, GL_RGB, GL_UNSIGNED_BYTE, frame.get());
	glBindVertexArray(vao);
	glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ebo);
	glDrawElements(GL_TRIANGLES, 6, GL_UNSIGNED_INT, 0);
	glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);
	glBindVertexArray(0);
	glBindTexture(GL_TEXTURE_2D, 0);
	float ms = microseconds / 1000.f;
	printf("time: %.3f ms fps: %.3f", ms, 1000.f / ms);
	printf("\r");
}

int main(int argc, char** argv) {
	if (argc < 4) {
		std::cerr << "usage: scan_line.exe model_name width height\nexample: scan_line.exe bunny 512 512" << std::endl;
		return 1;
	}
	std::string path = model_root + argv[1] + ".obj";
	resx = std::stoi(argv[2]);
	resy = std::stoi(argv[3]);
	frame.reset(new byte[resx * resy * 3]);
	renderer.reset(new Renderer(resx, resy));
	glfwInit();
	glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
	glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
	glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
	glfwWindowHint(GLFW_RESIZABLE, GL_FALSE);
	GLFWwindow* window = glfwCreateWindow(resx, resy, "ScanLine", NULL, NULL);
	if (window == NULL)
	{
		std::cout << "Failed to create GLFW window" << std::endl;
		glfwTerminate();
		return -1;
	}
	glfwMakeContextCurrent(window);
	if (!gladLoadGLLoader((GLADloadproc)glfwGetProcAddress))
	{
		std::cout << "Failed to initialize GLAD" << std::endl;
		return -1;
	}
	init_shader();
	load_obj(path);
	glfwSetKeyCallback(window, key_callback);
	while (!glfwWindowShouldClose(window))
	{
		glfwPollEvents();
		render();
		glfwSwapBuffers(window);
	}
	glfwTerminate();
	return 0;
}