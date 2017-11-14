//
// Created by Stephen E Ingram on 11/9/17.
//

#ifndef TIPPECANOE_READLAYER_H
#define TIPPECANOE_READLAYER_H

#include "mvt.hpp"
#include <mapbox/geometry/feature.hpp>

using mapbox::geometry::feature_collection;
using feature_collection_type = feature_collection<long long>;
using mapbox::geometry::feature;
using feature_type = feature<long long>;

std::vector<feature_type> layer_to_features(mvt_layer const &layer, unsigned z, unsigned x, unsigned y);

const int NORTH = 0x01;
const int SOUTH = 0x02;
const int EAST = 0x04;
const int WEST = 0x08;

#endif //TIPPECANOE_READLAYER_H
