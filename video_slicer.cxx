#include "video_slicer.h"
#include <cgv/media/volume/volume_io.h>
#include <cgv/media/volume/sliced_volume_io.h>
#include <cgv/utils/scan.h>
#include <cgv/gui/dialog.h>
#include <cgv/utils/file.h>

video_slicer::vec3 video_slicer::world_to_voxel_coordinate_transform(const vec3& p_world) const
{
	return vec3(float(frame_width), float(frame_height), float(frame_count))*(p_world - (position - 0.5f * V.get_extent())) / V.get_extent();
}
video_slicer::vec3 video_slicer::voxel_to_world_coordinate_transform(const vec3& p_voxel) const
{
	return p_voxel*V.get_extent()/vec3(float(frame_width), float(frame_height), float(frame_count)) + position - 0.5f * V.get_extent();
}
video_slicer::vec3 video_slicer::voxel_to_world_coordinate_transform(const ivec3& i_voxel) const
{
	return voxel_to_world_coordinate_transform(vec3(i_voxel(0) + 0.5f, i_voxel(1) + 0.5f, i_voxel(2) + 0.5f));
}

bool video_slicer::read_video_file(const std::string& file_name, cgv::media::volume::volume& V, uint32_t frame_offset, uint32_t frame_count)
{
	cgv::media::volume::volume::dimension_type dims(-1, -1, -1);
	cgv::media::volume::volume::extent_type ext(-1, -1, -3);
	if (frame_count != uint32_t(-1)) {
		dims(2) = frame_count;
		ext(2) = float(frame_count);
	}
	cgv::data::component_format cf(cgv::type::info::TI_UINT8, pixel_format);
	if (!cgv::media::volume::read_volume_from_video_with_ffmpeg(V, file_name, dims, ext, cf, frame_offset, cgv::media::volume::FT_VERTICAL))
		return false;
	return true;
}

bool video_slicer::load_video(const std::string& file_name, uint32_t _frame_offset, uint32_t _frame_count)
{
	if (!read_video_file(file_name, V, _frame_offset, _frame_count))
		return false;
	frame_width = V.get_dimensions()(0);
	frame_height = V.get_dimensions()(1);
	frame_count = V.get_dimensions()(2);

	if (4*frame_count > std::max(frame_width, frame_height))
		V.ref_extent() = 0.7f * vec3(0.25f*float(frame_width) / frame_count, 0.25f * float(frame_height) / frame_count, 1.0f);
	else
		V.ref_extent() = 0.7f * vec3(1.0f, float(frame_height) / frame_width, 4*float(frame_count) / frame_width);

	position = vec3(0, 0.5f * V.ref_extent()(1)+0.01f, 0);
	vol_tex_outofdate = true;
	return true;
}

video_slicer::video_slicer()
{
	brs.culling_mode = cgv::render::CM_FRONTFACE;
	position = vec3(0, 0.501f, 0);
}

bool video_slicer::init(cgv::render::context& ctx)
{
	cgv::render::ref_box_renderer(ctx, 1);
	aam.init(ctx);
	return slice_prog.build_program(ctx, "slice.glpr");
}

void video_slicer::clear(cgv::render::context& ctx)
{
	cgv::render::ref_box_renderer(ctx, -1);
	aam.destruct(ctx);
	slice_prog.destruct(ctx);
}

void video_slicer::init_frame(cgv::render::context& ctx)
{
	if (vol_tex_outofdate) {
		if (vol_tex.get_width() != V.get_dimensions()(0) ||
			vol_tex.get_height() != V.get_dimensions()(1) ||
			vol_tex.get_depth() != V.get_dimensions()(2))
			vol_tex.destruct(ctx);
		if (vol_tex.is_created())
			vol_tex.replace(ctx, 0,0,0, V.get_data_view());
		else
			vol_tex.create(ctx, V.get_data_view());
		vol_tex_outofdate = false;
	}
}
void video_slicer::draw(cgv::render::context& ctx)
{
	// show box
	auto& br = cgv::render::ref_box_renderer(ctx);
	br.set_render_style(brs);
	br.set_position(ctx, position);
	br.set_color_array(ctx, &box_color, 1);
	br.set_extent(ctx, V.get_extent());
	br.render(ctx, 0, 1);

	if (!vol_tex.is_created())
		return;

	// construct slice geometry
	std::vector<vec3> P;
	std::vector<float> O;
	for (int i = 0; i < 3; ++i) {
		if (!show_slices[i] || slice_indices[i] == uint32_t(-1))
			continue;
		box3 B(vec3(0.0f), vec3(float(V.get_dimensions()(0)), float(V.get_dimensions()(1)), float(V.get_dimensions()(2))));
		B.ref_min_pnt()(i) = B.ref_max_pnt()(i) = slice_indices[i] + 0.5f;
		int j = (i + 1) % 3;
		int k = (j + 1) % 3;
		int off_j = int(pow(2, j));
		int off_k = int(pow(2, k));
		P.push_back(voxel_to_world_coordinate_transform(B.get_corner(0)));
		P.push_back(voxel_to_world_coordinate_transform(B.get_corner(off_j)));
		P.push_back(voxel_to_world_coordinate_transform(B.get_corner(off_j + off_k)));
		P.push_back(voxel_to_world_coordinate_transform(B.get_corner(0)));
		P.push_back(voxel_to_world_coordinate_transform(B.get_corner(off_j + off_k)));
		P.push_back(voxel_to_world_coordinate_transform(B.get_corner(off_k)));
		for (int s = 0; s < 6; ++s)
			O.push_back(1.0f);
	}
	// construct oblique slice geometry
	for (int i = 0; i < slice_origins.size(); ++i)
	{
		std::vector<vec3> polygon;
		construct_slice(i, polygon);

		if (!polygon.empty())
		{
			for (int i = polygon.size() - 1; i > 1; --i)
			{
				P.push_back(polygon[0]);
				P.push_back(polygon[i]);
				P.push_back(polygon[i - 1]);
				for (int s = 0; s < 3; ++s)
					O.push_back(1.0f);

#ifdef DEBUG
				std::cout << "texcoord " << (polygon[0] - (position - 0.5f * V.get_extent())) / V.get_extent() << std::endl;
				std::cout << "texcoord " << (polygon[i] - (position - 0.5f * V.get_extent())) / V.get_extent() << std::endl;
				std::cout << "texcoord " << (polygon[i - 1] - (position - 0.5f * V.get_extent())) / V.get_extent() << std::endl;
#endif
			}
		}
	}
	// render slice geometry
	if (!P.empty()) {
		GLboolean is_culling;
		glGetBooleanv(GL_CULL_FACE, &is_culling);
		glDisable(GL_CULL_FACE);
		aam.set_attribute_array(ctx, slice_prog.get_attribute_location(ctx, "position"), P);
		aam.set_attribute_array(ctx, slice_prog.get_attribute_location(ctx, "opacity"), O);
		aam.enable(ctx);
		vol_tex.enable(ctx, 0);
		slice_prog.enable(ctx);
		slice_prog.set_uniform(ctx, "box_min_point", position - 0.5f * V.get_extent());
		slice_prog.set_uniform(ctx, "box_extent", V.get_extent());
		slice_prog.set_uniform(ctx, "vol_tex", 0);
		glDrawArrays(GL_TRIANGLES, 0, GLsizei(P.size()));
		slice_prog.disable(ctx);
		vol_tex.disable(ctx);
		aam.disable(ctx);
		if (is_culling)
			glEnable(GL_CULL_FACE);
	}
}
bool video_slicer::create_slice(const vec3& origin, const vec3& direction, const rgba& color)
{
	box3 B(vec3(0.0f), vec3(float(V.get_dimensions()(0)), float(V.get_dimensions()(1)), float(V.get_dimensions()(2))));

	if (!B.inside(world_to_voxel_coordinate_transform(origin)))
		return false;

	slice_origins.emplace_back(origin);
	slice_directions.emplace_back(direction);

	//post_recreate_gui();

	return true;
}

bool video_slicer::delete_slice(int index, size_t count)
{
	if (index < 0 || index + count > slice_origins.size())
		return false;

	slice_origins.erase(slice_origins.begin() + index, slice_origins.begin() + index + count);
	slice_directions.erase(slice_directions.begin() + index, slice_directions.begin() + index + count);

	//post_recreate_gui();

	return true;
}

size_t video_slicer::get_num_slices() const
{
	return slice_origins.size();
}

void video_slicer::construct_slice(size_t index, std::vector<vec3>& polygon) const
{
	/************************************************************************************
	 Classify the volume box corners (vertices) as inside or outside vertices.
	 The signed_distance_from_slice()-method calculates the distance between each box corner and the slice.
	 Assume that outside vertices have a positive distance. */

	box3 B(vec3(0.0f), vec3(float(V.get_dimensions()(0)), float(V.get_dimensions()(1)), float(V.get_dimensions()(2))));

	float values[8];
	bool corner_classifications[8]; // true = outside, false = inside

	for (int c = 0; c < 8; ++c)
	{
		float value = signed_distance_from_slice(index, voxel_to_world_coordinate_transform(B.get_corner(c)));

		values[c] = value;
		corner_classifications[c] = value >= 0;
	}

	/************************************************************************************
	 Construct the edge points on each edge connecting differently classified
				   corners. Remember that the edge point coordinates are in range [0,1] for
				   all dimensions since they are 3D-texture coordinates. These points are
				   stored in the polygon-vector.
	 Arrange the points along face adjacencies for easier tessellation of the
				   polygon. Store the ordered edge points in the polygon-vector. Create your own
				   helper structures for edge-face adjacenies etc.*/

	static int a_corner_comparisons[12] = { 0, 2, 6, 4, 0, 4, 5, 1, 0, 1, 3, 2 };
	static int b_corner_comparisons[12] = { 1, 3, 7, 5, 2, 6, 7, 3, 4, 5, 7, 6 };

	std::vector<vec3> p;

	for (int i = 0; i < 12; ++i)
	{
		int a_corner_index = a_corner_comparisons[i];
		int b_corner_index = b_corner_comparisons[i];

		if (corner_classifications[a_corner_index] != corner_classifications[b_corner_index])
		{
			vec3 coord = B.get_corner(a_corner_index);
			float a_value = abs(values[a_corner_index]);
			float b_value = abs(values[b_corner_index]);

			float new_value = a_value / (a_value + b_value);

			coord(i / 4) = new_value * B.get_extent()(i / 4) + B.get_min_pnt()(i / 4);

			p.push_back(coord);
		}
	}

	if (p.empty())
		return;

	vec3 p0 = p[0];
	p.erase(p.begin());

	polygon.push_back(voxel_to_world_coordinate_transform(p0));

	while (p.size() > 1)
	{
		bool found = false;

		for (unsigned int i = 0; i < 3; ++i)
		{
			if (p0(i) < B.get_min_pnt()(i) + EPSILON ||
				p0(i) > B.get_max_pnt()(i) - EPSILON)
			{
				for (unsigned int j = 0; j < p.size(); ++j)
				{
					vec3 f = p[j];

					if (fabs(f(i) - p0(i)) < EPSILON)
					{
						p.erase(p.begin() + j);

						polygon.push_back(voxel_to_world_coordinate_transform(f));

						p0 = f;
						found = true;
						break;
					}
				}
			}

			if (found) break;
		}
	}

	polygon.push_back(voxel_to_world_coordinate_transform(p[0]));
}
float video_slicer::signed_distance_from_slice(size_t index, const vec3& p) const
{
	/************************************************************************************
	 The signed distance between the given point p and the slice which
	 is defined through slice_direction and slice_distance. */

	return dot(slice_directions[index], p - slice_origins[index]);
}
