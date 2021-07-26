// Copyright 2020 Google Inc. All Rights Reserved.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include "stdint.h"
#include <string>
#include "disk_interface.h"
#include "state.h"
#include "manifest_parser.h"
#include <filesystem>

extern "C" int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
	char build_file[256];
	sprintf(build_file, "/tmp/build.ninja");
	FILE *fp = fopen(build_file, "wb");
	if (!fp)
		return 0;
	fwrite(data, size, 1, fp);
	fclose(fp);	
	
	std::string err;
	RealDiskInterface disk_interface;
	State state;
	ManifestParser parser(&state, &disk_interface);
	
	parser.Load("/tmp/build.ninja", &err);
	
	std::__fs::filesystem::remove_all("/tmp/build.ninja");
	return 0;
}
