#include "PolySetCGALEvaluator.h"
#include "polyset.h"
#include "CGALEvaluator.h"
#include "projectionnode.h"
#include "dxflinextrudenode.h"
#include "dxfrotextrudenode.h"
#include "dxfdata.h"
#include "dxftess.h"
#include "module.h"

#include "printutils.h"
#include "openscad.h" // get_fragments_from_r()

PolySet *PolySetCGALEvaluator::evaluatePolySet(const ProjectionNode &node, AbstractPolyNode::render_mode_e)
{
	const string &cacheid = this->cgalevaluator.getTree().getString(node);
	if (this->cache.contains(cacheid)) return this->cache[cacheid]->ps->link();

	CGAL_Nef_polyhedron N = this->cgalevaluator.evaluateCGALMesh(node);

	PolySet *ps = new PolySet();
	ps->convexity = node.convexity;
	ps->is2d = true;

	if (node.cut_mode)
	{
		PolySet *cube = new PolySet();
		double infval = 1e8, eps = 0.1;
		double x1 = -infval, x2 = +infval, y1 = -infval, y2 = +infval, z1 = 0, z2 = eps;

		cube->append_poly(); // top
		cube->append_vertex(x1, y1, z2);
		cube->append_vertex(x2, y1, z2);
		cube->append_vertex(x2, y2, z2);
		cube->append_vertex(x1, y2, z2);

		cube->append_poly(); // bottom
		cube->append_vertex(x1, y2, z1);
		cube->append_vertex(x2, y2, z1);
		cube->append_vertex(x2, y1, z1);
		cube->append_vertex(x1, y1, z1);

		cube->append_poly(); // side1
		cube->append_vertex(x1, y1, z1);
		cube->append_vertex(x2, y1, z1);
		cube->append_vertex(x2, y1, z2);
		cube->append_vertex(x1, y1, z2);

		cube->append_poly(); // side2
		cube->append_vertex(x2, y1, z1);
		cube->append_vertex(x2, y2, z1);
		cube->append_vertex(x2, y2, z2);
		cube->append_vertex(x2, y1, z2);

		cube->append_poly(); // side3
		cube->append_vertex(x2, y2, z1);
		cube->append_vertex(x1, y2, z1);
		cube->append_vertex(x1, y2, z2);
		cube->append_vertex(x2, y2, z2);

		cube->append_poly(); // side4
		cube->append_vertex(x1, y2, z1);
		cube->append_vertex(x1, y1, z1);
		cube->append_vertex(x1, y1, z2);
		cube->append_vertex(x1, y2, z2);
		CGAL_Nef_polyhedron Ncube = this->cgalevaluator.evaluateCGALMesh(*cube);
		cube->unlink();

		// N.p3 *= CGAL_Nef_polyhedron3(CGAL_Plane(0, 0, 1, 0), CGAL_Nef_polyhedron3::INCLUDED);
		N.p3 *= Ncube.p3;
		if (!N.p3.is_simple()) {
			PRINTF("WARNING: Body of projection(cut = true) isn't valid 2-manifold! Modify your design..");
			goto cant_project_non_simple_polyhedron;
		}

		PolySet *ps3 = N.convertToPolyset();
		Grid2d<int> conversion_grid(GRID_COARSE);
		for (size_t i = 0; i < ps3->polygons.size(); i++) {
			for (size_t j = 0; j < ps3->polygons[i].size(); j++) {
				double x = ps3->polygons[i][j][0];
				double y = ps3->polygons[i][j][1];
				double z = ps3->polygons[i][j][2];
				if (z != 0)
					goto next_ps3_polygon_cut_mode;
				if (conversion_grid.align(x, y) == i+1)
					goto next_ps3_polygon_cut_mode;
				conversion_grid.data(x, y) = i+1;
			}
			ps->append_poly();
			for (size_t j = 0; j < ps3->polygons[i].size(); j++) {
				double x = ps3->polygons[i][j][0];
				double y = ps3->polygons[i][j][1];
				conversion_grid.align(x, y);
				ps->insert_vertex(x, y);
			}
		next_ps3_polygon_cut_mode:;
		}
		ps3->unlink();
	}
	else
	{
		if (!N.p3.is_simple()) {
			PRINTF("WARNING: Body of projection(cut = false) isn't valid 2-manifold! Modify your design..");
			goto cant_project_non_simple_polyhedron;
		}

		PolySet *ps3 = N.convertToPolyset();
		CGAL_Nef_polyhedron np;
		np.dim = 2;
		for (size_t i = 0; i < ps3->polygons.size(); i++)
		{
			int min_x_p = -1;
			double min_x_val = 0;
			for (size_t j = 0; j < ps3->polygons[i].size(); j++) {
				double x = ps3->polygons[i][j][0];
				if (min_x_p < 0 || x < min_x_val) {
					min_x_p = j;
					min_x_val = x;
				}
			}
			int min_x_p1 = (min_x_p+1) % ps3->polygons[i].size();
			int min_x_p2 = (min_x_p+ps3->polygons[i].size()-1) % ps3->polygons[i].size();
			double ax = ps3->polygons[i][min_x_p1][0] - ps3->polygons[i][min_x_p][0];
			double ay = ps3->polygons[i][min_x_p1][1] - ps3->polygons[i][min_x_p][1];
			double at = atan2(ay, ax);
			double bx = ps3->polygons[i][min_x_p2][0] - ps3->polygons[i][min_x_p][0];
			double by = ps3->polygons[i][min_x_p2][1] - ps3->polygons[i][min_x_p][1];
			double bt = atan2(by, bx);

			double eps = 0.000001;
			if (fabs(at - bt) < eps || (fabs(ax) < eps && fabs(ay) < eps) ||
					(fabs(bx) < eps && fabs(by) < eps)) {
				// this triangle is degenerated in projection
				continue;
			}

			std::list<CGAL_Nef_polyhedron2::Point> plist;
			for (size_t j = 0; j < ps3->polygons[i].size(); j++) {
				double x = ps3->polygons[i][j][0];
				double y = ps3->polygons[i][j][1];
				CGAL_Nef_polyhedron2::Point p = CGAL_Nef_polyhedron2::Point(x, y);
				if (at > bt)
					plist.push_front(p);
				else
					plist.push_back(p);
			}
			np.p2 += CGAL_Nef_polyhedron2(plist.begin(), plist.end(),
					CGAL_Nef_polyhedron2::INCLUDED);
		}
		DxfData dxf(np);
		dxf_tesselate(ps, &dxf, 0, true, false, 0);
		dxf_border_to_ps(ps, &dxf);
		ps3->unlink();
	}

cant_project_non_simple_polyhedron:

	this->cache.insert(cacheid, new cache_entry(ps->link()));
	return ps;
}

static void add_slice(PolySet *ps, DxfData::Path *pt, double rot1, double rot2, double h1, double h2)
{
	for (int j = 1; j < pt->points.count(); j++)
	{
		int k = j - 1;

		double jx1 = (*pt->points[j])[0] *  cos(rot1*M_PI/180) + (*pt->points[j])[1] * sin(rot1*M_PI/180);
		double jy1 = (*pt->points[j])[0] * -sin(rot1*M_PI/180) + (*pt->points[j])[1] * cos(rot1*M_PI/180);

		double jx2 = (*pt->points[j])[0] *  cos(rot2*M_PI/180) + (*pt->points[j])[1] * sin(rot2*M_PI/180);
		double jy2 = (*pt->points[j])[0] * -sin(rot2*M_PI/180) + (*pt->points[j])[1] * cos(rot2*M_PI/180);

		double kx1 = (*pt->points[k])[0] *  cos(rot1*M_PI/180) + (*pt->points[k])[1] * sin(rot1*M_PI/180);
		double ky1 = (*pt->points[k])[0] * -sin(rot1*M_PI/180) + (*pt->points[k])[1] * cos(rot1*M_PI/180);

		double kx2 = (*pt->points[k])[0] *  cos(rot2*M_PI/180) + (*pt->points[k])[1] * sin(rot2*M_PI/180);
		double ky2 = (*pt->points[k])[0] * -sin(rot2*M_PI/180) + (*pt->points[k])[1] * cos(rot2*M_PI/180);

		double dia1_len_sq = (jy1-ky2)*(jy1-ky2) + (jx1-kx2)*(jx1-kx2);
		double dia2_len_sq = (jy2-ky1)*(jy2-ky1) + (jx2-kx1)*(jx2-kx1);

		if (dia1_len_sq > dia2_len_sq)
		{
			ps->append_poly();
			if (pt->is_inner) {
				ps->append_vertex(kx1, ky1, h1);
				ps->append_vertex(jx1, jy1, h1);
				ps->append_vertex(jx2, jy2, h2);
			} else {
				ps->insert_vertex(kx1, ky1, h1);
				ps->insert_vertex(jx1, jy1, h1);
				ps->insert_vertex(jx2, jy2, h2);
			}

			ps->append_poly();
			if (pt->is_inner) {
				ps->append_vertex(kx2, ky2, h2);
				ps->append_vertex(kx1, ky1, h1);
				ps->append_vertex(jx2, jy2, h2);
			} else {
				ps->insert_vertex(kx2, ky2, h2);
				ps->insert_vertex(kx1, ky1, h1);
				ps->insert_vertex(jx2, jy2, h2);
			}
		}
		else
		{
			ps->append_poly();
			if (pt->is_inner) {
				ps->append_vertex(kx1, ky1, h1);
				ps->append_vertex(jx1, jy1, h1);
				ps->append_vertex(kx2, ky2, h2);
			} else {
				ps->insert_vertex(kx1, ky1, h1);
				ps->insert_vertex(jx1, jy1, h1);
				ps->insert_vertex(kx2, ky2, h2);
			}

			ps->append_poly();
			if (pt->is_inner) {
				ps->append_vertex(jx2, jy2, h2);
				ps->append_vertex(kx2, ky2, h2);
				ps->append_vertex(jx1, jy1, h1);
			} else {
				ps->insert_vertex(jx2, jy2, h2);
				ps->insert_vertex(kx2, ky2, h2);
				ps->insert_vertex(jx1, jy1, h1);
			}
		}
	}
}

PolySet *PolySetCGALEvaluator::evaluatePolySet(const DxfLinearExtrudeNode &node, AbstractPolyNode::render_mode_e)
{
	const string &cacheid = this->cgalevaluator.getTree().getString(node);
	if (this->cache.contains(cacheid)) return this->cache[cacheid]->ps->link();

	DxfData *dxf;

	if (node.filename.isEmpty())
	{
		// Before extruding, union all (2D) children nodes
		// to a single DxfData, then tesselate this into a PolySet
		CGAL_Nef_polyhedron N;
		N.dim = 2;
		foreach (AbstractNode * v, node.getChildren()) {
			if (v->modinst->tag_background) continue;
			N.p2 += this->cgalevaluator.evaluateCGALMesh(*v).p2;
		}

		dxf = new DxfData(N);
	} else {
		dxf = new DxfData(node.fn, node.fs, node.fa, node.filename, node.layername, node.origin_x, node.origin_y, node.scale);
	}

	PolySet *ps = new PolySet();
	ps->convexity = node.convexity;

	double h1, h2;

	if (node.center) {
		h1 = -node.height/2.0;
		h2 = +node.height/2.0;
	} else {
		h1 = 0;
		h2 = node.height;
	}

	bool first_open_path = true;
	for (int i = 0; i < dxf->paths.count(); i++)
	{
		if (dxf->paths[i].is_closed)
			continue;
		if (first_open_path) {
			PRINTF("WARNING: Open paths in dxf_linear_extrude(file = \"%s\", layer = \"%s\"):",
					node.filename.toAscii().data(), node.layername.toAscii().data());
			first_open_path = false;
		}
		PRINTF("   %9.5f %10.5f ... %10.5f %10.5f",
					 (*dxf->paths[i].points.first())[0] / node.scale + node.origin_x,
					 (*dxf->paths[i].points.first())[1] / node.scale + node.origin_y, 
					 (*dxf->paths[i].points.last())[0] / node.scale + node.origin_x,
					 (*dxf->paths[i].points.last())[1] / node.scale + node.origin_y);
	}


	if (node.has_twist)
	{
		dxf_tesselate(ps, dxf, 0, false, true, h1);
		dxf_tesselate(ps, dxf, node.twist, true, true, h2);
		for (int j = 0; j < node.slices; j++)
		{
			double t1 = node.twist*j / node.slices;
			double t2 = node.twist*(j+1) / node.slices;
			double g1 = h1 + (h2-h1)*j / node.slices;
			double g2 = h1 + (h2-h1)*(j+1) / node.slices;
			for (int i = 0; i < dxf->paths.count(); i++)
			{
				if (!dxf->paths[i].is_closed)
					continue;
				add_slice(ps, &dxf->paths[i], t1, t2, g1, g2);
			}
		}
	}
	else
	{
		dxf_tesselate(ps, dxf, 0, false, true, h1);
		dxf_tesselate(ps, dxf, 0, true, true, h2);
		for (int i = 0; i < dxf->paths.count(); i++)
		{
			if (!dxf->paths[i].is_closed)
				continue;
			add_slice(ps, &dxf->paths[i], 0, 0, h1, h2);
		}
	}

	delete dxf;

	this->cache.insert(cacheid, new cache_entry(ps->link()));
	return ps;
}

PolySet *PolySetCGALEvaluator::evaluatePolySet(const DxfRotateExtrudeNode &node, 
																						AbstractPolyNode::render_mode_e)
{
	const string &cacheid = this->cgalevaluator.getTree().getString(node);
	if (this->cache.contains(cacheid)) return this->cache[cacheid]->ps->link();

	DxfData *dxf;

	if (node.filename.isEmpty())
	{
		// Before extruding, union all (2D) children nodes
		// to a single DxfData, then tesselate this into a PolySet
		CGAL_Nef_polyhedron N;
		N.dim = 2;
		foreach (AbstractNode * v, node.getChildren()) {
			if (v->modinst->tag_background) continue;
			N.p2 += this->cgalevaluator.evaluateCGALMesh(*v).p2;
		}

		dxf = new DxfData(N);
	} else {
		dxf = new DxfData(node.fn, node.fs, node.fa, node.filename, node.layername, node.origin_x, node.origin_y, node.scale);
	}

	PolySet *ps = new PolySet();
	ps->convexity = node.convexity;

	for (int i = 0; i < dxf->paths.count(); i++)
	{
		double max_x = 0;
		for (int j = 0; j < dxf->paths[i].points.count(); j++) {
			max_x = fmax(max_x, (*dxf->paths[i].points[j])[0]);
		}

		int fragments = get_fragments_from_r(max_x, node.fn, node.fs, node.fa);

		double ***points;
		points = new double**[fragments];
		for (int j=0; j < fragments; j++) {
			points[j] = new double*[dxf->paths[i].points.count()];
			for (int k=0; k < dxf->paths[i].points.count(); k++)
				points[j][k] = new double[3];
		}

		for (int j = 0; j < fragments; j++) {
			double a = (j*2*M_PI) / fragments;
			for (int k = 0; k < dxf->paths[i].points.count(); k++) {
				if ((*dxf->paths[i].points[k])[0] == 0) {
					points[j][k][0] = 0;
					points[j][k][1] = 0;
				} else {
					points[j][k][0] = (*dxf->paths[i].points[k])[0] * sin(a);
					points[j][k][1] = (*dxf->paths[i].points[k])[0] * cos(a);
				}
				points[j][k][2] = (*dxf->paths[i].points[k])[1];
			}
		}

		for (int j = 0; j < fragments; j++) {
			int j1 = j + 1 < fragments ? j + 1 : 0;
			for (int k = 0; k < dxf->paths[i].points.count(); k++) {
				int k1 = k + 1 < dxf->paths[i].points.count() ? k + 1 : 0;
				if (points[j][k][0] != points[j1][k][0] ||
						points[j][k][1] != points[j1][k][1] ||
						points[j][k][2] != points[j1][k][2]) {
					ps->append_poly();
					ps->append_vertex(points[j ][k ][0],
							points[j ][k ][1], points[j ][k ][2]);
					ps->append_vertex(points[j1][k ][0],
							points[j1][k ][1], points[j1][k ][2]);
					ps->append_vertex(points[j ][k1][0],
							points[j ][k1][1], points[j ][k1][2]);
				}
				if (points[j][k1][0] != points[j1][k1][0] ||
						points[j][k1][1] != points[j1][k1][1] ||
						points[j][k1][2] != points[j1][k1][2]) {
					ps->append_poly();
					ps->append_vertex(points[j ][k1][0],
							points[j ][k1][1], points[j ][k1][2]);
					ps->append_vertex(points[j1][k ][0],
							points[j1][k ][1], points[j1][k ][2]);
					ps->append_vertex(points[j1][k1][0],
							points[j1][k1][1], points[j1][k1][2]);
				}
			}
		}

		for (int j=0; j < fragments; j++) {
			for (int k=0; k < dxf->paths[i].points.count(); k++)
				delete[] points[j][k];
			delete[] points[j];
		}
		delete[] points;
	}

	delete dxf;

	this->cache.insert(cacheid, new cache_entry(ps->link()));
	return ps;
}
