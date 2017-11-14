//
// Created by Stephen E Ingram on 11/9/17.
//

#include "readlayer.h"
#include <mapbox/geometry/multi_point.hpp>
#include <mapbox/geometry/multi_line_string.hpp>
#include <mapbox/geometry/multi_polygon.hpp>
#include <mapbox/geometry/geometry.hpp>
#include <mapbox/geometry/feature.hpp>
#include <mapbox/variant.hpp>
#include <protozero/pbf_reader.hpp>
#include "geometry.hpp"


struct oppoint {
    int op;
    long long x;
    long long y;

    oppoint(int nop, long long nx, long long ny) {
        this->op = nop;
        this->x = nx;
        this->y = ny;
    }
};

using mapbox::geometry::geometry;
using mapbox::geometry::multi_point;
using mapbox::geometry::point;
using mapbox::geometry::line_string;
using mapbox::geometry::multi_line_string;
using mapbox::geometry::polygon;
using mapbox::geometry::linear_ring;
using mapbox::geometry::multi_polygon;
using mapbox::geometry::feature;
using mapbox::geometry::feature_collection;

using point_type = point<long long>;
using multi_point_type = multi_point<long long>;
using line_type = line_string<long long>;
using multi_line_type = multi_line_string<long long>;
using polygon_type = polygon<long long>;
using linear_ring_type = linear_ring<long long>;
using multi_polygon_type = multi_polygon<long long>;
using geometry_type = geometry<long long>;
using feature_type = feature<long long>;

int test_op_within(oppoint op, long long extent) {
    int return_flags = 0;
    if ( op.x < 0 ) {
        return_flags |= WEST;
    } else if ( op.x >= extent ) {
        return_flags |= EAST;
    }
    if ( op.y < 0 ) {
        return_flags |= NORTH;
    } else if ( op.y >= extent ) {
        return_flags |= SOUTH;
    }
    return return_flags;
}

geometry_type extract_geometry(mvt_feature const &feat, long long extent, int &within) {
    std::vector <oppoint> ops;
    for (size_t g = 0; g < feat.geometry.size(); g++) {
        int op = feat.geometry[g].op;
        long long px = feat.geometry[g].x;
        long long py = feat.geometry[g].y;

        if (op == VT_MOVETO || op == VT_LINETO) {
            ops.push_back(oppoint(op, px, py));
        } else {
            ops.push_back(oppoint(op, 0, 0));
        }
    }

    if (feat.type == VT_POINT) {
        if (ops.size() == 1) {
            within = test_op_within(ops[0], extent) == 0;
            return point_type(ops[0].x, ops[0].y);
        } else {
            multi_point_type pm;
            within = true;
            for (size_t i = 0; i < ops.size(); i++) {
                within |= test_op_within(ops[i], extent);
                pm.push_back(point_type(ops[i].x, ops[i].y));
            }
            return pm;
        }
    } else if (feat.type == VT_LINE) {
        int movetos = 0;
        for (size_t i = 0; i < ops.size(); i++) {
            if (ops[i].op == VT_MOVETO) {
                movetos++;
            }
        }

        if (movetos < 2) {
            line_type line;
            within = true;
            for (size_t i = 0; i < ops.size(); i++) {
                within |= test_op_within(ops[i], extent);
                line.push_back(point_type(ops[i].x, ops[i].y));
            }
            return line;
        } else {
            multi_line_type mline;
            int state = 0;

            line_type *lt = new line_type();
            mline.push_back(*lt);
            within = true;
            for (size_t i = 0; i < ops.size(); i++) {
                if (ops[i].op == VT_MOVETO) {
                    if (state == 0) {
                        within |= test_op_within(ops[i], extent);
                        lt->push_back(point_type(ops[i].x, ops[i].y));
                        state = 1;
                    } else {
                        lt = new line_type();
                        mline.push_back(*lt);
                        within |= test_op_within(ops[i], extent);
                        lt->push_back(point_type(ops[i].x, ops[i].y));
                        state = 1;
                    }
                } else {
                    within |= test_op_within(ops[i], extent);
                    lt->push_back(point_type(ops[i].x, ops[i].y));
                }
            }
            return mline;
        }
    } else if (feat.type == VT_POLYGON) {
        std::vector<std::vector<oppoint> > rings;
        std::vector<double> areas;

        for (size_t i = 0; i < ops.size(); i++) {
            if (ops[i].op == VT_MOVETO) {
                rings.push_back(std::vector<oppoint>());
                areas.push_back(0);
            }

            within = true;

            int n = rings.size() - 1;
            if (n >= 0) {
                if (ops[i].op == VT_CLOSEPATH) {
                    rings[n].push_back(rings[n][0]);
                } else {
                    within |= test_op_within(ops[i], extent);
                    rings[n].push_back(ops[i]);
                }
            }

            if (i + 1 >= ops.size() || ops[i + 1].op == VT_MOVETO) {
                if (ops[i].op != VT_CLOSEPATH) {
                    fprintf(stderr, "Ring does not end with closepath (ends with %d)\n", ops[i].op);
                    exit(EXIT_FAILURE);
                }
            }
        }

        int outer = 0;

        for (size_t i = 0; i < rings.size(); i++) {
            long double area = 0;
            for (size_t k = 0; k < rings[i].size(); k++) {
                if (rings[i][k].op != VT_CLOSEPATH) {
                    area += (long double) rings[i][k].x * (long double) rings[i][(k + 1) % rings[i].size()].y;
                    area -= (long double) rings[i][k].y * (long double) rings[i][(k + 1) % rings[i].size()].x;
                }
            }
            area /= 2;

            areas[i] = area;
            if (areas[i] >= 0 || i == 0) {
                outer++;
            }
        }

        polygon_type *poly = new polygon_type();
        linear_ring_type *lr = new linear_ring_type();
        poly->push_back(*lr);

        multi_polygon_type *mpoly = new multi_polygon_type();

        if (outer > 1) {
            mpoly->push_back(*poly);
        }
        int state = 0;
        for (size_t i = 0; i < rings.size(); i++) {
            if (i == 0 && areas[i] < 0) {
                fprintf(stderr, "Polygon begins with an inner ring\n");
                exit(EXIT_FAILURE);
            }

            if (areas[i] >= 0) {
                if (state != 0) {
                    // new multipolygon
                    poly = new polygon_type();
                    lr = new linear_ring_type();
                    poly->push_back(*lr);
                    mpoly->push_back(*poly);
                }
                state = 1;
            }

            if (state == 2) {
                // new ring in the same polygon
                lr = new linear_ring_type();
                poly->push_back(*lr);
            }

            for (size_t j = 0; j < rings[i].size(); j++) {
                if (rings[i][j].op != VT_CLOSEPATH) {
                    lr->push_back(point_type(rings[i][j].x, rings[i][j].y));
                } else {
                    lr->push_back(point_type(rings[i][0].x, rings[i][0].y));
                }
            }
            state = 2;
        }
        if (outer > 1) {
            return *mpoly;
        }
        return *poly;
    }
    within = true;
    return point_type(0, 0);
}

std::vector<feature_type> layer_to_features(mvt_layer const &layer, unsigned z, unsigned x, unsigned y) {
    std::vector<feature_type> features;
    for (size_t f = 0; f < layer.features.size(); f++) {
        mvt_feature const &feat = layer.features[f];
        long long extent = layer.extent;
        int within = 0;
        feature_type current_feature{ extract_geometry(feat, extent, within) };

        auto &p = current_feature.properties;
        p["offtile"] = uint64_t(within);
        for (size_t t = 0; t + 1 < feat.tags.size(); t += 2) {

            if (feat.tags[t] >= layer.keys.size()) {
                fprintf(stderr, "Error: out of bounds feature key (%u in %zu)\n", feat.tags[t], layer.keys.size());
                exit(EXIT_FAILURE);
            }
            if (feat.tags[t + 1] >= layer.values.size()) {
                fprintf(stderr, "Error: out of bounds feature value (%u in %zu)\n", feat.tags[t + 1], layer.values.size());
                exit(EXIT_FAILURE);
            }

            const char *key = layer.keys[feat.tags[t]].c_str();
            mvt_value const &val = layer.values[feat.tags[t + 1]];

            if (val.type == mvt_string) {
                p[key] = std::string(val.string_value.c_str());
            } else if (val.type == mvt_int) {
                p[key] = int64_t(val.numeric_value.int_value);
            } else if (val.type == mvt_double) {
                double v = val.numeric_value.double_value;
                if (v == (long long) v) {
                    p[key] =int64_t(v);
                } else {
                   p[key] = v;
                }
            } else if (val.type == mvt_float) {
                double v = val.numeric_value.float_value;
                if (v == (long long) v) {
                    p[key] =int64_t(v);
                } else {
                    p[key] = v;
                }
            } else if (val.type == mvt_sint) {
                p[key] = int64_t(val.numeric_value.sint_value);
            } else if (val.type == mvt_uint) {
                p[key] = uint64_t(val.numeric_value.uint_value);
            } else if (val.type == mvt_bool) {
                if (val.numeric_value.bool_value) {
                    p[key] = true;
                } else {
                    p[key] = false;
                }
            }
        }
        features.push_back(current_feature);
    }
    return features;
}
