#include <cgv/base/group.h>
#include <cgv/render/drawable.h>
#include <cgv/gui/provider.h>

#include <cg_nui/focusable.h>
#include <cg_nui/transforming.h>
#include <plugins/vr_lab/vr_tool.h>

#include "video_labeler.h"
#include "pressable.h"

class vr_label_tool : 
	public cgv::base::group,
	public cgv::render::drawable,
	public cgv::nui::focusable,
	public cgv::nui::transforming,
	public cgv::gui::provider,
	public vr::vr_tool
{
	/// label index to show statistics
	uint32_t li_stats;
	/// background color of statistics label
	rgba stats_bgclr;
	/// labels to show help on controllers
	uint32_t li_help[2];
	/// label for play button
	uint32_t li_play = -1;
	///
	bool playback = false;
public:
	std::string get_type_name() const
	{
		return "vr_label_tool";
	}
	bool self_reflect(cgv::reflect::reflection_handler& rh)
	{
		return false;
	}
	/// transform point with pose to lab coordinate system 
	vec3 compute_lab_draw_position(const float* pose, const vec3& p)
	{
		return mat34(3, 4, pose) * vec4(p, 1.0f);
	}
	video_labeler_ptr labeler;
	std::vector<pressable_ptr> buttons;
	
public:
	vr_label_tool() : cgv::base::group("vr_label_tool")
	{
		li_help[0] = li_help[1] = -1;
		li_stats = -1;
		stats_bgclr = rgba(0.8f, 0.6f, 0.0f, 0.6f);
		buttons.push_back(new pressable("play", vec3(0.6f, 0.015f, 0), rgb(0.6f, 0.3f, 0.1f), vec3(0.15f,0.03f,0.15f), 0.015f));
		connect_copy(buttons.back()->pressed, cgv::signal::rebind(this, &vr_label_tool::on_pressed, cgv::signal::_c<unsigned>(0)));
		append_child(buttons.back());
		labeler = video_labeler_ptr(new video_labeler("labeler", rgb(0.5, 0.5f, 0.3f)));
		append_child(labeler);
		register_object(labeler);
	}
	void on_pressed(unsigned i)
	{
		if (i == 0) {
			playback = !playback;
			on_set(&playback);
		}
	}
	void on_set(void* member_ptr)
	{
		if (member_ptr == &playback) {
			if (li_play != -1) {
				vr::vr_scene* scene_ptr = get_scene_ptr();
				if (scene_ptr)
					scene_ptr->update_label_text(li_play, playback ? "stop" : "play");
			}
		}
		if (member_ptr == &stats_bgclr && li_stats != -1)
			get_scene_ptr()->update_label_background_color(li_stats, stats_bgclr);

		update_member(member_ptr);
		post_redraw();
	}
	void init_frame(cgv::render::context& ctx)
	{		
		vr::vr_scene* scene_ptr = get_scene_ptr();
		if (!scene_ptr)
			return;
		// if not done before, create labels
		if (li_help[0] == -1) {
			li_play = scene_ptr->add_label("play", rgba(0, 0, 0, 0));
			scene_ptr->fix_label_size(li_play);
			scene_ptr->place_label(li_play, vec3(0.6f, 0.0301f, 0), quat(vec3(1,0,0), -1.57079632679489661f), coordinate_system::table);

			li_stats = scene_ptr->add_label(
				"drawing index: 000000\n"
				"nr vertices:   000000\n"
				"nr edges:      000000", stats_bgclr);
			scene_ptr->fix_label_size(li_stats);
			scene_ptr->place_label(li_stats, vec3(0.0f, 0.01f, 0.0f), quat(vec3(1, 0, 0), -1.5f), coordinate_system::table);
			scene_ptr->hide_label(li_stats);
			for (int ci = 0; ci < 2; ++ci) {
				li_help[ci] = scene_ptr->add_label("DPAD_Right .. next/new drawing\nDPAD_Left  .. prev drawing\nDPAD_Down  .. save drawing\nDPAD_Up .. toggle draw mode\nTPAD_Touch&Up/Dn .. change radius\nTPAD_Touch&Move .. change color\ncolorize (0.000)\nRGB(0.00,0.00,0.00)\nHLS(0.00,0.00,0.00)",
					rgba(ci == 0 ? 0.8f : 0.4f, 0.4f, ci == 1 ? 0.8f : 0.4f, 0.6f));
				scene_ptr->fix_label_size(li_help[ci]);
				scene_ptr->place_label(li_help[ci], vec3(ci == 1 ? -0.05f : 0.05f, 0.0f, 0.0f), quat(vec3(1, 0, 0), -1.5f),
					ci == 0 ? coordinate_system::left_controller : coordinate_system::right_controller, 
					ci == 1 ? label_alignment::right : label_alignment::left, 0.2f);
				scene_ptr->hide_label(li_help[ci]);
			}
		}
		// always update visibility of visibility changing labels
		vr_view_interactor* vr_view_ptr = get_view_ptr();
		if (!vr_view_ptr)
			return;
		const vr::vr_kit_state* state_ptr = vr_view_ptr->get_current_vr_state();
		if (!state_ptr)
			return;
		vec3 view_dir = -reinterpret_cast<const vec3&>(state_ptr->hmd.pose[6]);
		vec3 view_pos = reinterpret_cast<const vec3&>(state_ptr->hmd.pose[9]);
		for (int ci = 0; ci < 2; ++ci) {
			vec3 controller_pos = reinterpret_cast<const vec3&>(state_ptr->controller[ci].pose[9]);
			float controller_depth = dot(view_dir, controller_pos - view_pos);
			float controller_dist = (view_pos + controller_depth * view_dir - controller_pos).length();
			if (view_dir.y() < -0.25f && controller_depth / controller_dist > 1.0f)
				scene_ptr->show_label(li_help[ci]);
			else
				scene_ptr->hide_label(li_help[ci]);
		}
	}
	void draw(cgv::render::context& ctx)
	{
		mat4 model_transform(3, 4, &get_scene_ptr()->get_coordsystem(coordinate_system::table)(0, 0));
		set_model_transform(model_transform);

		ctx.push_modelview_matrix();
		ctx.mul_modelview_matrix(model_transform);
	}
	void finish_draw(cgv::render::context& ctx)
	{
		ctx.pop_modelview_matrix();
	}
	void stream_help(std::ostream& os)
	{
		os << "vr_label_tool: no interaction" << std::endl;
	}

	bool focus_change(cgv::nui::focus_change_action action, cgv::nui::refocus_action rfa, const cgv::nui::focus_demand& demand, const cgv::gui::event& e, const cgv::nui::dispatch_info& dis_info)
	{
		return false;
	}
	bool handle(const cgv::gui::event& e, const cgv::nui::dispatch_info& dis_info, cgv::nui::focus_request& request)
	{
		return false;
	}
	void create_gui()
	{
		add_decorator("vr_label_tool", "heading");
		add_member_control(this, "play", playback, "toggle");
		add_member_control(this, "stats_bgclr", stats_bgclr);
//		if (begin_tree_node("labeler", labeler, true)) {
//			align("\a");
//			inline_object_gui(labeler);
//			align("\b");
//			end_tree_node(labeler);
//		}
		if (begin_tree_node("buttons", buttons)) {
			align("\a");
			for (auto op : buttons)
				if (begin_tree_node(op->get_name(), *op)) {
					align("\a");
					inline_object_gui(op);
					align("\b");
					end_tree_node(*op);
				}
			align("\b");
			end_tree_node(buttons);
		}
	}
};


#include <cgv/base/register.h>
cgv::base::object_registration<vr_label_tool> vr_label_tool_reg("vr_label_tool");
#ifdef CGV_FORCE_STATIC
cgv::base::registration_order_definition ro_def("vr_view_interactor;vr_emulator;vr_scene;vr_label_tool");
#endif
