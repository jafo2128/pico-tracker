/**
 * Autogenerated aprs zone structures. See sim/geofence
 */

#ifndef GEOFENCE_APRS_ZONES_H
#define GEOFENCE_APRS_ZONES_H


#include <stdbool.h>

#include "samd20.h"


/**
 * struct representing all we need to know about a aprs zone
 */
struct aprs_zone_t {
  const int16_t** outlines;
  const uint16_t outline_count;
  const uint16_t* outline_lengths;
  int32_t frequency;
  int32_t deviation;
  char* name;
  char* call;
};


const struct aprs_zone_t aprs_zones[11];

#endif /* GEOFENCE_APRS_ZONES_H */
