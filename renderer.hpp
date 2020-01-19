#pragma once
#include <algorithm>
#include <cstddef>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/vec3.hpp>
#include <glm/vec4.hpp>
#include <list>
#include <vector>
using glm::vec3;
using glm::vec4;
using uint = uint32_t;
using byte = unsigned char;
struct Renderer {
	Renderer(uint resx, uint resy) : resx_(resx), resy_(resy) {
		SPT.resize(resy_);
		SET.resize(resy_);
	}
	bool render_scanline_zbuffer(const std::vector<vec3>& vertexes,
		const std::vector<uint>& faces,
		const std::vector<vec3>& normals, byte* frame) {
		clear();
		build_table(vertexes, faces, normals);
		std::vector<float> zb(resx_);
		auto clear_zb = [&]() {
			for (auto& v : zb) {
				v = INFINITY;
			}
		};
		std::list<Activate_Edge> AET;
		std::list<Activate_Polygon> APT;
		for (uint line = 0; line < resy_; line++) {
			uint y = resy_ - line - 1;
			if (APT.empty() && SPT[y].empty()) continue;
			byte* fb = &frame[line * resx_ * 3];
			clear_zb();
			//插入APT
			for (uint pid : SPT[y]) {
				auto&& p = PT[pid];
				Activate_Polygon ap;
				ap.pid = pid;
				ap.dy = p.dy;
				ap.ae = nullptr;
				p.ap = APT.insert(APT.end(), ap);
			}
			//插入AET
			for (uint eid : SET[y]) {
				auto&& e = ET[eid];
				Activate_Edge ae;
				ae.pid = e.pid;
				ae.dy = e.dy;
				ae.x = e.x;
				ae.z = e.z;
				ae.dx = e.dx;
				ae.dz = e.dz;
				ae.next_ae = nullptr;
				AET.push_back(ae);
				auto ae_ptr = &AET.back();
				auto&& ap = *PT[e.pid].ap;
				if (!ap.ae) {
					ap.ae = ae_ptr;
				}
				else {
					Activate_Edge* pre = nullptr;
					Activate_Edge* pp = ap.ae;
					while (pp) {
						if (ae_ptr->x < pp->x ||
							ae_ptr->x == pp->x && ae_ptr->dx < pp->dx) {
							ae_ptr->next_ae = pp;
							if (pre == nullptr)
								ap.ae = ae_ptr;
							else
								pre->next_ae = ae_ptr;
							break;
						}
						pre = pp;
						pp = pp->next_ae;
					}
					if (pp == nullptr) pre->next_ae = ae_ptr;
				}
			}
			//处理APT 和 AET
			for (auto&& ap : APT) {
				auto pp = ap.ae;
				while (pp) {
					auto&& left = *pp;
					pp = pp->next_ae;
					auto&& right = *pp;
					pp = pp->next_ae;
					int xl = left.x, xr = right.x;
					float z = left.z;
					auto&& polygon = PT[ap.pid];
					float dzx = polygon.dzx;
					vec3 normal = polygon.normal;
					for (int i = xl; i <= xr; i++) {
						if (z < zb[i]) {
							// shading
							shading(normal, fb[3 * i + 0], fb[3 * i + 1], fb[3 * i + 2]);
							// update zb
							zb[i] = z;
						}
						z += dzx;
					}
					//更新AET
					left.dy -= 1;
					left.x += left.dx;
					left.z += left.dz;
					right.dy -= 1;
					right.x += right.dx;
					//其实没用
					right.z += right.dz;
				}
				ap.dy -= 1;
			}
			//清理完成的AP和AE
			for (auto it = APT.begin(); it != APT.end();) {
				if (it->dy <= 0) {
					PT[it->pid].finished = true;
					it = APT.erase(it);
				}
				else
					it++;
			}
			for (auto it = AET.begin(); it != AET.end();) {
				if (it->dy <= 0) {
					if (PT[it->pid].finished == false) {
						//多边形还有未处理的部分
						auto&& ap = *PT[it->pid].ap;
						Activate_Edge* pp = ap.ae;
						Activate_Edge* pre = nullptr;
						Activate_Edge* cur_addr = &(*it);
						while (pp) {
							if (pp == cur_addr) {
								if (pre == nullptr) {
									ap.ae = cur_addr->next_ae;
								}
								else {
									pre->next_ae = cur_addr->next_ae;
								}
								break;
							}
							pre = pp;
							pp = pp->next_ae;
						}
					}
					it = AET.erase(it);
				}
				else
					++it;
			}
		}
		return true;
	}
	bool render_interval_scanline(const std::vector<vec3>& vertexes,
		const std::vector<uint>& faces,
		const std::vector<vec3>& normals, byte* frame) {
		clear();
		build_table(vertexes, faces, normals);
		std::list<Activate_Edge> AET;
		std::list<Activate_Polygon> APT;
		for (uint line = 0; line < resy_; line++) {
			uint y = resy_ - line - 1;
			if (AET.empty() && SET[y].empty()) continue;
			byte* fb = &frame[line * resx_ * 3];
			for (auto eid : SET[y]) {
				auto&& e = ET[eid];
				Activate_Edge ae;
				ae.dx = e.dx;
				ae.dy = e.dy;
				ae.dz = e.dz;
				ae.pid = e.pid;
				ae.x = e.x;
				ae.z = e.z;
				AET.push_back(ae);
			}
			AET.sort([&](const Activate_Edge& a, const Activate_Edge& b) {
				if (a.x == b.x) {
					if (a.dx == b.dx)
						return PT[a.pid].dzx < PT[b.pid].dzx;
					else
						return a.dx < b.dx;
				}
				else
					return a.x < b.x;
				});
			auto pre = AET.begin();
			Activate_Polygon first_p;
			first_p.pid = pre->pid;
			first_p.ae = &(*pre);
			PT[pre->pid].ap = APT.insert(APT.end(),first_p);
			PT[pre->pid].in = true;
			auto it = AET.begin();
			it++;
			//处理可见性
			for (; it != AET.end(); ++it) {
				if (!APT.empty()) {
					auto&& p = PT[APT.begin()->pid];
					auto normal = p.normal;
					int left = (int)(pre->x);
					int right = (int)(it->x);
					for (int i = left; i <=right; i++)
						shading(normal, fb[3 * i], fb[3 * i + 1], fb[3 * i + 2]);
				}
				auto&& p = PT[it->pid];
				if (p.in) {
					p.in = false;
					APT.erase(p.ap);
				}
				else {
					p.in = true;
					Activate_Polygon ap;
					ap.pid = it->pid;
					ap.ae = &(*it);
					float z = it->z;
					float dz = it->dz;
					auto it_ap = APT.begin();
					for (; it_ap != APT.end(); ++it_ap) {
						auto zz = it_ap->ae->z + (it->x - it_ap->ae->x) * PT[it_ap->pid].dzx;
						if (zz > z||
							zz==z&&PT[it_ap->pid].dzx>p.dzx) {
							p.ap = APT.insert(it_ap, ap); break;
						}
					}
					if (it_ap == APT.end()) p.ap = APT.insert(it_ap, ap);
				}
				pre = it;
			}
			assert(APT.empty());
			for (auto it = AET.begin(); it != AET.end();) {
				it->dy--;
				if (it->dy<=0) {
					it = AET.erase(it);
				}
				else {
					it->x += it->dx;
					it->z += it->dz;
					++it;
				}
			}
		}
		return true;
	}

private:
	uint resx_, resy_;
	struct Activate_Edge {
		float x, z;
		float dx;
		float dz;
		uint dy;
		uint pid;
		Activate_Edge* next_ae = nullptr;
	};
	struct Activate_Polygon {
		uint pid;
		uint dy;
		Activate_Edge* ae = nullptr;
	};
	struct Polygon {
		vec4 plane;
		uint dy;
		float dzx;
		vec3 normal;
		union {
			bool finished = false;
			bool in;
		};
		std::list<Activate_Polygon>::iterator ap;
	};
	struct Edge {
		// x in screen space, z in ndc space
		float x, z;
		float dx;
		float dz;
		uint dy;
		uint pid;
	};
	std::vector<Polygon> PT;
	std::vector<Edge> ET;
	std::vector<std::vector<uint>> SPT;
	std::vector<std::vector<uint>> SET;
	void clear() {
		PT.clear();
		ET.clear();
		for (auto&& a : SPT) a.clear();
		for (auto&& a : SET) a.clear();
	}
	void build_table(const std::vector<vec3>& vertexes,
		const std::vector<uint>& faces,
		const std::vector<vec3>& normals) {
		auto get_panel = [](const vec3& p1, const vec3& p2, const vec3& p3) {
			float a, b, c, d;
			a = (p2.y - p1.y) * (p3.z - p1.z) - (p2.z - p1.z) * (p3.y - p1.y);
			b = (p2.z - p1.z) * (p3.x - p1.x) - (p2.x - p1.x) * (p3.z - p1.z);
			c = (p2.x - p1.x) * (p3.y - p1.y) - (p2.y - p1.y) * (p3.x - p1.x);
			d = 0 - (a * p1.x + b * p1.y + c * p1.z);
			return vec4(a, b, c, d);
		};
		auto to_screen = [&](vec3& v) {
			v.x = (v.x + 1) / 2 * resx_;
			v.y = (v.y + 1) / 2 * resy_;
		};
		uint vidx = 0;
		for (int f = 0; f < faces.size(); f++) {
			uint face_vertex_num = faces[f];
			Polygon p;
			p.normal = normals[f];
			std::vector<vec3> face_vertexes(face_vertex_num);
			for (int v = 0; v < face_vertex_num; v++) {
				face_vertexes[v] = vertexes[vidx + v];
			}
			p.plane = get_panel(face_vertexes[0], face_vertexes[1], face_vertexes[2]);
			if (p.plane.z != 0) {
				p.dzx = -p.plane.x / p.plane.z * 2.f / resx_;
			}
			else {
				p.dzx = 0;
			}
			for (int v = 0; v < face_vertex_num; v++) to_screen(face_vertexes[v]);
			int pymax = -1, pymin = resy_;
			for (int e = 0; e < face_vertex_num; e++) {
				auto v0 = face_vertexes[e];
				auto v1 = face_vertexes[(e + 1) % face_vertex_num];
				bool is_peak = false;
				if (v0.y < v1.y) {
					std::swap(v0, v1);
					auto v2 = face_vertexes[(e - 1 + face_vertex_num) % face_vertex_num];
					if ((int)v2.y > (int)v1.y) is_peak = true;
				}
				else {
					auto v2 = face_vertexes[(e + 2) % face_vertex_num];
					if ((int)v2.y > (int)v1.y) is_peak = true;
				}
				bool is_horizontal = (int)v0.y == (int)v1.y;
				float ddy = (v0.y - v1.y);
				//避免斜率过大的问题
				if (ddy < 1) {
					ddy = 1;
				}
				Edge edge;
				edge.pid = f;
				edge.dx = (v1.x - v0.x) / ddy;
				edge.dz = (v1.z - v0.z) / ddy;
				edge.x = v0.x;
				edge.z = v0.z;
				int ymax = v0.y, ymin = v1.y;
				pymin = std::min(pymin, ymin);
				pymax = std::max(pymax, ymax);
				edge.dy = ymax - ymin + 1;
				if (!is_peak) edge.dy -= 1;
				if (!is_horizontal) SET[ymax].push_back(ET.size());
				ET.push_back(edge);
			}
			p.dy = pymax - pymin + 1;
			SPT[pymax].push_back(PT.size());
			PT.push_back(p);
			vidx += face_vertex_num;
		}
		for (auto&& et : SET) {
			std::sort(et.begin(), et.end(), [&](size_t a, size_t b) {
				if (ET[a].x == ET[b].x) {
					return ET[a].dx < ET[b].dx;
				}
				return ET[a].x < ET[b].x;
				});
		}
	}
	void shading(const vec3& normal, byte& r, byte& g, byte& b) {
		r = (normal.x + 1) / 2 * 255;
		g = (normal.y + 1) / 2 * 255;
		b = (normal.z + 1) / 2 * 255;
	}
};