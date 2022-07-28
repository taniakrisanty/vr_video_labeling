#include "pressable.h"
#include <cgv/math/proximity.h>
#include <cgv/math/intersection.h>

cgv::render::shader_program pressable::prog;

pressable::rgb pressable::get_modified_color(const rgb& color) const
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
pressable::pressable(const std::string& _name, const vec3& _position, const rgb& _color, const vec3& _extent, float _radius, const quat& _rotation)
	: cgv::base::node(_name), position(_position), color(_color), extent(_extent), rotation(_rotation)
{
	brs.rounding = true;
	brs.default_radius = _radius;
}
std::string pressable::get_type_name() const
{
	return "pressable";
}
void pressable::on_set(void* member_ptr)
{
	update_member(member_ptr);
	post_redraw();
}
bool pressable::focus_change(cgv::nui::focus_change_action action, cgv::nui::refocus_action rfa, const cgv::nui::focus_demand& demand, const cgv::gui::event& e, const cgv::nui::dispatch_info& dis_info)
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
void pressable::stream_help(std::ostream& os)
{
	os << "pressable: point at it and pull trigger" << std::endl;
}
bool pressable::handle(const cgv::gui::event& e, const cgv::nui::dispatch_info& dis_info, cgv::nui::focus_request& request)
{
	// ignore all events in idle mode
	if (state == state_enum::idle)
		return false;
	// ignore events from other hids
	if (!(dis_info.hid_id == hid_id))
		return false;
	bool pressed;
	// hid independent check if object is triggered during pointing
	if (is_trigger_change(e, pressed)) {
		if (pressed) {
			state = state_enum::triggered;
			this->pressed();
			on_set(&state);
		}
		else {
			state = state_enum::pointed;
			on_set(&state);
		}
		post_redraw();
		return true;
	}
	return false;
}
bool pressable::compute_intersection(const vec3& ray_start, const vec3& ray_direction, float& hit_param, vec3& hit_normal, size_t& primitive_idx)
{
	vec3 ro = ray_start - position;
	vec3 rd = ray_direction;
	rotation.inverse_rotate(ro);
	rotation.inverse_rotate(rd);
	vec3 n;
	vec2 res;
	if (cgv::math::ray_box_intersection(ro, rd, 0.5f*extent, res, n) == 0)
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
	rotation.rotate(n);
	return true;
}
bool pressable::init(cgv::render::context& ctx)
{
	auto& br = cgv::render::ref_box_renderer(ctx, 1);
	if (prog.is_linked())
		return true;
	return br.build_program(ctx, prog, brs);
}
void pressable::clear(cgv::render::context& ctx)
{
	cgv::render::ref_box_renderer(ctx, -1);
}
void pressable::draw(cgv::render::context& ctx)
{
	// show box
	auto& br = cgv::render::ref_box_renderer(ctx);
	br.set_render_style(brs);
	if (brs.rounding)
		br.set_prog(prog);
	br.set_position(ctx, position);
	br.set_color_array(ctx, &color, 1);
	br.set_secondary_color(ctx, get_modified_color(color));
	br.set_extent(ctx, extent);
	br.set_rotation_array(ctx, &rotation, 1);
	br.render(ctx, 0, 1);
}
void pressable::create_gui()
{
	add_decorator(get_name(), "heading", "level=2");
	add_member_control(this, "color", color);
	add_member_control(this, "width", extent[0], "value_slider", "min=0.01;max=1;log=true");
	add_member_control(this, "height", extent[1], "value_slider", "min=0.01;max=1;log=true");
	add_member_control(this, "depth", extent[2], "value_slider", "min=0.01;max=1;log=true");
	add_gui("rotation", rotation, "direction", "options='min=-1;max=1;ticks=true'");
	if (begin_tree_node("style", brs)) {
		align("\a");
		add_gui("brs", brs);
		align("\b");
		end_tree_node(brs);
	}
}
