//
// Created by Stephen E Ingram on 11/9/17.
//

#include <typeinfo>
#include <iostream>
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

bool intercept_segments(long long x1, long long y1, long long x2, long long y2, long long x3, long long y3, long long x4, long long y4, double &x_intercept, double &y_intercept) {
    double denominator = (double)(((x1 - x2)*(y3 - y4)) - ((y1 - y2)*(x3 - x4)));
    if (denominator == 0.0) {
        return false;
    }

    double x = (double)(((x2*y2 - y1*x2) * (x3 - x4)) - ((x1 - x2) * (x3*y4 - y3*x4))) / denominator;
    double y = (double)(((x1*y2 - y1*x2) * (y3 - y4)) - ((y1 - y2) * (x3*y4 - y3*x4))) / denominator;
    x_intercept = x;
    y_intercept = y;
    return true;
}

int edge_intercept(int edge, point_type p1, point_type p2, long long extent, point_type &intercept) {
    bool vertical = p1.x == p2.x;
    double x_intercept = 0.0;
    double y_intercept = 0.0;
    switch (edge) {
        case EAST:
            if (vertical) {
                if (p1.x == extent - 1) {
                    return COINCIDENT;
                }
                return NO_INTERCECT;
            } else {
                if ( intercept_segments(0, 0, 0, extent-1, p1.x, p1.y, p2.x, p2.y, x_intercept, y_intercept) ) {
                    if ( x_intercept != double(0) ) {
                        return NO_INTERCECT;
                    } else if ( y_intercept < 0 || y_intercept >= double(extent) ) {
                        return NO_INTERCECT;
                    }
                    intercept.x = floor(x_intercept + 0.5);
                    intercept.y = floor(y_intercept + 0.5);
                    return INTERCECT;
                } else {
                    return COINCIDENT;
                }
            }
        case WEST:
            if (vertical) {
                if (p1.x == 0) {
                    return COINCIDENT;
                }
                return NO_INTERCECT;
            } else {
                if ( intercept_segments(extent-1, 0, extent-1, extent-1, p1.x, p1.y, p2.x, p2.y, x_intercept, y_intercept) ) {
                    if ( x_intercept != double(extent-1) ) {
                        return NO_INTERCECT;
                    } else if ( y_intercept < 0.0 || y_intercept >= double(extent) ) {
                        return NO_INTERCECT;
                    }
                    intercept.x = floor(x_intercept + 0.5);
                    intercept.y = floor(y_intercept + 0.5);
                    return INTERCECT;
                } else {
                    return COINCIDENT;
                }
            }
        case NORTH:
            if ( vertical ) {
                x_intercept = p1.x;
                y_intercept = 0.0;
            } else if ( intercept_segments(0, 0, extent-1, 0, p1.x, p1.y, p2.x, p2.y, x_intercept, y_intercept) ) {
                if ( y_intercept != double(0) ) {
                    return NO_INTERCECT;
                } else if ( x_intercept < 0.0 || x_intercept >= double(extent) ) {
                    return NO_INTERCECT;
                }
                intercept.x = floor(x_intercept + 0.5);
                intercept.y = floor(y_intercept + 0.5);
                return INTERCECT;
            } else {
                return COINCIDENT;
            }
        case SOUTH:
            if ( vertical ) {
                x_intercept = p1.x;
                y_intercept = extent-1;
            } else if ( intercept_segments(0, extent-1, extent-1, extent-1, p1.x, p1.y, p2.x, p2.y, x_intercept, y_intercept) ) {
                if ( y_intercept != double(extent-1) ) {
                    return NO_INTERCECT;
                } else if ( x_intercept < 0.0 || x_intercept >= double(extent) ) {
                    return NO_INTERCECT;
                }
                intercept.x = floor(x_intercept + 0.5);
                intercept.y = floor(y_intercept + 0.5);
                return INTERCECT;
            } else {
                return COINCIDENT;
            }
    }
    return NO_INTERCECT;
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
            within = test_op_within(ops[0], extent);
            return point_type(ops[0].x, ops[0].y);
        } else {
            multi_point_type pm;
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
            for (size_t i = 0; i < ops.size(); i++) {
                within |= test_op_within(ops[i], extent);
                line.push_back(point_type(ops[i].x, ops[i].y));
            }
            return line;
        } else {
            multi_line_type mline;
            int state = 0;

            line_type lt;
            for (size_t i = 0; i < ops.size(); i++) {
                if (ops[i].op == VT_MOVETO) {
                    if (state == 0) {
                        within |= test_op_within(ops[i], extent);
                        lt.push_back(point_type(ops[i].x, ops[i].y));
                        state = 1;
                    } else {
                        mline.push_back(lt);
                        lt.clear();
                        within |= test_op_within(ops[i], extent);
                        lt.push_back(point_type(ops[i].x, ops[i].y));
                        state = 1;
                    }
                } else {
                    within |= test_op_within(ops[i], extent);
                    lt.push_back(point_type(ops[i].x, ops[i].y));
                }
            }
            mline.push_back(lt);
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

        polygon_type poly;
        linear_ring_type lr;

        multi_polygon_type mpoly;

        int state = 0;
        for (size_t i = 0; i < rings.size(); i++) {
            if (i == 0 && areas[i] < 0) {
                fprintf(stderr, "Polygon begins with an inner ring\n");
                exit(EXIT_FAILURE);
            }

            if (areas[i] >= 0) {
                if (state != 0) {
                    // new multipolygon
                    poly.push_back(lr);
                    mpoly.push_back(poly);
                    lr.clear();
                    poly.clear();
                }
                state = 1;
            }

            if (state == 2) {
                // new ring in the same polygon
                poly.push_back(lr);
                lr.clear();
            }

            for (size_t j = 0; j < rings[i].size(); j++) {
                if (rings[i][j].op != VT_CLOSEPATH) {
                    lr.push_back(point_type(rings[i][j].x, rings[i][j].y));
                } else {
                    lr.push_back(point_type(rings[i][0].x, rings[i][0].y));
                }
            }
            state = 2;
        }
        if (outer > 1) {
            poly.push_back(lr);
            mpoly.push_back(poly);
            return mpoly;
        }
        poly.push_back(lr);
        return poly;
    }
    within = 0x0;
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

struct property_writer {
    property_writer() {}

    void operator()(std::string const & s) const {
        fprintf(stdout, "\"%s\"", s.c_str());
    }

    void operator()(uint64_t const & u) const {
        fprintf(stdout, "%llu", u);
    }

    void operator()(int64_t const & i) const {
        fprintf(stdout, "%lld", i);
    }

    void operator()(bool const & b) const {
        fprintf(stdout, "%s", b ? "true" : "false");
    }

    void operator()(double const & d) const {
        fprintf(stdout, "%f", d);
    }

    template <typename T>
    void operator()(T const & g) const {
        std::clog << "encountered unhandled value";
    }
};

struct shape_writer {
    shape_writer() {}

    void operator()(point_type const & pt) const {
        fprintf(stdout, "{ \"type\": \"Point\", \"coordinates\": [ %lld, %lld ] }", pt.x, pt.y);
    }

    void operator()(multi_point_type  & pts) const {
        fprintf(stdout, "{ \"type\": \"MultiPoint\", \"coordinates\": [");
        bool comma = false;
        for (auto pt = pts.begin(); pt != pts.end(); ++pt) {
             if ( comma ) {
                 fprintf(stdout, ",");
             } else {
                 comma = true;
             }
            fprintf(stdout, " [ %lld, %lld ]", pt->x, pt->y);
        }
        fprintf(stdout, " ] }");
    }


    void operator()(line_type const & ln) const {
        fprintf(stdout, "{ \"type\": \"LineString\", \"coordinates\": [");
        bool comma = false;
        for ( auto pt = ln.begin(); pt != ln.end(); ++pt) {
            if ( comma ) {
                fprintf(stdout, ",");
            } else {
                comma = true;
            }
            fprintf(stdout, " [ %lld, %lld ]", pt->x, pt->y);
        }
        fprintf(stdout, " ] }");
    }

    void operator()(multi_line_type const & lns) const {
        fprintf(stdout, "{ \"type\": \"MultiLineString\", \"coordinates\": [");
        bool line_comma = false;

        for ( auto it = lns.begin(); it != lns.end(); ++it ) {
            if ( line_comma ) {
                fprintf(stdout, ",");
            } else {
                line_comma = true;
            }

            bool comma = false;
            fprintf(stdout, " [");
            for ( auto pt = it->begin(); pt != it->end(); ++pt) {
                if ( comma ) {
                    fprintf(stdout, ",");
                } else {
                    comma = true;
                }
                fprintf(stdout, " [ %lld, %lld ]", pt->x, pt->y);
            }
            fprintf(stdout, " ]");
        }
        fprintf(stdout, " ] }");
    }

    void operator()(polygon_type const & poly) const {
        fprintf(stdout, "{ \"type\": \"Polygon\", \"coordinates\": [");
        bool ring_comma = false;
        for ( auto ring = poly.begin(); ring != poly.end(); ++ring) {
            if ( ring_comma ) {
                fprintf(stdout, ",");
            } else {
                ring_comma = true;
            }

            fprintf(stdout, " [");
            bool comma = false;
            for ( auto pt = ring->begin(); pt != ring->end(); ++pt) {
                if (comma) {
                    fprintf(stdout, ",");
                } else {
                    comma = true;
                }
                fprintf(stdout, " [ %lld, %lld ]", pt->x, pt->y);
            }
            fprintf(stdout, " ]");
        }
        fprintf(stdout, " ] }");
    }

    void operator()(multi_polygon_type const & mpoly) const {
        fprintf(stdout, "{ \"type\": \"MultiPolygon\", \"coordinates\": [");
        bool poly_comma = false;
        for ( auto poly = mpoly.begin(); poly != mpoly.end(); ++poly) {
            if ( poly_comma ) {
                fprintf(stdout, ",");
            } else {
                poly_comma = true;
            }
            fprintf(stdout, " [");
            bool ring_comma = false;
            for ( auto ring = poly->begin(); ring != poly->end(); ++ring) {
                if (ring_comma) {
                    fprintf(stdout, ",");
                } else {
                    ring_comma = true;
                }
                fprintf(stdout, " [");
                bool comma = false;
                for ( auto pt = ring->begin(); pt != ring->end(); ++pt) {
                    if (comma) {
                        fprintf(stdout, ",");
                    } else {
                        comma = true;
                    }
                    fprintf(stdout, " [ %lld, %lld ]", pt->x, pt->y);
                }
                fprintf(stdout, " ]");
            }
            fprintf(stdout, " ]");
        }
        fprintf(stdout, " ] }");
    }

    template <typename T>
    void operator()(T const & g) const {
        std::clog << "encountered unhandled shape";
    }
};

std::vector<feature_type> output_within_tile(std::vector<feature_type> features, int z, unsigned x, unsigned y, bool &feature_comma) {
    property_writer w;
    shape_writer sw;
    std::vector<feature_type> retval;
    for ( feature_type feat : features ) {
        // if within tile
        if ( feat.properties[std::string("offtile")] == uint64_t(0) ) {
            if ( feature_comma ) {
                fprintf(stdout, ",\n");
            } else {
                feature_comma = true;
            }
            fprintf(stdout, "{ \"type\": \"Feature\",");
            fprintf(stdout, "  \"properties\": {");
            bool comma = false;
            for ( auto it = feat.properties.begin(); it != feat.properties.end(); ++it ) {
                if ( comma ) {
                    fprintf(stdout, ",");
                } else {
                    comma = true;
                }
                fprintf(stdout, " \"%s\": ", it->first.c_str());
                mapbox::util::apply_visitor(w, it->second);
            }
            fprintf(stdout, "},");
            fprintf(stdout, " \"geometry\": ");
            mapbox::util::apply_visitor(sw, feat.geometry);
            fprintf(stdout, "}");
        } else {
            retval.push_back( feat );
        }
    }
    return retval;
}
