#include "Model.h"
#include "labhelper.h"
#include <iostream>
#define TINYOBJLOADER_IMPLEMENTATION // define this in only *one* .cc
#include <tiny_obj_loader.h>
//#include <experimental/tinyobj_loader_opt.h>
#include <algorithm>
#include <sstream>
#include <iomanip>
#include <GL/glew.h>
#include <stb_image.h>
#include <map>

namespace labhelper
{
bool Texture::load(const std::string& _directory, const std::string& _filename, int _components)
{
	filename = _filename;
	directory = _directory;
	valid = true;
	int components;
	data = stbi_load((directory + filename).c_str(), &width, &height, &components, _components);
	if(data == nullptr)
	{
		std::cout << "ERROR: loadModelFromOBJ(): Failed to load texture: " << filename << " in " << _directory
		          << "\n";
		exit(1);
	}
	glGenTextures(1, &gl_id);
	glBindTexture(GL_TEXTURE_2D, gl_id);
	GLenum format, internal_format;
	if(_components == 1)
	{
		format = GL_R;
		internal_format = GL_R8;
	}
	else if(_components == 3)
	{
		format = GL_RGB;
		internal_format = GL_RGB;
	}
	else if(_components == 4)
	{
		format = GL_RGBA;
		internal_format = GL_RGBA;
	}
	else
	{
		std::cout << "Texture loading not implemented for this number of compenents.\n";
		exit(1);
	}
	glTexImage2D(GL_TEXTURE_2D, 0, internal_format, width, height, 0, format, GL_UNSIGNED_BYTE, data);
	glGenerateMipmap(GL_TEXTURE_2D);
	glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
	glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
	glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MAX_ANISOTROPY_EXT, 16);

	glBindTexture( GL_TEXTURE_2D, 0 );
	return true;
}

///////////////////////////////////////////////////////////////////////////
// Destructor
///////////////////////////////////////////////////////////////////////////
Model::~Model()
{
	for(auto& material : m_materials)
	{
		if(material.m_color_texture.valid)
			glDeleteTextures(1, &material.m_color_texture.gl_id);
		if(material.m_reflectivity_texture.valid)
			glDeleteTextures(1, &material.m_reflectivity_texture.gl_id);
		if(material.m_shininess_texture.valid)
			glDeleteTextures(1, &material.m_shininess_texture.gl_id);
		if(material.m_metalness_texture.valid)
			glDeleteTextures(1, &material.m_metalness_texture.gl_id);
		if(material.m_fresnel_texture.valid)
			glDeleteTextures(1, &material.m_fresnel_texture.gl_id);
		if(material.m_emission_texture.valid)
			glDeleteTextures(1, &material.m_emission_texture.gl_id);
	}
	glDeleteBuffers(1, &m_positions_bo);
	glDeleteBuffers(1, &m_normals_bo);
	glDeleteBuffers(1, &m_texture_coordinates_bo);
	glDeleteBuffers(1, &m_indices_bo);
}

Model* loadModelFromOBJ(std::string path)
{
	///////////////////////////////////////////////////////////////////////
	// Separate filename into directory, base filename and extension
	// NOTE: This can be made a LOT simpler as soon as compilers properly
	//		 support std::filesystem (C++17)
	///////////////////////////////////////////////////////////////////////
	size_t separator = path.find_last_of("\\/");
	std::string filename, extension, directory;
	if(separator != std::string::npos)
	{
		filename = path.substr(separator + 1, path.size() - separator - 1);
		directory = path.substr(0, separator + 1);
	}
	else
	{
		filename = path;
		directory = "./";
	}
	separator = filename.find_last_of(".");
	if(separator == std::string::npos)
	{
		std::cout << "Fatal: loadModelFromOBJ(): Expecting filename ending in '.obj'\n";
		exit(1);
	}
	extension = filename.substr(separator, filename.size() - separator);
	filename = filename.substr(0, separator);

	///////////////////////////////////////////////////////////////////////
	// Parse the OBJ file using tinyobj
	///////////////////////////////////////////////////////////////////////
	std::cout << "Loading " << path << "..." << std::flush;
	tinyobj::attrib_t attrib;
	std::vector<tinyobj::shape_t> shapes;
	std::vector<tinyobj::material_t> materials;
	std::string err;
	// Expect '.mtl' file in the same directory and triangulate meshes
	bool ret = tinyobj::LoadObj(&attrib, &shapes, &materials, &err,
	                            (directory + filename + extension).c_str(), directory.c_str(), true);
	if(!err.empty())
	{ // `err` may contain warning message.
		std::cerr << err << std::endl;
	}
	if(!ret)
	{
		exit(1);
	}
	Model* model = new Model;
	model->m_name = filename;
	model->m_filename = path;

	///////////////////////////////////////////////////////////////////////
	// Transform all materials into our datastructure
	///////////////////////////////////////////////////////////////////////
	for(const auto& m : materials)
	{
		Material material;
		material.m_name = m.name;
		material.m_color = glm::vec3(m.diffuse[0], m.diffuse[1], m.diffuse[2]);
		if(m.diffuse_texname != "")
		{
			material.m_color_texture.load(directory, m.diffuse_texname, 4);
		}
		material.m_reflectivity = m.specular[0];
		if(m.specular_texname != "")
		{
			material.m_reflectivity_texture.load(directory, m.specular_texname, 1);
		}
		material.m_metalness = m.metallic;
		if(m.metallic_texname != "")
		{
			material.m_metalness_texture.load(directory, m.metallic_texname, 1);
		}
		material.m_fresnel = m.sheen;
		if(m.sheen_texname != "")
		{
			material.m_fresnel_texture.load(directory, m.sheen_texname, 1);
		}
		material.m_shininess = m.roughness;
		if(m.roughness_texname != "")
		{
			material.m_shininess_texture.load(directory, m.roughness_texname, 1);
		}
		material.m_emission = m.emission[0];
		if(m.emissive_texname != "")
		{
			material.m_emission_texture.load(directory, m.emissive_texname, 4);
		}
		material.m_transparency = m.transmittance[0];
		model->m_materials.push_back(material);
	}

	///////////////////////////////////////////////////////////////////////
	// A vertex in the OBJ file may have different indices for position,
	// normal and texture coordinate. We will now create unique vertices
	// and generate indices for indexed rendering.
	///////////////////////////////////////////////////////////////////////
	// We'll use a map to store unique vertices to avoid duplicates
	struct Vertex {
		glm::vec3 position;
		glm::vec3 normal;
		glm::vec2 texcoord;

		bool operator<(const Vertex& other) const {
			if (position != other.position) return position.x < other.position.x || (position.x == other.position.x && (position.y < other.position.y || (position.y == other.position.y && position.z < other.position.z)));
			if (normal != other.normal) return normal.x < other.normal.x || (normal.x == other.normal.x && (normal.y < other.normal.y || (normal.y == other.normal.y && normal.z < other.normal.z)));
			return texcoord.x < other.texcoord.x || (texcoord.x == other.texcoord.x && texcoord.y < other.texcoord.y);
		}
	};
	std::map<Vertex, uint32_t> unique_vertices;
	uint32_t current_vertex_index = 0;

	///////////////////////////////////////////////////////////////////////
	// For each vertex _position_ auto generate a normal that will be used
	// if no normal is supplied.
	///////////////////////////////////////////////////////////////////////
	std::vector<glm::vec4> auto_normals(attrib.vertices.size() / 3);
	for(const auto& shape : shapes)
	{
		for(int face = 0; face < int(shape.mesh.indices.size()) / 3; face++)
		{
			glm::vec3 v0 = glm::vec3(attrib.vertices[shape.mesh.indices[face * 3 + 0].vertex_index * 3 + 0],
			                         attrib.vertices[shape.mesh.indices[face * 3 + 0].vertex_index * 3 + 1],
			                         attrib.vertices[shape.mesh.indices[face * 3 + 0].vertex_index * 3 + 2]);
			glm::vec3 v1 = glm::vec3(attrib.vertices[shape.mesh.indices[face * 3 + 1].vertex_index * 3 + 0],
			                         attrib.vertices[shape.mesh.indices[face * 3 + 1].vertex_index * 3 + 1],
			                         attrib.vertices[shape.mesh.indices[face * 3 + 1].vertex_index * 3 + 2]);
			glm::vec3 v2 = glm::vec3(attrib.vertices[shape.mesh.indices[face * 3 + 2].vertex_index * 3 + 0],
			                         attrib.vertices[shape.mesh.indices[face * 3 + 2].vertex_index * 3 + 1],
			                         attrib.vertices[shape.mesh.indices[face * 3 + 2].vertex_index * 3 + 2]);

			glm::vec3 e0 = glm::normalize(v1 - v0);
			glm::vec3 e1 = glm::normalize(v2 - v0);
			glm::vec3 face_normal = cross(e0, e1);

			auto_normals[shape.mesh.indices[face * 3 + 0].vertex_index] += glm::vec4(face_normal, 1.0f);
			auto_normals[shape.mesh.indices[face * 3 + 1].vertex_index] += glm::vec4(face_normal, 1.0f);
			auto_normals[shape.mesh.indices[face * 3 + 2].vertex_index] += glm::vec4(face_normal, 1.0f);
		}
	}
	for(auto& normal : auto_normals)
	{
		normal = (1.0f / normal.w) * normal;
	}

	///////////////////////////////////////////////////////////////////////
	// Now we will turn all shapes into Meshes. A shape that has several
	// materials will be split into several meshes with unique names
	///////////////////////////////////////////////////////////////////////
	uint32_t indices_so_far = 0;
	for(const auto& shape : shapes)
	{
		///////////////////////////////////////////////////////////////////
		// The shapes in an OBJ file may several different materials.
		// If so, we will split the shape into one Mesh per Material
		///////////////////////////////////////////////////////////////////
		int next_material_index = shape.mesh.material_ids[0];
		int next_material_starting_face = 0;
		std::vector<bool> finished_materials(materials.size(), false);
		int number_of_materials_in_shape = 0;
		while(next_material_index != -1)
		{
			int current_material_index = next_material_index;
			int current_material_starting_face = next_material_starting_face;
			next_material_index = -1;
			next_material_starting_face = -1;
			// Process a new Mesh with a unique material
			Mesh mesh;
			mesh.m_name = shape.name + "_" + materials[current_material_index].name;
			mesh.m_material_idx = current_material_index;
			mesh.m_start_index = indices_so_far;
			number_of_materials_in_shape += 1;

			uint64_t number_of_faces = shape.mesh.indices.size() / 3;
			for(int i = current_material_starting_face; i < number_of_faces; i++)
			{
				if(shape.mesh.material_ids[i] != current_material_index)
				{
					if(next_material_index >= 0)
						continue;
					else if(finished_materials[shape.mesh.material_ids[i]])
						continue;
					else
					{ // Found a new material that we have not processed.
						next_material_index = shape.mesh.material_ids[i];
						next_material_starting_face = i;
					}
				}
				else
				{
					///////////////////////////////////////////////////////
					// Now we generate the unique vertices and indices
					///////////////////////////////////////////////////////
					for(int j = 0; j < 3; j++)
					{
						Vertex vertex;
						vertex.position = glm::vec3(attrib.vertices[shape.mesh.indices[i * 3 + j].vertex_index * 3 + 0],
						                            attrib.vertices[shape.mesh.indices[i * 3 + j].vertex_index * 3 + 1],
						                            attrib.vertices[shape.mesh.indices[i * 3 + j].vertex_index * 3 + 2]);
						if(shape.mesh.indices[i * 3 + j].normal_index == -1)
						{
							// No normal, use the autogenerated
							vertex.normal = glm::vec3(auto_normals[shape.mesh.indices[i * 3 + j].vertex_index]);
						}
						else
						{
							vertex.normal = glm::vec3(attrib.normals[shape.mesh.indices[i * 3 + j].normal_index * 3 + 0],
							                          attrib.normals[shape.mesh.indices[i * 3 + j].normal_index * 3 + 1],
							                          attrib.normals[shape.mesh.indices[i * 3 + j].normal_index * 3 + 2]);
						}
						if(shape.mesh.indices[i * 3 + j].texcoord_index == -1)
						{
							// No UV coordinates. Use null.
							vertex.texcoord = glm::vec2(0.0f);
						}
						else
						{
							vertex.texcoord = glm::vec2(attrib.texcoords[shape.mesh.indices[i * 3 + j].texcoord_index * 2 + 0],
							                            attrib.texcoords[shape.mesh.indices[i * 3 + j].texcoord_index * 2 + 1]);
						}

						// Check if this vertex already exists
						if (unique_vertices.count(vertex) == 0) {
							unique_vertices[vertex] = current_vertex_index;
							model->m_positions.push_back(vertex.position);
							model->m_normals.push_back(vertex.normal);
							model->m_texture_coordinates.push_back(vertex.texcoord);
							current_vertex_index++;
						}
						model->m_indices.push_back(unique_vertices[vertex]);
					}
					indices_so_far += 3; // Increment by 3 for indices
				}
			}
			///////////////////////////////////////////////////////////////
			// Finalize and push this mesh to the list
			///////////////////////////////////////////////////////////////
			mesh.m_number_of_indices = indices_so_far - mesh.m_start_index; // Store number of indices
			model->m_meshes.push_back(mesh);
			finished_materials[current_material_index] = true;
		}
		if(number_of_materials_in_shape == 1)
		{
			model->m_meshes.back().m_name = shape.name;
		}
	}

	///////////////////////////////////////////////////////////////////////
	// Upload to GPU
	///////////////////////////////////////////////////////////////////////
	glGenVertexArrays(1, &model->m_vaob);
	glBindVertexArray(model->m_vaob);
	glGenBuffers(1, &model->m_positions_bo);
	glBindBuffer(GL_ARRAY_BUFFER, model->m_positions_bo);
	glBufferData(GL_ARRAY_BUFFER, model->m_positions.size() * sizeof(glm::vec3), &model->m_positions[0].x,
	             GL_STATIC_DRAW);
	glVertexAttribPointer(0, 3, GL_FLOAT, false, 0, 0);
	glEnableVertexAttribArray(0);
	glGenBuffers(1, &model->m_normals_bo);
	glBindBuffer(GL_ARRAY_BUFFER, model->m_normals_bo);
	glBufferData(GL_ARRAY_BUFFER, model->m_normals.size() * sizeof(glm::vec3), &model->m_normals[0].x,
	             GL_STATIC_DRAW);
	glVertexAttribPointer(1, 3, GL_FLOAT, false, 0, 0);
	glEnableVertexAttribArray(1);
	glGenBuffers(1, &model->m_texture_coordinates_bo);
	glBindBuffer(GL_ARRAY_BUFFER, model->m_texture_coordinates_bo);
	glBufferData(GL_ARRAY_BUFFER, model->m_texture_coordinates.size() * sizeof(glm::vec2),
	             &model->m_texture_coordinates[0].x, GL_STATIC_DRAW);
	glVertexAttribPointer(2, 2, GL_FLOAT, false, 0, 0);
	glEnableVertexAttribArray(2);

	glGenBuffers(1, &model->m_indices_bo);
	glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, model->m_indices_bo);
	glBufferData(GL_ELEMENT_ARRAY_BUFFER, model->m_indices.size() * sizeof(uint32_t), &model->m_indices[0], GL_STATIC_DRAW);

	glBindVertexArray( 0 );
	glBindBuffer(GL_ARRAY_BUFFER, 0);
	glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);

	std::cout << "done.\n";
	return model;
}

void saveModelToOBJ(Model* model, std::string path)
{
	///////////////////////////////////////////////////////////////////////
	// Separate filename into directory, base filename and extension
	// NOTE: This can be made a LOT simpler as soon as compilers properly
	//		 support std::filesystem (C++17)
	///////////////////////////////////////////////////////////////////////
	size_t separator = path.find_last_of("\\/");
	std::string filename, extension, directory;
	if(separator != std::string::npos)
	{
		filename = path.substr(separator + 1, path.size() - separator - 1);
		directory = path.substr(0, separator + 1);
	}
	else
	{
		filename = path;
		directory = "./";
	}
	separator = filename.find_last_of(".");
	if(separator == std::string::npos)
	{
		std::cout << "Fatal: loadModelFromOBJ(): Expecting filename ending in '.obj'\n";
		exit(1);
	}
	extension = filename.substr(separator, filename.size() - separator);
	filename = filename.substr(0, separator);

	///////////////////////////////////////////////////////////////////////
	// Save Materials
	///////////////////////////////////////////////////////////////////////
	std::ofstream mat_file(directory + filename + ".mtl");
	if(!mat_file.is_open())
	{
		std::cout << "Could not open file " << filename << " for writing.\n";
		return;
	}
	mat_file << "# Exported by Chalmers Graphics Group\n";
	for(auto mat : model->m_materials)
	{
		mat_file << "newmtl " << mat.m_name << "\n";
		mat_file << "Kd " << mat.m_color.x << " " << mat.m_color.y << " " << mat.m_color.z << "\n";
		mat_file << "Ks " << mat.m_reflectivity << " " << mat.m_reflectivity << " " << mat.m_reflectivity
		         << "\n";
		mat_file << "Pm " << mat.m_metalness << "\n";
		mat_file << "Ps " << mat.m_fresnel << "\n";
		mat_file << "Pr " << mat.m_shininess << "\n";
		mat_file << "Ke " << mat.m_emission << " " << mat.m_emission << " " << mat.m_emission << "\n";
		mat_file << "Tf " << mat.m_transparency << " " << mat.m_transparency << " " << mat.m_transparency
		         << "\n";
		if(mat.m_color_texture.valid)
			mat_file << "map_Kd " << mat.m_color_texture.filename << "\n";
		if(mat.m_reflectivity_texture.valid)
			mat_file << "map_Ks " << mat.m_reflectivity_texture.filename << "\n";
		if(mat.m_metalness_texture.valid)
			mat_file << "map_Pm " << mat.m_metalness_texture.filename << "\n";
		if(mat.m_fresnel_texture.valid)
			mat_file << "map_Ps " << mat.m_fresnel_texture.filename << "\n";
		if(mat.m_shininess_texture.valid)
			mat_file << "map_Pr " << mat.m_shininess_texture.filename << "\n";
		if(mat.m_emission_texture.valid)
			mat_file << "map_Ke " << mat.m_emission_texture.filename << "\n";
	}
	mat_file.close();

	///////////////////////////////////////////////////////////////////////
	// Save geometry
	///////////////////////////////////////////////////////////////////////
	std::ofstream obj_file(directory + filename + ".obj");
	if(!obj_file.is_open())
	{
		std::cout << "Could not open file " << filename << " for writing.\n";
		return;
	}
	obj_file << "# Exported by Chalmers Graphics Group\n";
	obj_file << "mtllib " << filename << ".mtl\n";

	// Output unique vertices first
	for(const auto& pos : model->m_positions) {
		obj_file << "v " << pos.x << " " << pos.y << " " << pos.z << "\n";
	}
	for(const auto& normal : model->m_normals) {
		obj_file << "vn " << normal.x << " " << normal.y << " " << normal.z << "\n";
	}
	for(const auto& texcoord : model->m_texture_coordinates) {
		obj_file << "vt " << texcoord.x << " " << texcoord.y << "\n";
	}

	for(auto mesh : model->m_meshes)
	{
		obj_file << "o " << mesh.m_name << "\n";
		obj_file << "g " << mesh.m_name << "\n";
		obj_file << "usemtl " << model->m_materials[mesh.m_material_idx].m_name << "\n";

		// Output faces using indices
		int number_of_faces = mesh.m_number_of_indices / 3;
		for(int i = 0; i < number_of_faces; i++)
		{
			// OBJ indices are 1-based
			uint32_t idx0 = model->m_indices[mesh.m_start_index + i * 3 + 0] + 1;
			uint32_t idx1 = model->m_indices[mesh.m_start_index + i * 3 + 1] + 1;
			uint32_t idx2 = model->m_indices[mesh.m_start_index + i * 3 + 2] + 1;
			obj_file << "f " << idx0 << "/" << idx0 << "/" << idx0 << " " // pos/texcoord/normal
			         << idx1 << "/" << idx1 << "/" << idx1 << " "
			         << idx2 << "/" << idx2 << "/" << idx2 << "\n";
		}
	}
	obj_file.close();
}

///////////////////////////////////////////////////////////////////////
// Free model
///////////////////////////////////////////////////////////////////////
void freeModel(Model* model)
{
	if(model != nullptr)
		delete model;
}

///////////////////////////////////////////////////////////////////////
// Loop through all Meshes in the Model and render them
///////////////////////////////////////////////////////////////////////
void render(const Model* model, const bool submitMaterials)
{
	glBindVertexArray(model->m_vaob);
	glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, model->m_indices_bo);

	for(auto& mesh : model->m_meshes)
	{
		if(submitMaterials)
		{
			const Material& material = model->m_materials[mesh.m_material_idx];

			bool has_color_texture = material.m_color_texture.valid;
			bool has_reflectivity_texture = material.m_reflectivity_texture.valid;
			bool has_metalness_texture = material.m_metalness_texture.valid;
			bool has_fresnel_texture = material.m_fresnel_texture.valid;
			bool has_shininess_texture = material.m_shininess_texture.valid;
			bool has_emission_texture = material.m_emission_texture.valid;
			if ( has_color_texture )
			{
				glActiveTexture( GL_TEXTURE0 );
				glBindTexture( GL_TEXTURE_2D, material.m_color_texture.gl_id );
			}
			if(has_reflectivity_texture)
			{
				glActiveTexture( GL_TEXTURE1 );
				glBindTexture( GL_TEXTURE_2D, material.m_reflectivity_texture.gl_id );
			}
			if ( has_metalness_texture )
			{
				glActiveTexture( GL_TEXTURE2 );
				glBindTexture( GL_TEXTURE_2D, material.m_metalness_texture.gl_id );
			}
			if ( has_fresnel_texture )
			{
				glActiveTexture( GL_TEXTURE3 );
				glBindTexture( GL_TEXTURE_2D, material.m_fresnel_texture.gl_id );
			}
			if ( has_shininess_texture )
			{
				glActiveTexture( GL_TEXTURE4 );
				glBindTexture( GL_TEXTURE_2D, material.m_shininess_texture.gl_id );
			}
			if ( has_emission_texture )
			{
				glActiveTexture( GL_TEXTURE5 );
				glBindTexture( GL_TEXTURE_2D, material.m_emission_texture.gl_id );
			}
			glActiveTexture( GL_TEXTURE0 );
			GLint current_program = 0;
			glGetIntegerv(GL_CURRENT_PROGRAM, &current_program);

			setUniformSlow( current_program, "has_color_texture", has_color_texture );
			setUniformSlow( current_program, "has_emission_texture", has_emission_texture );

			setUniformSlow( current_program, "material_color", material.m_color );
			setUniformSlow( current_program, "material_reflectivity", material.m_reflectivity );
			setUniformSlow( current_program, "material_metalness", material.m_metalness );
			setUniformSlow( current_program, "material_fresnel", material.m_fresnel );
			setUniformSlow( current_program, "material_shininess", material.m_shininess );
			setUniformSlow( current_program, "material_emission", material.m_emission );

			// Actually unused in the labs
			setUniformSlow( current_program, "has_reflectivity_texture", has_reflectivity_texture );
			setUniformSlow( current_program, "has_metalness_texture", has_metalness_texture );
			setUniformSlow( current_program, "has_fresnel_texture", has_fresnel_texture );
			setUniformSlow( current_program, "has_shininess_texture", has_shininess_texture );

		}
		
		glPolygonMode(GL_FRONT_AND_BACK, GL_LINE);
		glDrawElements(GL_TRIANGLES, (GLsizei)mesh.m_number_of_indices, GL_UNSIGNED_INT, (const void*)(mesh.m_start_index * sizeof(uint32_t)));
		glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
	}
	glBindVertexArray(0);
	glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);
}
} // namespace labhelper