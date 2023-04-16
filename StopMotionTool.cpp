// StopMotionTool.cpp : Defines the entry point for the application.
//
#define TINYOBJLOADER_IMPLEMENTATION 
//#define TINYOBJLOADER_USE_MAPBOX_EARCUT

#include <Alembic/Abc/All.h>
#include <Alembic/AbcCoreOgawa/All.h>
#include <Alembic/AbcGeom/All.h>
#include <Alembic/AbcMaterial/All.h>
#include <Alembic/AbcCollection/All.h>	

#include "StopMotionTool.h"
#include <cxxopts.hpp>
#include <iostream>
#include <fstream>
#include <json/json.h>
#include "tiny_obj_loader.h"
#include <filesystem>
#include "External/xatlas/xatlas.h"
using namespace std;
using namespace Alembic::AbcGeom;


int main(int argc, char* argv[])
{
	cxxopts::Options options("StopMotionTool", "Converts a sequence of meshes to an alembic file, unwrapping UVs along the way");
	options.add_options()
		("f,file", "The objseq manifest file", cxxopts::value<string>())
		("o,outfile", "The path to the alembic file", cxxopts::value<string>())
		("h,help", "Print usage");

	options.parse_positional({ "file", "outfile" });

	auto arguments = options.parse(argc, argv);
	if (arguments.count("help")) {
		cout << options.help() << std::endl;
		exit(0);
	}
	
	auto should_auto_unwrap = true;// arguments.count("uv") > 0;

	if (arguments.count("f") == 0 || arguments.count("o") == 0)
	{
		cout << options.help() << endl;
		exit(EXIT_SUCCESS);
	}

	string in_file_path_str = arguments["f"].as<string>();
	ifstream input_file_stream;
	input_file_stream.open(in_file_path_str);
	if (!input_file_stream.is_open()) 
	{
		cout << "File Could Not Be Opened" << endl;
		exit(EXIT_FAILURE);
	}


	Json::Value root;
	input_file_stream >> root;

	auto start_frame = root.get("frame_start", 0).asInt();
	auto end_frame = root.get("frame_end", 0).asInt();
	auto frame_rate = root.get("frame_rate", 24).asUInt();
	auto loop = root.get("loop", false).asBool();
	auto root_materials_json = root["materials"];
	
	if (root_materials_json == NULL || root_materials_json.type() != Json::ValueType::arrayValue)
	{
		cout << "File contains malformed materials field";
		exit(EXIT_FAILURE);
	}
	std::vector<string> root_materials;
	for (auto i = 0; i < root_materials_json.size(); ++i) {
		root_materials.push_back(root_materials_json[i].asString());
	}
	
	auto objects_json = root["objects"];
	if (objects_json == NULL || objects_json.type()!=Json::ValueType::arrayValue) {
		cout << "File contains malformed objects field";
		exit(EXIT_FAILURE);
	}
	
	Alembic::Abc::OArchive alembic_file(
		Alembic::AbcCoreOgawa::WriteArchive(),
		arguments["o"].as<string>()
	);

	Abc::OObject materials(alembic_file.getTop(), "materials");
	for (auto i = 0; i < root_materials.size(); ++i) {
		auto material_name = root_materials[i];
		Alembic::AbcMaterial::OMaterial materialA(materials, material_name);
		materialA.getSchema().setShader("unity", "surface", "standard");
	}


	filesystem::path in_path(in_file_path_str);
	auto filename_without_extension = in_path.filename().replace_extension().replace_extension();
	auto parent_directory_path = in_path.parent_path();
	auto parent_directory_path_string = parent_directory_path.string();

	for (auto i = 0; i < objects_json.size(); ++i) {
		auto obj = objects_json[i];
		auto name = obj.get("name", NULL).asString();
		auto keyframes_json = obj["keyframes"];
		Alembic::AbcGeom::OPolyMesh mesh_obj(alembic_file.getTop(), name);
		auto mesh = mesh_obj.getSchema();

		map<string, Alembic::AbcGeom::OFaceSet> facesets;
		for (auto j = 0; j < root_materials.size(); ++j)
		{
			auto material_name = root_materials[j];
			auto faceset = mesh.createFaceSet(material_name);
			facesets[material_name] = faceset;
			Alembic::AbcMaterial::addMaterialAssignment(faceset, "/materials/" + material_name);
		}

		vector<chrono_t> time_samples;
		for (auto j = 0; j < keyframes_json.size(); ++j) {
			auto frame_json = keyframes_json[j];
			auto frame_number = frame_json.get("frame", 0).asInt();
			time_samples.push_back((1.0 / frame_rate) * frame_number);
		}
		TimeSamplingPtr time_sampling_ptr(new TimeSampling(TimeSamplingType(TimeSamplingType::kAcyclic), time_samples));
		mesh.setTimeSampling(time_sampling_ptr);
		
		// some apps can arbitrarily name their primary UVs, this function allows
		// you to do that, and must be done before the first time you set UVs
		// on the schema
		mesh.setUVSourceName("uv0");
		
		for (auto j = 0; j < keyframes_json.size(); ++j) {
			auto frame_json = keyframes_json[j];
			auto frame_number = frame_json.get("frame", 0).asInt();
			auto frame_materials_json = frame_json["materials"];
			tinyobj::attrib_t inattrib;
			std::vector<tinyobj::shape_t> inshapes;
			std::vector<tinyobj::material_t> inmaterials;

			std::string warn;
			std::string err;

			auto obj_file_path = parent_directory_path / (filename_without_extension.string() + "_" + name + "_" + to_string(frame_number) + ".obj");
			auto obj_file_string = obj_file_path.string();
			tinyobj::LoadObj(&inattrib, &inshapes, &inmaterials, &warn, &err, obj_file_string.c_str(), parent_directory_path_string.c_str(), true, true);

			if (!warn.empty()) {
				cout << "WARN: " << warn << endl;
			}
			if (!err.empty()) {
				cerr << err << endl;
				exit(EXIT_FAILURE);
			}

			if (inshapes.size() > 1) {
				cerr << "Expected only one mesh per obj file" << endl;
				exit(EXIT_FAILURE);
			}

			auto atlas = xatlas::Create();
			
			auto inmesh = inshapes[0].mesh;

			N3f* innormals = new N3f[inmesh.indices.size()];
			for (auto k = 0; k < inmesh.indices.size(); ++k) {
				auto indices = inmesh.indices[k];
				innormals[k] = N3f(inattrib.normals[indices.normal_index * 3], inattrib.normals[indices.normal_index * 3 + 1], inattrib.normals[indices.normal_index * 3 + 2]);
			}

			uint32_t* inface_indices = new uint32_t[inmesh.indices.size()];
			for (auto k = 0; k < inmesh.indices.size(); ++k) {
				auto indices = inmesh.indices[k];
				inface_indices[k] = indices.vertex_index;
			}

			uint32_t* inmaterial_ids = new uint32_t[inmesh.material_ids.size()];
			for (auto k = 0; k < inmesh.material_ids.size(); ++k) {
				auto material_id = inmesh.material_ids[k];
				inmaterial_ids[k] = (uint32_t)material_id;
			}
			// Unwrap mesh automatically
			xatlas::MeshDecl decl;
			xatlas::MeshDecl meshDecl;
			meshDecl.vertexCount = (uint32_t)inattrib.vertices.size() / 3;
			meshDecl.vertexPositionData = inattrib.vertices.data();
			meshDecl.vertexPositionStride = sizeof(float) * 3;
			meshDecl.vertexNormalData = innormals;
			meshDecl.vertexNormalStride = sizeof(float) * 3;

			meshDecl.indexCount = (uint32_t)inmesh.indices.size();
			meshDecl.indexData = inface_indices;
			meshDecl.indexFormat = xatlas::IndexFormat::UInt32;
			meshDecl.faceMaterialData = inmaterial_ids;

			xatlas::AddMeshError::Enum error = xatlas::AddMesh(atlas, meshDecl, (uint32_t)1);
			if (error != xatlas::AddMeshError::Success) {
				xatlas::Destroy(atlas);
				cerr << "Error adding mesh: " << endl << xatlas::StringForEnum(error) << endl;
				exit(EXIT_FAILURE);
			}
			xatlas::AddMesh(atlas, decl);

			xatlas::ChartOptions chart_options;
			chart_options.fixWinding = true;
			xatlas::Generate(atlas, chart_options);

			auto atlas_mesh = atlas->meshes[0];


			V2f* outuvs = new V2f[atlas_mesh.vertexCount];
			for (auto k = 0; k < atlas_mesh.vertexCount; ++k) {
				auto vertex = atlas_mesh.vertexArray[k];
				auto u = vertex.uv[0] / (float)atlas->width;
				auto v = vertex.uv[1] / (float)atlas->height;
				auto vertex_index = vertex.xref;
				outuvs[k] = V2f(u, v);
			}

			Alembic::AbcGeom::OV2fGeomParam::Sample uvsamp(
				Alembic::Abc::V2fArraySample((const Alembic::Abc::V2f*)outuvs, atlas_mesh.vertexCount),
				kVaryingScope
			);
		
			N3f* outnormals = new N3f[inmesh.indices.size()];
			for (auto k = 0; k < inmesh.indices.size(); ++k) {
				auto indices = inmesh.indices[k];
				outnormals[k] = N3f(inattrib.normals[indices.normal_index * 3], inattrib.normals[indices.normal_index * 3 + 1], inattrib.normals[indices.normal_index * 3 + 2]);
			}
			
			// indexed normals
			Alembic::AbcGeom::ON3fGeomParam::Sample nsamp(
				Alembic::Abc::N3fArraySample((const N3f*)outnormals, inmesh.indices.size()),
				kFacevaryingScope
			);

			int* outnumfacevertices = new int[inmesh.num_face_vertices.size()];
			for (auto k = 0; k < inmesh.num_face_vertices.size(); ++k) {
				outnumfacevertices[k] = inmesh.num_face_vertices[k];
			}
		
			V3f* outvertices = new V3f[atlas_mesh.vertexCount];
			for (auto k = 0; k < atlas_mesh.vertexCount; ++k) {
				auto vertex_index = atlas_mesh.vertexArray[k].xref;
				outvertices[k] = V3f(inattrib.vertices[vertex_index * 3], inattrib.vertices[vertex_index * 3 + 1], inattrib.vertices[vertex_index * 3 + 2]);
			}
		
			Alembic::AbcGeom::OPolyMeshSchema::Sample mesh_samp(
				V3fArraySample((const V3f*)outvertices, atlas_mesh.vertexCount),
				Int32ArraySample((const int*)atlas_mesh.indexArray, atlas_mesh.indexCount),
				Int32ArraySample((const int*)outnumfacevertices, inmesh.num_face_vertices.size()),
				uvsamp,
				nsamp
			);

			Box3d cbox;
			for (auto k = 0; k < inattrib.vertices.size() / 3; ++k) {
				cbox.extendBy(V3d(inattrib.vertices[k * 3], inattrib.vertices[k * 3 + 1], inattrib.vertices[k * 3 + 2]));
			}
			mesh.set(mesh_samp);
			mesh.getChildBoundsProperty().set(cbox);
	
			for (auto k = 0; k < frame_materials_json.size();++k) {
				auto material_name = frame_materials_json[k].asString();
				auto faceset = facesets[material_name];
				vector<Abc::int32_t> face_nums;
				auto material_ids = inmesh.material_ids;
				for (auto l = 0; l <  material_ids.size(); ++l) {
					if (material_ids[l] == k) {
						face_nums.push_back(l);
					}
				}
				Alembic::AbcGeom::OFaceSetSchema::Sample faceset_sample(face_nums);
				faceset.getSchema().set(faceset_sample);
				faceset.getSchema().setTimeSampling(time_sampling_ptr);
			}
			delete[] innormals;
			delete[] inface_indices;
			delete[] inmaterial_ids;
			delete[] outnumfacevertices;
			delete[] outuvs;
			delete[] outvertices;
			delete[] outnormals;
			xatlas::Destroy(atlas);
		}
		
	}
	
	return 0;
}
