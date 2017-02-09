// ======================================================================
/*!
 * Grib tools
 */
// ----------------------------------------------------------------------

#pragma once

#include <grib_api.h>
#include <string>

// Debugging tools

void DUMP(grib_handle* grib);
void DUMP(grib_handle* grib, const char* ns);

// Convenience functions

long get_long(grib_handle* g, const char* name);
long get_double(grib_handle* g, const char* name);
void gset(grib_handle* g, const char* name, double value);
void gset(grib_handle* g, const char* name, long value);
void gset(grib_handle* g, const char* name, unsigned long value);
void gset(grib_handle* g, const char* name, int value);
void gset(grib_handle* g, const char* name, const char* value);
void gset(grib_handle* g, const char* name, const std::string& value);
