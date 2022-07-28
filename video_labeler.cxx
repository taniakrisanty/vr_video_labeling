#include "video_labeler.h"
#include <cgv/math/proximity.h>
#include <cgv/media/volume/volume_io.h>
#include <cgv/media/volume/sliced_volume_io.h>
#include <cgv/utils/scan.h>
#include <cgv/gui/dialog.h>
#include <cgv/utils/file.h>
#include <cgv/math/intersection.h>

video_labeler::rgb video_labeler::get_modified_color(const rgb& color) const
{
	rgb mod_col(color);
	switch (state) {
	case state_enum::grabbed:
		mod_col[1] = std::min(1.0f, mod_col[0] + 0.2f);
	case state_enum::close:
		mod_col[0] = std::min(1.0f, mod_col[0] + 0.2f);
		break;
	case state_enum::triggered:
		mod_col[1] = std::min(1.0f, mod_col[0] + 0.2f);
	case state_enum::pointed:
		mod_col[2] = std::min(1.0f, mod_col[2] + 0.2f);
		break;
	}
	return mod_col;
}

video_labeler::video_labeler(const std::string& _name, const rgb& _color)
	: cgv::base::node(_name)
{
	box_color = _color;
}

std::string video_labeler::get_type_name() const
{
	return "video_labeler";
}

bool video_labeler::open_file(const std::string& file_name)
{
	if (!load_video(file_name, frame_offset, frame_count))
		return false;
	update_member(&frame_width);
	update_member(&frame_height);
	update_member(&frame_count);
	for (int i = 0; i < 3; ++i) {
		update_member(&V.ref_extent()[i]);
		update_member(&position[i]);
		find_control(slice_indices[i])->set("max", V.get_dimensions()[i]-1);
		slice_indices[i] = V.get_dimensions()[i] / 2;
		update_member(&slice_indices[i]);
	}
	return true;
}


void video_labeler::on_set(void* member_ptr)
{
	if (member_ptr == &file_name) {
		if (!open_file(file_name)) {
			std::cerr << "could not open file " << file_name << std::endl;
		}
	}

	update_member(member_ptr);
	post_redraw();
}
bool video_labeler::focus_change(cgv::nui::focus_change_action action, cgv::nui::refocus_action rfa, const cgv::nui::focus_demand& demand, const cgv::gui::event& e, const cgv::nui::dispatch_info& dis_info)
{
	switch (action) {
	case cgv::nui::focus_change_action::attach:
		if (state == state_enum::idle) {
			// set state based on dispatch mode
			state = dis_info.mode == cgv::nui::dispatch_mode::pointing ? state_enum::pointed : state_enum::close;
			on_set(&state);
			// store hid to filter handled events
			hid_id = dis_info.hid_id;
			return true;
		}
		// if focus is given to other hid, refuse attachment to new hid
		return false;
	case cgv::nui::focus_change_action::detach:
		// check whether detach corresponds to stored hid
		if (state != state_enum::idle && hid_id == dis_info.hid_id) {
			state = state_enum::idle;
			on_set(&state);
			return true;
		}
		return false;
	case cgv::nui::focus_change_action::index_change:
		// nothing to be done because with do not use indices
		break;
	}
	return true;
}

bool video_labeler::self_reflect(cgv::reflect::reflection_handler& rh)
{
	return
		rh.reflect_member("frame_offset", frame_offset) &&
		rh.reflect_member("frame_count", frame_count) &&
		rh.reflect_member("file_name", file_name);
}

void video_labeler::stream_help(std::ostream& os)
{
	os << "video_labeler: grab and point at it" << std::endl;
}
bool video_labeler::handle(const cgv::gui::event& e, const cgv::nui::dispatch_info& dis_info, cgv::nui::focus_request& request)
{
	// ignore all events in idle mode
	if (state == state_enum::idle)
		return false;
	// ignore events from other hids
	if (!(dis_info.hid_id == hid_id))
		return false;
	bool pressed;
	// hid independent check if grabbing is activated or deactivated
	if (is_grab_change(e, pressed)) {
		if (pressed) {
			state = state_enum::grabbed;
			on_set(&state);
			drag_begin(request, false, original_config);
		}
		else {
			drag_end(request, original_config);
			state = state_enum::close;
			on_set(&state);
		}
		return true;
	}
	// check if event is for grabbing
	if (is_grabbing(e, dis_info)) {
		const auto& prox_info = get_proximity_info(dis_info);
		if (state == state_enum::close) {
			debug_point = prox_info.hit_point;
			query_point_at_grab = prox_info.query_point;
			position_at_grab = position;
		}
		else if (state == state_enum::grabbed) {
			debug_point = prox_info.hit_point;
			position = position_at_grab + prox_info.query_point - query_point_at_grab;
		}
		post_redraw();
		return true;
	}
	// hid independent check if object is triggered during pointing
	if (is_trigger_change(e, pressed)) {
		if (pressed) {
			state = state_enum::triggered;
			on_set(&state);
			drag_begin(request, true, original_config);
		}
		else {
			drag_end(request, original_config);
			state = state_enum::pointed;
			on_set(&state);
		}
		return true;
	}
	// check if event is for pointing
	if (is_pointing(e, dis_info)) {
		const auto& inter_info = get_intersection_info(dis_info);
		if (state == state_enum::pointed) {
			debug_point = inter_info.hit_point;
			hit_point_at_trigger = inter_info.hit_point;
			position_at_trigger = position;
		}
		else if (state == state_enum::triggered) {
			// if we still have an intersection point, use as debug point
			if (inter_info.ray_param != std::numeric_limits<float>::max())
				debug_point = inter_info.hit_point;
			// to be save even without new intersection, find closest point on ray to hit point at trigger
			vec3 q = cgv::math::closest_point_on_line_to_point(inter_info.ray_origin, inter_info.ray_direction, hit_point_at_trigger);
			position = position_at_trigger + q - hit_point_at_trigger;
		}
		post_redraw();
		return true;
	}
	return false;
}
bool video_labeler::compute_closest_point(const vec3& point, vec3& prj_point, vec3& prj_normal, size_t& primitive_idx)
{
	vec3 p = point - position;
	for (int i = 0; i < 3; ++i)
		p[i] = std::max(-0.5f * V.get_extent()[i], std::min(0.5f * V.get_extent()[i], p[i]));
	prj_point = p + position;
	return true;
}
bool video_labeler::compute_intersection(const vec3& ray_start, const vec3& ray_direction, float& hit_param, vec3& hit_normal, size_t& primitive_idx)
{
	vec3 ro = ray_start - position;
	vec3 rd = ray_direction;
	vec3 n;
	vec2 res;
	if (cgv::math::ray_box_intersection(ro, rd, 0.5f*V.get_extent(), res, n) == 0)
		return false;
	if (res[0] < 0) {
		if (res[1] < 0)
			return false;
		hit_param = res[1];
	}
	else {
		hit_param = res[0];
	}
	hit_normal = n;
	return true;
}

void video_labeler::create_gui()
{
	add_decorator(get_name(), "heading", "level=2");
	if (begin_tree_node("Video", file_name, true)) {
		align("\a");
		add_gui("File Name", file_name, "file_name", "title='Open Video';filter='Video Files (avi,mov,mp4,3gp,divx):*.avi;*.mov;*.mp4;*.3gp;*.divx|All Files:*.*';save=false;w=160");
		add_view("Frame Width", frame_width);
		add_view("Frame Height", frame_height);
		add_member_control(this, "Frame Count", frame_count, "value_slider", "min=-1;max=1000;log=true;ticks=true");
		add_member_control(this, "Frame Offset", frame_offset, "value_slider", "min=0;max=1000;log=true;ticks=true");
		align("\b");
		end_tree_node(file_name);
	}
	if (begin_tree_node("Slicing", slice_indices[0], true)) {
		align("\a");
		for (unsigned i = 0; i < 3; ++i) {
			add_member_control(this, std::string("Slice Index ") + "XYZ"[i], slice_indices[i],
				"value_slider", "min=-1;max=10;ticks=true;w=140", " ");
			find_control(slice_indices[i])->set("max", V.get_dimensions()[i]);
			add_member_control(this, "show", show_slices[i], "toggle", "w=40");
		}
		align("\b");
		end_tree_node(slice_indices[0]);
	}

	if (begin_tree_node("Box", position)) {
		align("\a");
		add_member_control(this, "Color", box_color);
		add_gui("Position", position, "vector", "options='min=-1;max=1;ticks=true;log=true'");
		add_gui("Extent", V.ref_extent(), "vector", "options='min=0.05;max=2;ticks=true;log=true'");
		if (begin_tree_node("Style", brs)) {
			align("\a");
			add_gui("brs", brs);
			align("\b");
			end_tree_node(brs);
		}
		align("\b");
		end_tree_node(position);
	}

}
