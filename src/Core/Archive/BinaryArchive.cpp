//===-- Core/Archive/BinaryArchive.cpp ----------------------------------------------*- C++ -*-===//
//
//                                    S E R I A L B O X
//
// This file is distributed under terms of BSD license.
// See LICENSE.txt for more information
//
//===------------------------------------------------------------------------------------------===//
//
/// \file
/// This file implements the non-portable binary archive.
///
//===------------------------------------------------------------------------------------------===//

#include "serialbox/Core/Archive/BinaryArchive.h"
#include "serialbox/Core/SHA256.h"
#include "serialbox/Core/Version.h"
#include <fstream>
#include <iostream>

namespace serialbox {

const int BinaryArchive::Version = 0;

BinaryArchive::~BinaryArchive() {
  updateMetaData();
}

void BinaryArchive::readMetaDataFromJson() {
  fieldTable_.clear();
  json_.clear();

  // Writing always operates on fresh directories
  if(mode_ == OpenModeKind::Write)
    return;
  
  // Check if metaData file exists
  if(!boost::filesystem::exists(directory_ / Archive::ArchiveName)) {
    if(mode_ == OpenModeKind::Append)
      return;
    throw Exception("archive meta data not found in directory '%s'", directory_.string());    
  }

  std::ifstream fs((directory_ / Archive::ArchiveName).string(), std::ios::in);
  fs >> json_;
  fs.close();

  int serialboxVersion = json_["serialbox_version"];
  int binaryArchiveVersion = json_["binary_archive_version"];

  // Check versions
  if(Version::equals(serialboxVersion))
    throw Exception("serialbox version of binary archive meta data (%s) does not match the version "
                    "of the library (%s)",
                    Version::toString(serialboxVersion), SERIALBOX_VERSION_STRING);

  if(binaryArchiveVersion != BinaryArchive::Version)
    throw Exception("binary archive version (%s) does not match the version of the library (%s)",
                    binaryArchiveVersion, BinaryArchive::Version);

  // Deserialize FieldsTable  
  for(auto it = json_["fields_table"].begin(); it != json_["fields_table"].end(); ++it) {
    FieldOffsetTable fieldOffsetTable;
    
    // Iterate over savepoint of this field
    for(auto fileOffsetIt = it->begin(); fileOffsetIt != it->end(); ++fileOffsetIt)
      fieldOffsetTable.push_back(FileOffsetType{fileOffsetIt->at(0), fileOffsetIt->at(1)});    
    
    fieldTable_[it.key()] = fieldOffsetTable;
  }
}

void BinaryArchive::writeMetaDataToJson() {
  json_.clear();
  
  // Tag versions
  json_["serialbox_version"] =
      100 * SERIALBOX_VERSION_MAJOR + 10 * SERIALBOX_VERSION_MINOR + SERIALBOX_VERSION_PATCH;
  json_["binary_archive_version"] = BinaryArchive::Version;

  // FieldsTable
  for(auto it = fieldTable_.begin(), end = fieldTable_.end(); it != end; ++it) {
    for(int id = 0; id < it->second.size(); ++id)
      json_["fields_table"][it->first].push_back({it->second[id].offset, it->second[id].checksum});
  }
   
  // Write metaData to disk (just overwrite the file, we assume that there is never more than one
  // Archive per data set and thus our in-memory copy is always the up-to-date one)
  std::ofstream fs((directory_ / Archive::ArchiveName).string(), std::ofstream::trunc);
  fs << json_.dump(4) << std::endl;
  fs.close();
}

BinaryArchive::BinaryArchive(const boost::filesystem::path& directory, OpenModeKind mode)
    : mode_(mode), directory_(directory), json_(), metaDataDirty_(false) {
  try {
    bool isDir = boost::filesystem::is_directory(directory_);

    switch(mode_) {
    // We are reading, the directory needs to exist
    case OpenModeKind::Read:
      if(!isDir)
        throw Exception("no such directory: '%s'", directory_.string());
      break;
    // We are writing, the directory has to be empty
    case OpenModeKind::Write:
      if(isDir && !boost::filesystem::is_empty(directory_))
        throw Exception("directory '%s' is not empty", directory_.string());
    // We are appending, create directory if it does not exist
    case OpenModeKind::Append:
      if(!isDir)
        boost::filesystem::create_directory(directory_);
      break;
    }
  } catch(boost::filesystem::filesystem_error& e) {
    throw Exception(e.what());
  }

  readMetaDataFromJson();
}

void BinaryArchive::updateMetaData() {
  if(metaDataDirty_) 
    writeMetaDataToJson();
  metaDataDirty_ = false;
}

//===------------------------------------------------------------------------------------------===//
//     Writing
//===------------------------------------------------------------------------------------------===//

void BinaryArchive::write(StorageView& storageView, const FieldID& fieldID) throw(Exception) {
  if(mode_ == OpenModeKind::Read)
    throw Exception("Archive is not initialized with OpenModeKind set to 'Write' or 'Append'");

  // Check if a field with given id and name already exists
  auto it = fieldTable_.find(fieldID.name);

  std::string filename((directory_ / (fieldID.name + ".dat")).string());
  std::ofstream fs;
  
  // Create binary data buffer
  std::vector<Byte> binaryData;
  try {
    binaryData.resize(storageView.sizeInBytes());
  } catch(std::bad_alloc&) {
    throw Exception("out of memory");
  }

  Byte* dataPtr = binaryData.data();
  const int bytesPerElement = storageView.bytesPerElement();

  // Copy field into contiguous memory
  for(auto it = storageView.begin(), end = storageView.end(); it != end;
      ++it, dataPtr += bytesPerElement)
    std::memcpy(dataPtr, it.ptr(), bytesPerElement);
  
  // Compute hash
  std::string checksum(SHA256::hash(binaryData.data(), binaryData.size()));

  // Field does exists
  if(it != fieldTable_.end()) {
    FieldOffsetTable& fieldOffsetTable = it->second;
    
    // Do we append at the end?
    if(fieldID.id >= fieldOffsetTable.size()) { 
      fs.open(filename, std::ofstream::binary | std::ofstream::app);          
      auto offset = fs.tellp();   
      
      fieldOffsetTable.push_back(FileOffsetType{offset, checksum});
    }
    // Replace data
    else {
      fs.open(filename, std::ofstream::binary);          
      auto offset = fieldOffsetTable[fieldID.id].offset; 
//      std::cout << offset << std::endl;
      fs.seekp(offset);
      
      fieldOffsetTable[fieldID.id] = FileOffsetType{offset, checksum};
    }
  }
  // Field does not exist, create new file and append data
  else {
    fs.open(filename, std::ofstream::binary | std::ofstream::trunc);
    
    fieldTable_.insert(FieldTable::value_type(
        fieldID.name, FieldOffsetTable(1, FileOffsetType{0, checksum})));
  }

  if(!fs.is_open())
    throw Exception("cannot open file: '%s'", filename);

  // Write binaryData to disk
  fs.write(binaryData.data(), binaryData.size());
  fs.close();
  
  metaDataDirty_ = true;
  updateMetaData();
}

//===------------------------------------------------------------------------------------------===//
//     Reading
//===------------------------------------------------------------------------------------------===//

void BinaryArchive::read(StorageView& storageView, const FieldID& fieldID) throw(Exception) {
  if(mode_ != OpenModeKind::Read)
    throw Exception("Archive is not initialized with OpenModeKind set to 'Read'");

  // Check if field exists
  auto it = fieldTable_.find(fieldID.name);
  if(it == fieldTable_.end())
    throw Exception("no field '%s' registered in BinaryArchive", fieldID.name);

  const FieldOffsetTable& fieldOffsetTable = it->second;

  // Check if id is valid
  if(fieldID.id >= fieldOffsetTable.size())
    throw Exception("invalid id '%i' of field '%s'", fieldID.id, fieldID.name);

  // Allocate binary data
  std::vector<Byte> binaryData;
  try {
    binaryData.resize(storageView.sizeInBytes());
  } catch(std::bad_alloc&) {
    throw Exception("out of memory");
  }

  // Open file & read into binary buffer
  std::string filename((directory_ / (fieldID.name + ".dat")).string());
  std::ifstream fs(filename, std::ios::binary);

  if(!fs.is_open())
    throw Exception("cannot open file: '%s'", filename);

  // Set position in the steram
  auto offset = fieldOffsetTable[fieldID.id].offset;
  fs.seekg(offset);

  // Read data into contiguous memory
  fs.read(binaryData.data(), binaryData.size());
  fs.close();

  Byte* dataPtr = binaryData.data();
  const int bytesPerElement = storageView.bytesPerElement();
  
  // Compute hash and compare
  std::string checksum(SHA256::hash(binaryData.data(), binaryData.size()));

  
  //  std::cout << checksum << std::endl;
  //  std::cout << *this << std::endl;
  //  std::cout << fieldOffsetTable[fieldID.id].checksum << std::endl;
  
  if(checksum != fieldOffsetTable[fieldID.id].checksum)
    throw Exception("hashsum mismatch for field '%s' at id '%i'", fieldID.name, fieldID.id);

  // Copy contiguous memory into field
  for(auto it = storageView.begin(), end = storageView.end(); it != end;
      ++it, dataPtr += bytesPerElement)
    std::memcpy(it.ptr(), dataPtr, bytesPerElement);
}

std::ostream&  BinaryArchive::toStream(std::ostream& stream) const {
  stream << "BinaryArchive [\n";
  stream << "  directory = " << directory_.string() << "\n";
  stream << "  mode = " << mode_ << "\n";
  stream << "  fieldsTable = [\n";
  for(auto it = fieldTable_.begin(), end = fieldTable_.end(); it != end; ++it) {
    stream << "    " << it->first << " = {\n";
    for(int id = 0; id < it->second.size(); ++id) {
      stream << "      [ " << it->second[id].offset << ",\n";
      stream << "        " << it->second[id].checksum << " ]\n";
    }
    stream << "    }\n";
  }
  stream << "  ]\n";
  stream << "]\n";
  return stream;
}

} // namespace serialbox
