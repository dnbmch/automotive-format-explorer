#pragma once

// Protobuf JSON compatibility shim.
// v4+ (protobuf 28+): google/protobuf/json/json.h
//   - namespace google::protobuf::json, PrintOptions
// v3 (protobuf 3.x):  google/protobuf/util/json_util.h
//   - namespace google::protobuf::util, JsonPrintOptions

#include <google/protobuf/stubs/common.h>

#if GOOGLE_PROTOBUF_VERSION >= 5028000
#include <google/protobuf/json/json.h>
namespace proto_json = google::protobuf::json;
#else
#include <google/protobuf/util/json_util.h>
namespace proto_json {
using PrintOptions = google::protobuf::util::JsonPrintOptions;
using google::protobuf::util::MessageToJsonString;
}
#endif
