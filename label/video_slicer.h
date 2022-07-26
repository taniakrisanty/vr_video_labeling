#pragma once

#include <cgv/render/drawable.h>
#include <cgv/media/volume/volume.h>
#include <cgv_gl/box_renderer.h>

#define DEBUG

class video_slicer : public cgv::render::drawable
{
	bool vol_tex_outofdate = false;
protected:
	cgv::render::box_render_style brs;

	uint32_t frame_width  = uint32_t(-1);
	uint32_t frame_height = uint32_t(-1);
	uint32_t frame_offset = 0;
	uint32_t frame_count  = uint32_t(-1);

	cgv::data::ComponentFormat pixel_format = cgv::data::CF_RGB;
	std::string file_name;
	cgv::media::volume::volume V;

	cgv::render::texture vol_tex;
	cgv::render::shader_program slice_prog;
	cgv::render::attribute_array_manager aam;

	// geometry of slices
	std::vector<vec3> slice_origins;
	std::vector<vec3> slice_directions;

	int  slice_indices[3] = { -1, -1, -1 };
	bool show_slices[3] = { false, false, true };

	// position of volume
	vec3 position;
	// color used to render bounding box
	rgb box_color;

	vec3 world_to_voxel_coordinate_transform(const vec3& p_world) const;
	vec3 voxel_to_world_coordinate_transform(const vec3& p_voxel) const;
	vec3 voxel_to_world_coordinate_transform(const ivec3& i_voxel) const;
	// general video reading for later use to extent video volume with further video frames in case of very long videos
	bool read_video_file(const std::string& file_name, cgv::media::volume::volume& V, uint32_t frame_offset = 0, uint32_t frame_count = uint32_t(-1));
	// read and place video volume
	bool load_video(const std::string& file_name, uint32_t frame_offset = 0, uint32_t frame_count = uint32_t(-1));
public:
	video_slicer();

	bool init(cgv::render::context& ctx);
	void clear(cgv::render::context& ctx);
	void init_frame(cgv::render::context& ctx);
	void draw(cgv::render::context& ctx);

	void create_slice(const vec3& origin, const vec3& direction, const rgba& color = rgba(0.f, 1.f, 1.f, 0.05f));
	void delete_slice(size_t index, size_t count = 1);

	size_t get_num_slices() const;
private:
	void construct_slice(size_t index, std::vector<vec3>& polygon) const;
	float signed_distance_from_slice(size_t index, const vec3& p) const;
};
