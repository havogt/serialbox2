//===-- Unittest/FileUtility.h ------------------------------------------------------*- C++ -*-===//
//
//                                    S E R I A L B O X
//
// This file is distributed under terms of BSD license.
// See LICENSE.txt for more information
//
//===------------------------------------------------------------------------------------------===//
//
/// \file
/// This file implements File and Directory classes with RAII support for unittesting.
///
//===------------------------------------------------------------------------------------------===//

#ifndef SERIALBOX_UNITTEST_FILEUTILITY_H
#define SERIALBOX_UNITTEST_FILEUTILITY_H

#include "UnittestEnvironment.h"
#include <boost/filesystem.hpp>
#include <fstream>

namespace serialbox {

namespace unittest {

namespace internal {

template <class DerivedType>
class FileBase : private boost::noncopyable {
public:
  using value_type = boost::filesystem::path::value_type;
  using string_type = boost::filesystem::path::string_type;

  FileBase(const boost::filesystem::path& path) : path_(path) { create(); }
  FileBase(boost::filesystem::path& path) : path_(path) { create(); }
  FileBase(const string_type& path) : path_(path) { create(); }
  FileBase(string_type& path) : path_(path) { create(); }
  FileBase(const value_type* path) : path_(path) { create(); }
  FileBase(value_type* path) : path_(path) { create(); }

  const boost::filesystem::path& path() const noexcept { return path_; }
  boost::filesystem::path& path() noexcept { return path_; }

private:
  void create() { static_cast<DerivedType&>(*this).createImpl(path_); }

protected:
  boost::filesystem::path path_;
};

} // namespace internal

/// \brief File with RAII cleanup
class File : public internal::FileBase<File> {
public:
  using Base = internal::FileBase<File>;

  // Constructors
  template <class T>
  File(T&& path) : Base(path) {}

  // Destruction
  ~File() {
    if(UnittestEnvironment::getInstance().cleanup()) {    
      boost::system::error_code ec;
      boost::filesystem::remove(path_, ec);
    }
  }

  void createImpl(const boost::filesystem::path& path) {
    std::fstream fs(path.string());
    fs.close();
  }
};

/// \brief Directory with RAII cleanup
class Directory : public internal::FileBase<Directory> {
public:
  using Base = internal::FileBase<Directory>;

  // Constructors
  template <class T>
  Directory(T&& path) : Base(path) {}

  // Destruction
  ~Directory() {
    if(UnittestEnvironment::getInstance().cleanup()) {
      boost::system::error_code ec;
      boost::filesystem::remove_all(path_, ec);
    }
  }

  void createImpl(const boost::filesystem::path& path) {
    boost::filesystem::create_directories(path);
  }
};

} // namespace unittest

} // namespace serialbox

#endif
