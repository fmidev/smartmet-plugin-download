// ======================================================================
/*!
 * \brief GRIB handling tools
 */
// ======================================================================

#include "GribTools.h"
#include <spine/Exception.h>
#include <newbase/NFmiCommentStripper.h>

#include <boost/lexical_cast.hpp>

#include <iostream>
#include <stdexcept>

// ----------------------------------------------------------------------
// Dump the given namespace attributes
// ----------------------------------------------------------------------

void DUMP(grib_handle *grib, const char *ns)
{
  try
  {
    if (ns == NULL)
      std::cout << "\nValues in global namespace:" << std::endl;
    else
      std::cout << "\nValues for namespace " << ns << ":" << std::endl << std::endl;

    grib_keys_iterator *kiter;
    if (ns == NULL)
      kiter = grib_keys_iterator_new(grib, GRIB_KEYS_ITERATOR_ALL_KEYS, NULL);
    else
      kiter = grib_keys_iterator_new(grib, GRIB_KEYS_ITERATOR_ALL_KEYS, const_cast<char *>(ns));

    if (!kiter)
      throw SmartMet::Spine::Exception(BCP, "Failed to get iterator for grib keys");

    const int MAX_STRING_LEN = 1024;
    char buffer[MAX_STRING_LEN];

    while (grib_keys_iterator_next(kiter))
    {
      const char *name = grib_keys_iterator_get_name(kiter);
      int ret = GRIB_SUCCESS;
      if (grib_is_missing(grib, name, &ret) && ret == GRIB_SUCCESS)
      {
        std::cout << name << " = "
                  << "MISSING" << std::endl;
      }
      else
      {
        int type;
        grib_get_native_type(grib, name, &type);
        switch (type)
        {
          case GRIB_TYPE_STRING:
          {
            size_t len = MAX_STRING_LEN;
            grib_get_string(grib, name, buffer, &len);
            std::cout << name << " = \"" << std::string(buffer, strlen(buffer)) << '"' << std::endl;
            break;
          }
          case GRIB_TYPE_DOUBLE:
          {
            double value;
            grib_get_double(grib, name, &value);
            std::cout << name << " = " << value << std::endl;
            break;
          }
          case GRIB_TYPE_LONG:
          {
            long value;
            grib_get_long(grib, name, &value);
            std::cout << name << " = " << value << std::endl;
            break;
          }
          default:
            std::cout << "Unknown header type in grib with name" << name << std::endl;
        }
      }
    }
    grib_keys_iterator_delete(kiter);
  }
  catch (...)
  {
    throw SmartMet::Spine::Exception(BCP, "Operation failed!", NULL);
  }
}

// ----------------------------------------------------------------------
// Dump all namespaces
// ----------------------------------------------------------------------

void DUMP(grib_handle *grib)
{
  try
  {
    DUMP(grib, NULL);
    DUMP(grib, "geography");
    DUMP(grib, "parameter");
    DUMP(grib, "time");
    DUMP(grib, "vertical");
  }
  catch (...)
  {
    throw SmartMet::Spine::Exception(BCP, "Operation failed!", NULL);
  }
}

// ----------------------------------------------------------------------
// Convenience functions
// ----------------------------------------------------------------------

long get_long(grib_handle *g, const char *name)
{
  try
  {
    long value;
    if (grib_get_long(g, name, &value))
      throw SmartMet::Spine::Exception(
          BCP, "Failed to get long value for name '" + std::string(name) + "'!");
    return value;
  }
  catch (...)
  {
    throw SmartMet::Spine::Exception(BCP, "Operation failed!", NULL);
  }
}

#if 0
double get_double(grib_handle * g, const char * name)
{
  double value;
  if(grib_get_double(g, name, &value))
	throw SmartMet::Spine::Exception(BCP,"Failed to get double value for name '" + name + "'!");
  return value;
}
#endif

void gset(grib_handle *g, const char *name, double value)
{
  try
  {
    if (grib_set_double(g, name, value))
      throw SmartMet::Spine::Exception(BCP,
                                       "Failed to set '" + std::string(name) + "' to value '" +
                                           boost::lexical_cast<std::string>(value) + "'!");
  }
  catch (...)
  {
    throw SmartMet::Spine::Exception(BCP, "Operation failed!", NULL);
  }
}

void gset(grib_handle *g, const char *name, long value)
{
  try
  {
    if (grib_set_long(g, name, value))
      throw SmartMet::Spine::Exception(BCP,
                                       "Failed to set '" + std::string(name) + "' to value '" +
                                           boost::lexical_cast<std::string>(value) + "'!");
  }
  catch (...)
  {
    throw SmartMet::Spine::Exception(BCP, "Operation failed!", NULL);
  }
}

void gset(grib_handle *g, const char *name, unsigned long value)
{
  try
  {
    if (grib_set_long(g, name, value))
      throw SmartMet::Spine::Exception(BCP,
                                       "Failed to set '" + std::string(name) + "' to value '" +
                                           boost::lexical_cast<std::string>(value) + "'!");
  }
  catch (...)
  {
    throw SmartMet::Spine::Exception(BCP, "Operation failed!", NULL);
  }
}

void gset(grib_handle *g, const char *name, int value)
{
  try
  {
    if (grib_set_long(g, name, value))
      throw SmartMet::Spine::Exception(BCP,
                                       "Failed to set '" + std::string(name) + "' to value '" +
                                           boost::lexical_cast<std::string>(value) + "'!");
  }
  catch (...)
  {
    throw SmartMet::Spine::Exception(BCP, "Operation failed!", NULL);
  }
}

void gset(grib_handle *g, const char *name, const char *value)
{
  try
  {
    size_t len = strlen(value);
    if (grib_set_string(g, name, value, &len))
      throw SmartMet::Spine::Exception(
          BCP, "Failed to set '" + std::string(name) + "' to value '" + std::string(value) + "'!");
  }
  catch (...)
  {
    throw SmartMet::Spine::Exception(BCP, "Operation failed!", NULL);
  }
}

void gset(grib_handle *g, const char *name, const std::string &value)
{
  try
  {
    size_t len = value.size();
    if (grib_set_string(g, name, value.c_str(), &len))
      throw SmartMet::Spine::Exception(
          BCP, "Failed to set '" + std::string(name) + "' to value '" + std::string(value) + "'!");
  }
  catch (...)
  {
    throw SmartMet::Spine::Exception(BCP, "Operation failed!", NULL);
  }
}
